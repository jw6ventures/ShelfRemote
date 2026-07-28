#include "model/ProgressStore.h"

#include <QDateTime>
#include <QJsonArray>

ProgressStore::ProgressStore(QObject *parent)
    : QObject(parent)
{
}

void ProgressStore::loadFromUser(const QJsonObject &user)
{
    m_byItem.clear();
    const QJsonArray progresses = user.value(QStringLiteral("mediaProgress")).toArray();
    for (const auto &v : progresses) {
        const QJsonObject p = v.toObject();
        // Skip episode-level progress for the item-level cards.
        if (!p.value(QStringLiteral("episodeId")).isNull() &&
            !p.value(QStringLiteral("episodeId")).toString().isEmpty())
            continue;
        const QString itemId = p.value(QStringLiteral("libraryItemId")).toString();
        if (itemId.isEmpty())
            continue;
        Rec r;
        r.fraction = p.value(QStringLiteral("progress")).toDouble();
        r.currentTime = p.value(QStringLiteral("currentTime")).toDouble();
        r.startedAtMs = p.value(QStringLiteral("startedAt")).toDouble();
        r.finished = p.value(QStringLiteral("isFinished")).toBool();
        m_byItem.insert(itemId, r);
    }
    emit changed();
}

void ProgressStore::clear()
{
    if (m_byItem.isEmpty())
        return;
    m_byItem.clear();
    emit changed();
}

double ProgressStore::fraction(const QString &itemId) const
{
    auto it = m_byItem.constFind(itemId);
    if (it == m_byItem.constEnd())
        return 0.0;
    return it->finished ? 1.0 : it->fraction;
}

bool ProgressStore::isFinished(const QString &itemId) const
{
    auto it = m_byItem.constFind(itemId);
    return it != m_byItem.constEnd() && it->finished;
}

double ProgressStore::currentTime(const QString &itemId) const
{
    auto it = m_byItem.constFind(itemId);
    return it == m_byItem.constEnd() ? 0.0 : it->currentTime;
}

double ProgressStore::startedAtMs(const QString &itemId) const
{
    auto it = m_byItem.constFind(itemId);
    return it == m_byItem.constEnd() ? 0.0 : it->startedAtMs;
}

void ProgressStore::update(const QString &itemId, double currentTime, double duration,
                           bool finished)
{
    if (itemId.isEmpty())
        return;
    Rec &r = m_byItem[itemId];
    r.currentTime = currentTime;
    r.finished = finished;
    if (duration > 0.0)
        r.fraction = finished ? 1.0 : qBound(0.0, currentTime / duration, 1.0);
    if (r.startedAtMs <= 0.0)
        r.startedAtMs = double(QDateTime::currentMSecsSinceEpoch());
    emit changed();
}
