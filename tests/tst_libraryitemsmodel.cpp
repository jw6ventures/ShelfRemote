#include <QTest>

#include <QJsonArray>
#include <QJsonObject>

#include "model/LibraryItemsModel.h"

// setItems() is reused for both shelf contents and search results, which may be
// either nested ABS libraryItems or already-flattened rows. These tests pin that
// dual-shape normalisation and the role exposure the MediaGrid depends on.
class TstLibraryItemsModel : public QObject
{
    Q_OBJECT

    static QString role(const LibraryItemsModel &m, int row, int r)
    {
        return m.data(m.index(row, 0), r).toString();
    }

private slots:
    void normalisesNestedLibraryItem()
    {
        LibraryItemsModel m;
        QJsonObject nested{
            {"id", "x"}, {"mediaType", "book"},
            {"media", QJsonObject{
                {"duration", 3600},
                {"metadata", QJsonObject{
                    {"title", "T"}, {"authorName", "A"}, {"subtitle", "S"}}}}},
            {"userMediaProgress", QJsonObject{{"progress", 0.5}, {"isFinished", false}}}};
        m.setItems(QJsonArray{nested});

        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(role(m, 0, LibraryItemsModel::ItemIdRole), QStringLiteral("x"));
        QCOMPARE(role(m, 0, LibraryItemsModel::TitleRole), QStringLiteral("T"));
        QCOMPARE(role(m, 0, LibraryItemsModel::AuthorRole), QStringLiteral("A"));
        QCOMPARE(role(m, 0, LibraryItemsModel::SubtitleRole), QStringLiteral("S"));
        QCOMPARE(m.data(m.index(0, 0), LibraryItemsModel::DurationRole).toDouble(), 3600.0);
        QCOMPARE(m.data(m.index(0, 0), LibraryItemsModel::ProgressRole).toDouble(), 0.5);
    }

    void passesFlatRowThrough()
    {
        LibraryItemsModel m;
        // A row that is already flat (no "media") must be stored verbatim.
        QJsonObject flat{{"id", "y"}, {"title", "FT"}, {"author", "FA"}};
        m.setItems(QJsonArray{flat});

        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(role(m, 0, LibraryItemsModel::ItemIdRole), QStringLiteral("y"));
        QCOMPARE(role(m, 0, LibraryItemsModel::TitleRole), QStringLiteral("FT"));
        QCOMPARE(role(m, 0, LibraryItemsModel::AuthorRole), QStringLiteral("FA"));
    }

    void setItemsResetsAndClearsPagination()
    {
        LibraryItemsModel m;
        m.setItems(QJsonArray{QJsonObject{{"id", "a"}}, QJsonObject{{"id", "b"}}});
        QCOMPARE(m.rowCount(), 2);
        // A directly-populated model is complete: no server pagination follows.
        QVERIFY(!m.hasMore());
        // A subsequent setItems fully replaces the prior content.
        m.setItems(QJsonArray{QJsonObject{{"id", "c"}}});
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(role(m, 0, LibraryItemsModel::ItemIdRole), QStringLiteral("c"));
    }
};

QTEST_MAIN(TstLibraryItemsModel)
#include "tst_libraryitemsmodel.moc"
