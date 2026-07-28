#pragma once

#include <QObject>
#include <QString>

// User preferences that outlive a single run, persisted as key/value rows in the
// database. Exposed to QML so the Settings screen can read/write them and other
// screens (e.g. TransportBar's skip amount) can bind to them live. The default
// playback rate is also read back on the C++ side by PlaybackSession when a new
// session starts (via the shared "defaultRate" setting key).
class AppSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int skipSeconds READ skipSeconds WRITE setSkipSeconds NOTIFY skipSecondsChanged)
    Q_PROPERTY(double defaultRate READ defaultRate WRITE setDefaultRate NOTIFY defaultRateChanged)
    Q_PROPERTY(QString audioDevice READ audioDevice WRITE setAudioDevice NOTIFY audioDeviceChanged)

public:
    explicit AppSettings(QObject *parent = nullptr);

    int skipSeconds() const { return m_skipSeconds; }
    void setSkipSeconds(int seconds);

    double defaultRate() const { return m_defaultRate; }
    void setDefaultRate(double rate);

    // mpv audio-device name ("auto" == let mpv choose). Applied to the player by
    // main.cpp on startup and whenever changed from the Settings screen.
    QString audioDevice() const { return m_audioDevice; }
    void setAudioDevice(const QString &name);

signals:
    void skipSecondsChanged();
    void defaultRateChanged();
    void audioDeviceChanged();

private:
    int     m_skipSeconds = 30;
    double  m_defaultRate = 1.0;
    QString m_audioDevice = QStringLiteral("auto");
};
