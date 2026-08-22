#include "player/MpvController.h"

#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <clocale>

namespace {

bool writeSilentWav(const QString &path, int sampleRate, int channels, int milliseconds)
{
    const quint32 frames = static_cast<quint32>(sampleRate * milliseconds / 1000);
    const quint16 bitsPerSample = 16;
    const quint16 blockAlign = static_cast<quint16>(channels * bitsPerSample / 8);
    const quint32 dataSize = frames * blockAlign;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataSize);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(channels) << quint32(sampleRate)
        << quint32(sampleRate * blockAlign) << blockAlign << bitsPerSample;
    out.writeRawData("data", 4);
    out << dataSize;

    const QByteArray silence(static_cast<qsizetype>(dataSize), '\0');
    return out.writeRawData(silence.constData(), silence.size()) == silence.size();
}

} // namespace

class TestMpvController : public QObject
{
    Q_OBJECT

private slots:
    void keepsAudioOutputAcrossDifferingBookTracks()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString mono441 = dir.filePath(QStringLiteral("mono-44100.wav"));
        const QString stereo480 = dir.filePath(QStringLiteral("stereo-48000.wav"));
        QVERIFY(writeSilentWav(mono441, 44100, 1, 700));
        // Keep the second track running long enough to inspect the AO after the
        // boundary without racing the normal final-EOF teardown.
        QVERIFY(writeSilentWav(stereo480, 48000, 2, 2000));

        // Q(Core)Application adopts the environment locale, while libmpv requires
        // the numeric C locale (the production main() performs the same reset).
        std::setlocale(LC_NUMERIC, "C");
        MpvController player;
        QVERIFY(player.init());
        // Force mpv's deterministic null AO so this test never touches the host's
        // real PipeWire/PulseAudio graph.
        player.setAudioDevice(QStringLiteral("null"));

        int reconfigurations = 0;
        int eofCount = 0;
        int fileLoadedCount = 0;
        int reconfigsAtFirstEof = -1;
        qint64 sampleRateAtFirstEof = 0;
        qint64 channelCountAtFirstEof = 0;
        connect(&player, &MpvController::audioOutputReconfigured, this, [&]() {
            ++reconfigurations;
        });
        connect(&player, &MpvController::endOfFile, this, [&]() {
            ++eofCount;
            if (eofCount == 1) {
                reconfigsAtFirstEof = reconfigurations;
                sampleRateAtFirstEof = player.audioOutputSampleRate();
                channelCountAtFirstEof = player.audioOutputChannelCount();
            }
        });
        connect(&player, &MpvController::fileLoaded, this, [&]() {
            ++fileLoadedCount;
        });

        player.loadPlaylist(
            {QUrl::fromLocalFile(mono441).toString(),
             QUrl::fromLocalFile(stereo480).toString()},
            QString());

        QTRY_COMPARE_WITH_TIMEOUT(eofCount, 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(fileLoadedCount, 2, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(player.isPlaying(), 1000);
        QTest::qWait(150); // process any transition-related AO notifications
        QVERIFY(reconfigsAtFirstEof > 0); // the AO was opened for the first track
        QCOMPARE(sampleRateAtFirstEof, 48000);
        QCOMPARE(channelCountAtFirstEof, 2);
        QCOMPARE(player.audioOutputSampleRate(), 48000);
        QCOMPARE(player.audioOutputChannelCount(), 2);
        QCOMPARE(reconfigurations, reconfigsAtFirstEof);
        QCOMPARE(eofCount, 1); // second track is still playing
    }
};

QTEST_GUILESS_MAIN(TestMpvController)
#include "tst_mpvcontroller.moc"
