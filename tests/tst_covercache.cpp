#include <QTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "app/AppConfig.h"
#include "net/ApiClient.h"
#include "storage/CoverCache.h"

// Nothing else bounds the on-disk cover directory: every distinct item, author,
// and requested size ever displayed leaves a file behind. These tests pin the
// eviction contract that keeps it from growing without limit.
class TstCoverCache : public QObject
{
    Q_OBJECT

    // Writes `bytes` of filler and stamps the file with an explicit modification
    // time, so eviction order is deterministic rather than dependent on how fast
    // the test happens to run.
    static void writeCover(const QString &name, int bytes, const QDateTime &modified)
    {
        QFile f(AppConfig::coverCacheDir() + QLatin1Char('/') + name);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(f.write(QByteArray(bytes, 'x')), qint64(bytes));
        QVERIFY(f.setFileTime(modified, QFileDevice::FileModificationTime));
        f.close();
    }

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        QDir dir(AppConfig::coverCacheDir());
        const auto stale = dir.entryList(QDir::Files);
        for (const QString &e : stale)
            dir.remove(e);
    }

    void evictsOldestUntilUnderTheLimit()
    {
        const QDateTime base = QDateTime::currentDateTime();
        writeCover(QStringLiteral("oldest.webp"), 1000, base.addSecs(-300));
        writeCover(QStringLiteral("middle.webp"), 1000, base.addSecs(-200));
        writeCover(QStringLiteral("newest.webp"), 1000, base.addSecs(-100));

        ApiClient api;
        CoverCache covers(&api);
        QCOMPARE(covers.cacheSizeBytes(), qint64(3000));

        // Room for two files: the oldest one must go, and only that one.
        covers.pruneToLimit(2500);

        const QString dir = AppConfig::coverCacheDir() + QLatin1Char('/');
        QVERIFY(!QFile::exists(dir + QStringLiteral("oldest.webp")));
        QVERIFY(QFile::exists(dir + QStringLiteral("middle.webp")));
        QVERIFY(QFile::exists(dir + QStringLiteral("newest.webp")));
        QCOMPARE(covers.cacheSizeBytes(), qint64(2000));
    }

    void keepsEverythingWhenAlreadyUnderTheLimit()
    {
        writeCover(QStringLiteral("a.webp"), 500, QDateTime::currentDateTime());

        ApiClient api;
        CoverCache covers(&api);
        covers.pruneToLimit(CoverCache::kDefaultMaxCacheBytes);

        QCOMPARE(covers.cacheSizeBytes(), qint64(500));
    }

    void aZeroLimitEmptiesTheCache()
    {
        writeCover(QStringLiteral("a.webp"), 500, QDateTime::currentDateTime());
        writeCover(QStringLiteral("b.webp"), 500, QDateTime::currentDateTime());

        ApiClient api;
        CoverCache covers(&api);
        covers.pruneToLimit(0);

        QCOMPARE(covers.cacheSizeBytes(), qint64(0));
    }
};

QTEST_GUILESS_MAIN(TstCoverCache)
#include "tst_covercache.moc"
