#include "app/AppSettings.h"
#include "storage/Database.h"

namespace {
constexpr int kSkipDefault = 30;
constexpr double kRateDefault = 1.0;
} // namespace

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
    m_skipSeconds = Database::instance()
                        .getSetting(QStringLiteral("skipSeconds"),
                                    QString::number(kSkipDefault))
                        .toInt();
    if (m_skipSeconds <= 0)
        m_skipSeconds = kSkipDefault;

    m_defaultRate = Database::instance()
                        .getSetting(QStringLiteral("defaultRate"),
                                    QString::number(kRateDefault))
                        .toDouble();
    if (m_defaultRate <= 0.0)
        m_defaultRate = kRateDefault;

    m_audioDevice = Database::instance()
                        .getSetting(QStringLiteral("audioDevice"), QStringLiteral("auto"));
    if (m_audioDevice.isEmpty())
        m_audioDevice = QStringLiteral("auto");
}

void AppSettings::setSkipSeconds(int seconds)
{
    if (seconds <= 0 || seconds == m_skipSeconds)
        return;
    m_skipSeconds = seconds;
    Database::instance().putSetting(QStringLiteral("skipSeconds"), QString::number(seconds));
    emit skipSecondsChanged();
}

void AppSettings::setDefaultRate(double rate)
{
    if (rate <= 0.0 || qFuzzyCompare(rate, m_defaultRate))
        return;
    m_defaultRate = rate;
    Database::instance().putSetting(QStringLiteral("defaultRate"), QString::number(rate));
    emit defaultRateChanged();
}

double AppSettings::nextRate(double current) const
{
    static constexpr double kSteps[] = {0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 2.25, 2.5, 2.75, 3.0};
    for (const double step : kSteps)
        if (step > current + 0.001)
            return step;
    return kSteps[0];
}

void AppSettings::setAudioDevice(const QString &name)
{
    const QString n = name.isEmpty() ? QStringLiteral("auto") : name;
    if (n == m_audioDevice)
        return;
    m_audioDevice = n;
    Database::instance().putSetting(QStringLiteral("audioDevice"), n);
    emit audioDeviceChanged();
}
