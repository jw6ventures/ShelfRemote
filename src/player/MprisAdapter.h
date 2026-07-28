#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class PlaybackSession;
class MprisPlayerAdaptor;

// Exposes the running PlaybackSession over the standard MPRIS2 D-Bus interface so
// media keys, desktop widgets, and Bluetooth controls work even when the app is
// unfocused. The Flatpak sandbox permits owning
// org.mpris.MediaPlayer2.<app-id> without full session-bus access.
class MprisAdapter : public QObject
{
    Q_OBJECT
public:
    explicit MprisAdapter(PlaybackSession *session, QObject *parent = nullptr);

    // Registers the service + /org/mpris/MediaPlayer2 object. Returns success.
    bool registerOnBus();

    PlaybackSession *session() const { return m_session; }

    // Emits org.freedesktop.DBus.Properties.PropertiesChanged for the Player
    // interface with the given changed properties.
    void notify(const QVariantMap &changed);

private:
    PlaybackSession    *m_session;
    MprisPlayerAdaptor *m_player = nullptr;
};

// --- org.mpris.MediaPlayer2 (root) ------------------------------------------
class MprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)
public:
    explicit MprisRootAdaptor(QObject *parent) : QDBusAbstractAdaptor(parent) {}
    bool canQuit() const { return true; }
    // No reliable programmatic window-raise in this sandboxed 10-foot shell, so we
    // advertise the capability honestly rather than exporting a no-op Raise().
    bool canRaise() const { return false; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("ShelfRemote"); }
    QString desktopEntry() const;
    QStringList supportedUriSchemes() const { return {}; }
    QStringList supportedMimeTypes() const { return {}; }
public slots:
    void Raise();
    void Quit();
};

// --- org.mpris.MediaPlayer2.Player ------------------------------------------
class MprisPlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(bool CanGoNext READ canControl)
    Q_PROPERTY(bool CanGoPrevious READ canControl)
    Q_PROPERTY(bool CanPlay READ canControl)
    Q_PROPERTY(bool CanPause READ canControl)
    Q_PROPERTY(bool CanSeek READ canControl)
    Q_PROPERTY(bool CanControl READ canControl)
public:
    MprisPlayerAdaptor(MprisAdapter *owner, PlaybackSession *session);

    QString playbackStatus() const;
    double rate() const;
    void setRate(double r);
    double minimumRate() const { return 0.5; }
    double maximumRate() const { return 3.0; }
    double volume() const;
    void setVolume(double v);
    qlonglong position() const;         // microseconds
    QVariantMap metadata() const;
    bool canControl() const { return true; }

public slots:
    void Play();
    void Pause();
    void PlayPause();
    void Stop();
    void Next();
    void Previous();
    void Seek(qlonglong offsetMicroseconds);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong positionMicroseconds);

signals:
    void Seeked(qlonglong positionMicroseconds);

private:
    MprisAdapter    *m_owner;
    PlaybackSession *m_session;
};
