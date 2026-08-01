#include <QTest>

#include <QJsonArray>
#include <QJsonObject>

#include "model/PlaylistModel.h"

class TstPlaylistModel : public QObject
{
    Q_OBJECT

private slots:
    void bookEntry()
    {
        PlaylistModel model;
        const QJsonObject libraryItem{
            {"id", "book-1"},
            {"media", QJsonObject{{"metadata", QJsonObject{
                {"title", "A Book"}, {"authorName", "An Author"}}}}},
        };
        model.setPlaylists(QJsonArray{QJsonObject{
            {"id", "pl-1"},
            {"name", "Favorites"},
            {"description", "The good ones"},
            {"items", QJsonArray{QJsonObject{
                {"libraryItemId", "book-1"},
                {"libraryItem", libraryItem},
            }}},
        }});

        QCOMPARE(model.rowCount(), 1);
        const QModelIndex row = model.index(0, 0);
        QCOMPARE(model.data(row, PlaylistModel::PlaylistIdRole).toString(),
                 QStringLiteral("pl-1"));
        QCOMPARE(model.data(row, PlaylistModel::NameRole).toString(),
                 QStringLiteral("Favorites"));
        QCOMPARE(model.data(row, PlaylistModel::ItemCountRole).toInt(), 1);

        const QVariantMap entry =
            model.data(row, PlaylistModel::EntriesRole).toList().first().toMap();
        QCOMPARE(entry.value("kind").toString(), QStringLiteral("book"));
        QCOMPARE(entry.value("itemId").toString(), QStringLiteral("book-1"));
        QCOMPARE(entry.value("episodeId").toString(), QString());
        QCOMPARE(entry.value("title").toString(), QStringLiteral("A Book"));
        QCOMPARE(entry.value("author").toString(), QStringLiteral("An Author"));
        QCOMPARE(entry.value("coverId").toString(), QStringLiteral("book-1"));
    }

    void podcastEpisodeEntry()
    {
        PlaylistModel model;
        const QJsonObject podcast{
            {"id", "pod-1"},
            {"media", QJsonObject{{"metadata", QJsonObject{
                {"title", "The Podcast"}}}}},
        };
        model.setPlaylists(QJsonArray{QJsonObject{
            {"id", "pl-2"},
            {"name", "Episode queue"},
            {"items", QJsonArray{QJsonObject{
                {"libraryItemId", "pod-1"},
                {"libraryItem", podcast},
                {"episodeId", "ep-7"},
                {"episode", QJsonObject{{"id", "ep-7"}, {"title", "Episode Seven"}}},
            }}},
        }});

        const QVariantMap entry =
            model.data(model.index(0, 0), PlaylistModel::EntriesRole)
                .toList().first().toMap();
        QCOMPARE(entry.value("kind").toString(), QStringLiteral("episode"));
        QCOMPARE(entry.value("itemId").toString(), QStringLiteral("pod-1"));
        QCOMPARE(entry.value("episodeId").toString(), QStringLiteral("ep-7"));
        QCOMPARE(entry.value("title").toString(), QStringLiteral("Episode Seven"));
        QCOMPARE(entry.value("author").toString(), QStringLiteral("The Podcast"));
    }

    void resetDropsOldPlaylists()
    {
        PlaylistModel model;
        QSignalSpy countChanged(&model, &PlaylistModel::countChanged);
        model.setPlaylists(QJsonArray{
            QJsonObject{{"id", "one"}, {"name", "One"}},
            QJsonObject{{"id", "two"}, {"name", "Two"}},
        });
        QCOMPARE(model.rowCount(), 2);

        model.setPlaylists(QJsonArray());
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(countChanged.count(), 2);
    }
};

QTEST_GUILESS_MAIN(TstPlaylistModel)
#include "tst_playlistmodel.moc"
