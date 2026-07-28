#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

// Central per-item listening progress, keyed by libraryItemId. Audiobookshelf
// stores progress per user (not on each library item), so we load the user's
// mediaProgress array once (from /api/authorize) and let QML cards look progress
// up by id. Kept in sync opportunistically after playback closes.
class ProgressStore : public QObject
{
    Q_OBJECT
public:
    explicit ProgressStore(QObject *parent = nullptr);

    // Populate from a user object that contains a "mediaProgress" array.
    void loadFromUser(const QJsonObject &user);

    // Drop all cached progress (on logout / server change) so one user's progress
    // is never shown under another's session.
    void clear();

    // 0.0..1.0 fraction complete for an item (ignores episodes for now).
    Q_INVOKABLE double fraction(const QString &itemId) const;
    Q_INVOKABLE bool isFinished(const QString &itemId) const;
    Q_INVOKABLE double currentTime(const QString &itemId) const;   // seconds
    Q_INVOKABLE double startedAtMs(const QString &itemId) const;   // epoch ms, 0 if unknown
    Q_INVOKABLE bool has(const QString &itemId) const { return m_byItem.contains(itemId); }

    // Update a single item after local playback (so the UI reflects it without a
    // round-trip). finished true forces fraction to 1.0.
    Q_INVOKABLE void update(const QString &itemId, double currentTime, double duration,
                            bool finished);

signals:
    void changed();

private:
    struct Rec {
        double fraction = 0.0;
        double currentTime = 0.0;
        double startedAtMs = 0.0;
        bool   finished = false;
    };
    QHash<QString, Rec> m_byItem;
};
