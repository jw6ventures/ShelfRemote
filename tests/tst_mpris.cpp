#include <QTest>

#include <QDBusObjectPath>
#include <QSignalSpy>

#include "net/ApiClient.h"
#include "player/MprisAdapter.h"
#include "player/MpvController.h"
#include "player/PlaybackSession.h"

// MPRIS metadata has to marshal over D-Bus, which is unforgiving about object
// paths. These tests need no bus: they check the values that would be sent.
class TstMpris : public QObject
{
    Q_OBJECT

private slots:
    // Audiobookshelf ids are UUIDs. A hyphen is not legal in an object path, so
    // using the id verbatim produced a path QDBusObjectPath silently discarded,
    // and with it every Metadata update.
    void trackIdIsAValidObjectPathForUuidItems()
    {
        ApiClient api;
        MpvController mpv;
        PlaybackSession session(&api, &mpv);
        MprisAdapter mpris(&session);
        auto *player = mpris.findChild<MprisPlayerAdaptor *>();
        QVERIFY(player);

        session.playItem(QStringLiteral("8c4d1e7c-1f2a-4b3c-9d8e-0123456789ab"));
        const auto trackId = player->metadata()
                                 .value(QStringLiteral("mpris:trackid"))
                                 .value<QDBusObjectPath>();
        QVERIFY(!trackId.path().isEmpty());
        QVERIFY(trackId.path().startsWith(QLatin1Char('/')));
        for (const QString &element : trackId.path().mid(1).split(QLatin1Char('/'))) {
            QVERIFY(!element.isEmpty());
            for (const QChar c : element)
                QVERIFY2(c.isLetterOrNumber() || c == QLatin1Char('_'),
                         qPrintable(trackId.path()));
        }
    }

    // Two episodes of one podcast share the item id; they are different tracks.
    void episodesOfOneShowAreDistinctTracks()
    {
        ApiClient api;
        MpvController mpv;
        PlaybackSession session(&api, &mpv);
        MprisAdapter mpris(&session);
        auto *player = mpris.findChild<MprisPlayerAdaptor *>();
        QVERIFY(player);

        auto trackOf = [&](const QString &episode) {
            session.playEpisode(QStringLiteral("show-1"), episode);
            return player->metadata().value(QStringLiteral("mpris:trackid"))
                .value<QDBusObjectPath>().path();
        };
        const QString first = trackOf(QStringLiteral("ep-1"));
        const QString second = trackOf(QStringLiteral("ep-2"));
        QVERIFY(!first.isEmpty());
        QVERIFY(first != second);
    }

    // Nothing to play, pause or seek without a session.
    void actionsAreOnlyOfferedWithASession()
    {
        ApiClient api;
        MpvController mpv;
        PlaybackSession session(&api, &mpv);
        MprisAdapter mpris(&session);
        auto *player = mpris.findChild<MprisPlayerAdaptor *>();
        QVERIFY(player);
        QVERIFY(!player->canAct());
        QVERIFY(player->canControl());
        QCOMPARE(player->playbackStatus(), QStringLiteral("Stopped"));
    }
};

QTEST_GUILESS_MAIN(TstMpris)
#include "tst_mpris.moc"
