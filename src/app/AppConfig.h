#pragma once

#include <QString>

// Central access to build-time application identity and per-user data paths.
// The macros come from the top-level CMakeLists so there is a single source of
// truth for the app-id, custom URI scheme, and OIDC client_id.
namespace AppConfig {

inline QString appId()      { return QStringLiteral(SHELFREMOTE_APP_ID); }
inline QString uriScheme()  { return QStringLiteral(SHELFREMOTE_URI_SCHEME); }
inline QString clientId()   { return QStringLiteral(SHELFREMOTE_CLIENT_ID); }
inline QString version()    { return QStringLiteral(SHELFREMOTE_VERSION); }

// jw6-shelfremote://oauth
inline QString oauthRedirectUri() { return uriScheme() + QStringLiteral("://oauth"); }

// org.mpris.MediaPlayer2.us.jw6.ShelfRemote
inline QString mprisServiceName() {
    return QStringLiteral("org.mpris.MediaPlayer2.") + appId();
}

// Absolute path to the app data directory (XDG_DATA_HOME/us.jw6.ShelfRemote),
// created on demand. Holds the SQLite database and cover cache.
QString dataDir();
QString cacheDir();
QString databasePath();
QString coverCacheDir();

// Absolute path for the single-instance local socket. Flatpak instances have
// private /tmp directories, so the socket lives in their shared per-app runtime
// directory instead of relying on QLocalServer's default /tmp placement.
QString instanceSocketPath();

// Absolute path to the rotating debug log file (under the cache dir). The parent
// directory is created on demand.
QString logFilePath();

// Stable per-installation device id used for Audiobookshelf playback sessions.
// Generated once and persisted; identical across launches.
QString deviceId();

} // namespace AppConfig
