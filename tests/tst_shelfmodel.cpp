#include <QTest>

#include <QJsonArray>
#include <QJsonObject>

#include "model/ShelfModel.h"

// Personalized Home shelves are heterogeneous: book/podcast library items, series,
// authors, and recent episodes all arrive through the same endpoint. These tests
// pin the per-type unpacking that decides each card's cover source and how an
// activation is routed (a series must not open /api/items/:seriesId, an episode
// card must target the episode, not the enclosing podcast).
class TstShelfModel : public QObject
{
    Q_OBJECT

    static QVariantList shelfItems(ShelfModel &m, int row)
    {
        return m.data(m.index(row, 0), ShelfModel::ItemsRole).toList();
    }

private slots:
    void bookEntity()
    {
        ShelfModel m;
        QJsonObject book{
            {"id", "li1"}, {"mediaType", "book"},
            {"media", QJsonObject{{"metadata",
                QJsonObject{{"title", "The Title"}, {"authorName", "The Author"}}}}}};
        m.setShelves(QJsonArray{QJsonObject{
            {"label", "Recently Added"}, {"type", "book"}, {"entities", QJsonArray{book}}}});

        const QVariantMap e = shelfItems(m, 0).first().toMap();
        QCOMPARE(e.value("kind").toString(), QStringLiteral("book"));
        QCOMPARE(e.value("itemId").toString(), QStringLiteral("li1"));
        QCOMPARE(e.value("coverId").toString(), QStringLiteral("li1"));
        QCOMPARE(e.value("coverKind").toString(), QStringLiteral("item"));
        QCOMPARE(e.value("title").toString(), QStringLiteral("The Title"));
        QCOMPARE(e.value("author").toString(), QStringLiteral("The Author"));
        QCOMPARE(e.value("episodeId").toString(), QString());
    }

    void seriesEntity()
    {
        ShelfModel m;
        QJsonObject series{
            {"id", "ser1"}, {"name", "The Series"},
            {"books", QJsonArray{QJsonObject{{"id", "book-a"}}, QJsonObject{{"id", "book-b"}}}}};
        m.setShelves(QJsonArray{QJsonObject{
            {"label", "Continue Series"}, {"type", "series"}, {"entities", QJsonArray{series}}}});

        const QVariantMap e = shelfItems(m, 0).first().toMap();
        QCOMPARE(e.value("kind").toString(), QStringLiteral("series"));
        // Navigation is by series id (routed to a filtered library, not an item URL).
        QCOMPARE(e.value("itemId").toString(), QStringLiteral("ser1"));
        // The cover, however, comes from the first book (a series has no cover).
        QCOMPARE(e.value("coverId").toString(), QStringLiteral("book-a"));
        QCOMPARE(e.value("coverKind").toString(), QStringLiteral("item"));
        QCOMPARE(e.value("title").toString(), QStringLiteral("The Series"));
    }

    void authorEntity()
    {
        ShelfModel m;
        QJsonObject author{{"id", "au1"}, {"name", "Jane Doe"}};
        m.setShelves(QJsonArray{QJsonObject{
            {"label", "Newest Authors"}, {"type", "authors"}, {"entities", QJsonArray{author}}}});

        const QVariantMap e = shelfItems(m, 0).first().toMap();
        QCOMPARE(e.value("kind").toString(), QStringLiteral("author"));
        QCOMPARE(e.value("itemId").toString(), QStringLiteral("au1"));
        // Author portraits come from a different endpoint than item covers.
        QCOMPARE(e.value("coverKind").toString(), QStringLiteral("author"));
        QCOMPARE(e.value("coverId").toString(), QStringLiteral("au1"));
        QCOMPARE(e.value("title").toString(), QStringLiteral("Jane Doe"));
    }

    void episodeEntity()
    {
        ShelfModel m;
        QJsonObject podcast{
            {"id", "pod1"}, {"mediaType", "podcast"},
            {"media", QJsonObject{{"metadata", QJsonObject{{"title", "My Podcast"}}}}},
            {"recentEpisode", QJsonObject{{"id", "ep9"}, {"title", "Episode Nine"}}}};
        m.setShelves(QJsonArray{QJsonObject{
            {"label", "Newest Episodes"}, {"type", "episode"}, {"entities", QJsonArray{podcast}}}});

        const QVariantMap e = shelfItems(m, 0).first().toMap();
        QCOMPARE(e.value("kind").toString(), QStringLiteral("episode"));
        // Play targets the podcast item + the specific episode.
        QCOMPARE(e.value("itemId").toString(), QStringLiteral("pod1"));
        QCOMPARE(e.value("episodeId").toString(), QStringLiteral("ep9"));
        QCOMPARE(e.value("coverId").toString(), QStringLiteral("pod1"));
        // The card shows the episode title, with the podcast name as the subtitle.
        QCOMPARE(e.value("title").toString(), QStringLiteral("Episode Nine"));
        QCOMPARE(e.value("author").toString(), QStringLiteral("My Podcast"));
    }

    // A recentEpisode on an entity marks it as an episode even if the shelf type
    // label differs across server versions.
    void recentEpisodeImpliesEpisode()
    {
        ShelfModel m;
        QJsonObject podcast{
            {"id", "pod2"}, {"mediaType", "podcast"},
            {"media", QJsonObject{{"metadata", QJsonObject{{"title", "Cast"}}}}},
            {"recentEpisode", QJsonObject{{"id", "ep1"}, {"title", "Ep 1"}}}};
        m.setShelves(QJsonArray{QJsonObject{
            {"label", "Podcasts"}, {"type", "podcast"}, {"entities", QJsonArray{podcast}}}});

        const QVariantMap e = shelfItems(m, 0).first().toMap();
        QCOMPARE(e.value("kind").toString(), QStringLiteral("episode"));
        QCOMPARE(e.value("episodeId").toString(), QStringLiteral("ep1"));
    }
};

QTEST_GUILESS_MAIN(TstShelfModel)
#include "tst_shelfmodel.moc"
