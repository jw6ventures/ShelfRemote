#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QTextStream>

#include <clocale>

#include "app/AppConfig.h"
#include "app/AppSettings.h"
#include "app/Application.h"
#include "app/Backend.h"
#include "app/DebugLog.h"
#include "app/UriHandler.h"
#include "auth/AuthManager.h"
#include "auth/TokenStore.h"
#include "model/BookmarkStore.h"
#include "model/ProgressStore.h"
#include "net/ApiClient.h"
#include "player/MpvController.h"
#include "player/MprisAdapter.h"
#include "player/PlaybackSession.h"
#include "server/ServerManager.h"
#include "storage/CoverCache.h"
#include "storage/Database.h"
#include "storage/SecureStore.h"

namespace {
QtMessageHandler g_prevHandler = nullptr;

// Tees Qt log messages to a rotating file (for the Settings "Save debug log"
// action) in addition to the default stderr output.
void logToFileHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    static QMutex mutex;
    static const QString path = AppConfig::logFilePath();
    {
        QMutexLocker lock(&mutex);
        QFile f(path);
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            const char *lvl = "";
            switch (type) {
            case QtDebugMsg:    lvl = "DEBUG"; break;
            case QtInfoMsg:     lvl = "INFO";  break;
            case QtWarningMsg:  lvl = "WARN";  break;
            case QtCriticalMsg: lvl = "ERROR"; break;
            case QtFatalMsg:    lvl = "FATAL"; break;
            }
            QTextStream ts(&f);
            ts << QDateTime::currentDateTimeUtc().toString(Qt::ISODate)
               << " [" << lvl << "] " << msg << '\n';
        }
    }
    if (g_prevHandler)
        g_prevHandler(type, ctx, msg);
}

// Keep the log bounded: rotate to a single .1 backup once it passes ~1 MB.
void rotateLogIfLarge()
{
    const QString path = AppConfig::logFilePath();
    const QFileInfo fi(path);
    if (fi.exists() && fi.size() > 1 * 1024 * 1024) {
        const QString bak = path + QStringLiteral(".1");
        QFile::remove(bak);
        QFile::rename(path, bak);
    }
}
} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // libmpv requires LC_NUMERIC=C; QGuiApplication's constructor may set a
    // locale from the environment, so reset it here before mpv is created.
    std::setlocale(LC_NUMERIC, "C");
    QGuiApplication::setApplicationName(QStringLiteral("ShelfRemote"));
    QGuiApplication::setApplicationVersion(AppConfig::version());
    QGuiApplication::setDesktopFileName(AppConfig::appId());

    // Start file logging early so startup diagnostics are captured too.
    rotateLogIfLarge();
    g_prevHandler = qInstallMessageHandler(logToFileHandler);

    // Explicit Basic style: no KDE/GTK theme coupling, fully custom 10-foot UI.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // The desktop passes the custom-scheme callback as %u; capture it.
    const QString callbackArg = (argc > 1) ? QString::fromLocal8Bit(argv[1]) : QString();

    // --- Single instance --------------------------------------------------
    SingleInstanceGuard guard;
    if (!guard.acquire(callbackArg)) {
        // A primary instance exists; we forwarded the URI (if any). Exit.
        return 0;
    }

    // --- Storage ----------------------------------------------------------
    // If this fails, saved servers, tokens, and settings cannot persist; log it
    // loudly rather than letting every later DB call fail silently.
    if (!Database::instance().open())
        qCritical() << "Failed to open the application database; "
                       "server profiles, tokens, and settings will not persist";

    // --- Core objects (owned here, exposed to QML) ------------------------
    auto *api = new ApiClient(&app);
    auto *secure = new SecureStore(&app);
    auto *tokens = new TokenStore(api, secure, &app);
    auto *uris = new UriHandler(&app);
    auto *auth = new AuthManager(api, tokens, uris, &app);
    // Any request that 401s triggers a token refresh and one retry (single-flight
    // inside TokenStore), so long-running sessions survive access-token expiry.
    api->setUnauthorizedHandler([tokens](std::function<void(bool)> done) {
        tokens->refresh(std::move(done));
    });
    auto *servers = new ServerManager(&app);
    auto *covers = new CoverCache(api, &app);
    // Both caches are append-only during a run. Trim them once at startup so an
    // installation that has browsed a large library for months does not carry an
    // ever-growing cover directory and item_cache table. Both hold only data that
    // is re-fetched on demand, so evicting the oldest entries costs nothing but a
    // request the next time they are shown.
    covers->pruneToLimit();
    Database::instance().pruneItemCache(2000);
    auto *backend = new Backend(api, &app);
    auto *progress = new ProgressStore(&app);
    backend->setProgressStore(progress);
    auto *bookmarks = new BookmarkStore(api, &app);
    auto *settings = new AppSettings(&app);
    auto *debugLog = new DebugLog(&app);

    auto *mpv = new MpvController(&app);
    if (!mpv->init())
        qCritical() << "Failed to initialize libmpv; playback will be unavailable";
    // Apply the persisted audio output choice before any file is loaded.
    mpv->setAudioDevice(settings->audioDevice());
    auto *playback = new PlaybackSession(api, mpv, &app);
    auto *mpris = new MprisAdapter(playback, &app);
    mpris->registerOnBus();

    // Route forwarded callback URIs (from secondary launches) into the handler.
    QObject::connect(&guard, &SingleInstanceGuard::uriReceived, uris, &UriHandler::handle);
    // Handle a callback that arrived on our own command line (cold start).
    if (!callbackArg.isEmpty())
        uris->handle(callbackArg);

    // After authentication, load the user's libraries and capability version.
    // reset() first so that if this is a *different* server than a prior session,
    // none of that server's libraries/home/search state (or its in-flight
    // responses) can bleed into the newly authenticated one.
    QObject::connect(auth, &AuthManager::authenticated, backend, [=](const QJsonObject &user) {
        servers->setServerVersion(auth->serverVersion());
        backend->reset();
        progress->loadFromUser(user);
        bookmarks->loadFromUser(user);
        backend->loadLibraries();
    });
    // Symmetric with authenticated(): on logout (emitted while the token is still
    // valid) close the playback session with a final authenticated /close, stop
    // mpv, and drop this server's content + progress state BEFORE credentials are
    // cleared. Ordering matters — playback first so its /close is authenticated,
    // then the content/progress reset.
    QObject::connect(auth, &AuthManager::sessionEnding, playback, [=]() {
        playback->stopAndClose();
        backend->reset();
        progress->clear();
        bookmarks->clear();
    });
    // Reflect local playback progress immediately in the browse cards when a
    // session closes. Podcast episodes are NOT item-level progress (the store,
    // like /api/authorize, tracks progress per library item, not per episode), so
    // writing an episode's position under the podcast id would mislabel it — skip
    // those. `completed()` distinguishes a natural end-of-book from a manual stop.
    QObject::connect(playback, &PlaybackSession::activeChanged, progress, [=]() {
        if (playback->active() || !playback->episodeId().isEmpty())
            return;
        progress->update(playback->itemId(), playback->position(),
                         playback->duration(), playback->completed());
    });
    // Persist the server on successful discovery/login.
    QObject::connect(auth, &AuthManager::authenticated, servers, [=](const QJsonObject &user) {
        servers->saveServer(api->baseUrl(), api->baseUrl().host(),
                            user.value(QStringLiteral("id")).toString());
    });

    // Flush a final progress sync + close on shutdown. Blocking so the request is
    // transmitted before the event loop exits.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, playback,
                     [playback]() { playback->flushAndClose(true); });

    // --- QML --------------------------------------------------------------
    QQmlApplicationEngine engine;
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("Auth"), auth);
    ctx->setContextProperty(QStringLiteral("Backend"), backend);
    ctx->setContextProperty(QStringLiteral("Playback"), playback);
    ctx->setContextProperty(QStringLiteral("Covers"), covers);
    ctx->setContextProperty(QStringLiteral("Progress"), progress);
    ctx->setContextProperty(QStringLiteral("Bookmarks"), bookmarks);
    ctx->setContextProperty(QStringLiteral("Servers"), servers);
    ctx->setContextProperty(QStringLiteral("AppSettings"), settings);
    ctx->setContextProperty(QStringLiteral("Player"), mpv);
    ctx->setContextProperty(QStringLiteral("DebugLog"), debugLog);
    ctx->setContextProperty(QStringLiteral("appVersion"), AppConfig::version());

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     [](const QUrl &u) { qWarning() << "QML load failed:" << u; });
    engine.loadFromModule("ShelfRemote", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    // Attempt to silently restore the most recently used server's session. On
    // failure (no/expired tokens) the auth gate shows the login screen.
    const auto savedServers = Database::instance().servers();
    if (!savedServers.isEmpty()) {
        const auto &row = savedServers.first();
        auth->restoreSession(QUrl(row.baseUrl), row.id);
    }

    return app.exec();
}
