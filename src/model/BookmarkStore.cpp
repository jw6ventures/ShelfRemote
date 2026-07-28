#include "model/BookmarkStore.h"
#include "net/ApiClient.h"

#include <QJsonArray>
#include <QVariantMap>
#include <algorithm>

BookmarkStore::BookmarkStore(ApiClient *api, QObject *parent)
    : QObject(parent)
    , m_api(api)
{
}

void BookmarkStore::loadFromUser(const QJsonObject &user)
{
    m_byItem.clear();
    const QJsonArray arr = user.value(QStringLiteral("bookmarks")).toArray();
    for (const auto &v : arr) {
        const QJsonObject b = v.toObject();
        const QString itemId = b.value(QStringLiteral("libraryItemId")).toString();
        if (itemId.isEmpty())
            continue;
        Bookmark bm;
        bm.time = double(qint64(b.value(QStringLiteral("time")).toDouble()));
        bm.title = b.value(QStringLiteral("title")).toString();
        m_byItem[itemId].push_back(bm);
    }
    emit changed();
}

void BookmarkStore::clear()
{
    if (m_byItem.isEmpty())
        return;
    m_byItem.clear();
    emit changed();
}

QVariantList BookmarkStore::forItem(const QString &itemId) const
{
    QVariantList out;
    const auto it = m_byItem.constFind(itemId);
    if (it == m_byItem.constEnd())
        return out;
    QVector<Bookmark> marks = *it;
    std::sort(marks.begin(), marks.end(),
              [](const Bookmark &a, const Bookmark &b) { return a.time < b.time; });
    for (const Bookmark &b : marks) {
        QVariantMap m;
        m.insert(QStringLiteral("time"), b.time);
        m.insert(QStringLiteral("title"), b.title);
        out.push_back(m);
    }
    return out;
}

bool BookmarkStore::has(const QString &itemId, double time) const
{
    const auto it = m_byItem.constFind(itemId);
    if (it == m_byItem.constEnd())
        return false;
    const qint64 t = keyOf(time);
    for (const Bookmark &b : *it)
        if (keyOf(b.time) == t)
            return true;
    return false;
}

void BookmarkStore::add(const QString &itemId, double time, const QString &title)
{
    if (!m_api || itemId.isEmpty())
        return;
    const qint64 t = keyOf(time);
    if (has(itemId, double(t)))
        return; // already a bookmark at this second

    // Optimistic insert so the UI reflects it before the round-trip completes.
    Bookmark bm;
    bm.time = double(t);
    bm.title = title;
    m_byItem[itemId].push_back(bm);
    emit changed();

    const QJsonObject body{{QStringLiteral("time"), double(t)},
                           {QStringLiteral("title"), title}};
    m_api->post(m_api->endpoints().bookmark(itemId), body,
                [this, itemId, t](const ApiResponse &res) {
        if (res.stale || res.ok)
            return; // stale: a later login reloads authoritative bookmarks
        // Roll back the optimistic insert on failure.
        auto bit = m_byItem.find(itemId);
        if (bit == m_byItem.end())
            return;
        auto &vec = bit.value();
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [t](const Bookmark &b) { return keyOf(b.time) == t; }),
                  vec.end());
        if (vec.isEmpty())
            m_byItem.erase(bit);
        emit changed();
    });
}

void BookmarkStore::remove(const QString &itemId, double time)
{
    if (!m_api || itemId.isEmpty())
        return;
    const qint64 t = keyOf(time);

    // Snapshot the target so it can be restored if the delete is rejected.
    Bookmark removed;
    bool found = false;
    auto bit = m_byItem.find(itemId);
    if (bit != m_byItem.end()) {
        auto &vec = bit.value();
        for (const Bookmark &b : vec) {
            if (keyOf(b.time) == t) {
                removed = b;
                found = true;
                break;
            }
        }
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [t](const Bookmark &b) { return keyOf(b.time) == t; }),
                  vec.end());
        if (vec.isEmpty())
            m_byItem.erase(bit);
    }
    if (!found)
        return;
    emit changed();

    m_api->del(m_api->endpoints().bookmarkAt(itemId, double(t)),
               [this, itemId, removed](const ApiResponse &res) {
        if (res.stale || res.ok)
            return;
        m_byItem[itemId].push_back(removed); // restore on failure
        emit changed();
    });
}
