#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

class ApiClient;

// Per-user audiobook bookmarks, keyed by libraryItemId. Audiobookshelf stores
// bookmarks on the user object (like mediaProgress), so we load them once from the
// /api/authorize payload and mutate them through POST/DELETE, keeping a local cache
// in sync optimistically so the UI updates without a round-trip. Bookmark times are
// integer seconds (the server keys DELETE by :time as an integer).
class BookmarkStore : public QObject
{
    Q_OBJECT
public:
    explicit BookmarkStore(ApiClient *api, QObject *parent = nullptr);

    // Populate from a user object that contains a "bookmarks" array.
    void loadFromUser(const QJsonObject &user);

    // Drop all cached bookmarks (on logout / server change).
    void clear();

    // {time (seconds), title} for an item, sorted ascending by time.
    Q_INVOKABLE QVariantList forItem(const QString &itemId) const;
    // True if a bookmark already exists at floor(time) seconds for this item.
    Q_INVOKABLE bool has(const QString &itemId, double time) const;

    // Create a bookmark at `time` (seconds). Inserts locally + emits changed()
    // immediately; rolls the insert back if the server rejects it.
    Q_INVOKABLE void add(const QString &itemId, double time, const QString &title);
    // Delete the bookmark at `time`. Removes locally + emits changed(); restores it
    // if the server rejects the delete.
    Q_INVOKABLE void remove(const QString &itemId, double time);

signals:
    void changed();

private:
    struct Bookmark {
        double  time = 0.0; // always integral (server stores integer seconds)
        QString title;
    };
    // Bookmarks dedupe / match by integer second, matching Endpoints::bookmarkAt.
    static qint64 keyOf(double time) { return qint64(time); }

    ApiClient *m_api;
    QHash<QString, QVector<Bookmark>> m_byItem;
};
