#include "player/MpvController.h"

#include <QByteArray>
#include <QList>
#include <QMetaObject>
#include <QStringList>
#include <QVariantMap>

#include <mpv/client.h>

namespace {
// Property observation ids.
constexpr uint64_t kTimePos = 1;
constexpr uint64_t kDuration = 2;
constexpr uint64_t kPause = 3;
constexpr uint64_t kCoreIdle = 4;
constexpr uint64_t kPausedForCache = 5;
} // namespace

MpvController::MpvController(QObject *parent)
    : QObject(parent)
{
}

MpvController::~MpvController()
{
    if (m_mpv) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

bool MpvController::init()
{
    m_mpv = mpv_create();
    if (!m_mpv)
        return false;

    // Windowless audio-only player.
    mpv_set_option_string(m_mpv, "vid", "no");
    mpv_set_option_string(m_mpv, "audio-display", "no");
    mpv_set_option_string(m_mpv, "idle", "yes");
    mpv_set_option_string(m_mpv, "force-seekable", "yes");
    mpv_set_option_string(m_mpv, "cache", "yes");
    mpv_set_option_string(m_mpv, "user-agent", "ShelfRemote");

    if (mpv_initialize(m_mpv) < 0) {
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return false;
    }

    mpv_observe_property(m_mpv, kTimePos, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, kDuration, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, kPause, "pause", MPV_FORMAT_FLAG);
    // core-idle covers idle/seeking; paused-for-cache covers stall-buffering. Both
    // must read false (along with pause) for playback to count as truly active, so
    // buffering time is not billed as listening time.
    mpv_observe_property(m_mpv, kCoreIdle, "core-idle", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, kPausedForCache, "paused-for-cache", MPV_FORMAT_FLAG);

    // Deliver events to the Qt event loop.
    mpv_set_wakeup_callback(m_mpv, &MpvController::onWakeup, this);
    return true;
}

void MpvController::onWakeup(void *ctx)
{
    auto *self = static_cast<MpvController *>(ctx);
    // Marshal onto the object's thread; libmpv calls this from its own thread.
    QMetaObject::invokeMethod(self, [self]() { self->handleEvents(); },
                              Qt::QueuedConnection);
}

void MpvController::handleEvents()
{
    if (!m_mpv)
        return;
    while (true) {
        mpv_event *ev = mpv_wait_event(m_mpv, 0);
        if (ev->event_id == MPV_EVENT_NONE)
            break;
        switch (ev->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE: {
            auto *prop = static_cast<mpv_event_property *>(ev->data);
            if (ev->reply_userdata == kTimePos && prop->format == MPV_FORMAT_DOUBLE) {
                m_position = *static_cast<double *>(prop->data);
                emit positionChanged(m_position);
            } else if (ev->reply_userdata == kDuration && prop->format == MPV_FORMAT_DOUBLE) {
                m_duration = *static_cast<double *>(prop->data);
                emit durationChanged(m_duration);
            } else if (ev->reply_userdata == kPause && prop->format == MPV_FORMAT_FLAG) {
                m_paused = *static_cast<int *>(prop->data) != 0;
                updatePlaying();
            } else if (ev->reply_userdata == kCoreIdle && prop->format == MPV_FORMAT_FLAG) {
                m_coreIdle = *static_cast<int *>(prop->data) != 0;
                updatePlaying();
            } else if (ev->reply_userdata == kPausedForCache && prop->format == MPV_FORMAT_FLAG) {
                m_pausedForCache = *static_cast<int *>(prop->data) != 0;
                updatePlaying();
            }
            break;
        }
        case MPV_EVENT_FILE_LOADED:
            if (m_pendingSeek > 0.0) {
                seekAbsolute(m_pendingSeek);
                m_pendingSeek = 0.0;
            }
            emit fileLoaded();
            break;
        case MPV_EVENT_END_FILE: {
            auto *ef = static_cast<mpv_event_end_file *>(ev->data);
            if (ef->reason == MPV_END_FILE_REASON_EOF) {
                emit endOfFile();
            } else if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                // A stream failed to load or died mid-playback: surface it instead
                // of silently leaving the session "active" with no audio.
                emit mpvError(QString::fromUtf8(mpv_error_string(ef->error)));
            }
            // Playback stopped either way; refresh the effective-playing state.
            updatePlaying();
            break;
        }
        default:
            break;
        }
    }
}

void MpvController::updatePlaying()
{
    const bool effective = !m_paused && !m_coreIdle && !m_pausedForCache;
    if (effective == m_playing)
        return;
    m_playing = effective;
    emit playingChanged(m_playing);
}

void MpvController::setProperty(const QString &name, const QVariant &value)
{
    if (!m_mpv)
        return;
    const QByteArray n = name.toUtf8();
    if (value.typeId() == QMetaType::Double) {
        double d = value.toDouble();
        mpv_set_property(m_mpv, n.constData(), MPV_FORMAT_DOUBLE, &d);
    } else if (value.typeId() == QMetaType::Bool) {
        int flag = value.toBool() ? 1 : 0;
        mpv_set_property(m_mpv, n.constData(), MPV_FORMAT_FLAG, &flag);
    } else {
        const QByteArray s = value.toString().toUtf8();
        const char *cs = s.constData();
        mpv_set_property(m_mpv, n.constData(), MPV_FORMAT_STRING, &cs);
    }
}

void MpvController::command(const QStringList &args)
{
    if (!m_mpv)
        return;
    QVector<QByteArray> bytes;
    bytes.reserve(args.size());
    for (const QString &a : args)
        bytes.push_back(a.toUtf8());
    QVector<const char *> argv;
    argv.reserve(bytes.size() + 1);
    for (const QByteArray &b : bytes)
        argv.push_back(b.constData());
    argv.push_back(nullptr);
    mpv_command(m_mpv, argv.data());
}

void MpvController::load(const QString &url, const QString &authHeader, double startSeconds)
{
    if (!m_mpv)
        return;
    // Inject the Authorization header for this stream. Cleared first so a public
    // transcode URL is not sent stale headers. This is a runtime-changeable
    // property, so it must be set via set_property (not set_option) post-init.
    mpv_set_property_string(m_mpv, "http-header-fields",
                            authHeader.isEmpty() ? "" : authHeader.toUtf8().constData());

    m_pendingSeek = startSeconds;
    command({QStringLiteral("loadfile"), url, QStringLiteral("replace")});
    play();
}

void MpvController::play()  { setProperty(QStringLiteral("pause"), false); }
void MpvController::pause() { setProperty(QStringLiteral("pause"), true); }
void MpvController::stop()  { command({QStringLiteral("stop")}); }

void MpvController::seekAbsolute(double seconds)
{
    command({QStringLiteral("seek"), QString::number(seconds), QStringLiteral("absolute")});
}

void MpvController::seekRelative(double deltaSeconds)
{
    command({QStringLiteral("seek"), QString::number(deltaSeconds), QStringLiteral("relative")});
}

void MpvController::setSpeed(double speed)
{
    m_speed = speed;
    setProperty(QStringLiteral("speed"), speed);
}

void MpvController::setVolume(double percent)
{
    m_volume = percent;
    setProperty(QStringLiteral("volume"), percent);
}

QVariantList MpvController::audioDevices() const
{
    QVariantList out;
    // Always offer the implicit auto-select entry first so the picker has a sane
    // "let mpv decide" option even before any AO has been probed.
    {
        QVariantMap autoEntry;
        autoEntry.insert(QStringLiteral("name"), QStringLiteral("auto"));
        autoEntry.insert(QStringLiteral("description"), QStringLiteral("Autoselect (default)"));
        out.push_back(autoEntry);
    }
    if (!m_mpv)
        return out;

    mpv_node node;
    if (mpv_get_property(m_mpv, "audio-device-list", MPV_FORMAT_NODE, &node) < 0)
        return out;
    if (node.format == MPV_FORMAT_NODE_ARRAY) {
        for (int i = 0; i < node.u.list->num; ++i) {
            const mpv_node &entry = node.u.list->values[i];
            if (entry.format != MPV_FORMAT_NODE_MAP)
                continue;
            QString name;
            QString desc;
            for (int j = 0; j < entry.u.list->num; ++j) {
                const QString k = QString::fromUtf8(entry.u.list->keys[j]);
                const mpv_node &val = entry.u.list->values[j];
                if (val.format != MPV_FORMAT_STRING)
                    continue;
                const QString s = QString::fromUtf8(val.u.string);
                if (k == QLatin1String("name"))
                    name = s;
                else if (k == QLatin1String("description"))
                    desc = s;
            }
            if (name.isEmpty() || name == QLatin1String("auto"))
                continue; // 'auto' already added above with a friendly label
            QVariantMap a;
            a.insert(QStringLiteral("name"), name);
            a.insert(QStringLiteral("description"), desc.isEmpty() ? name : desc);
            out.push_back(a);
        }
    }
    mpv_free_node_contents(&node);
    return out;
}

void MpvController::setAudioDevice(const QString &name)
{
    m_audioDevice = name.isEmpty() ? QStringLiteral("auto") : name;
    if (!m_mpv)
        return;
    const QByteArray n = m_audioDevice.toUtf8();
    const char *cs = n.constData();
    mpv_set_property(m_mpv, "audio-device", MPV_FORMAT_STRING, &cs);
}
