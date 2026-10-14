#include "processservice.h"

#include "database.h"

#include <QSqlQuery>
#include <QVariant>

namespace labdesk {

namespace {
QVariant sessionKey(std::optional<qint64> sessionId)
{
    return sessionId ? QVariant::fromValue<qlonglong>(*sessionId) : QVariant();  // QVariant() binds NULL
}
}  // namespace

std::vector<StoredProcess> ProcessService::loadWorkload(std::optional<qint64> sessionId, QString* error) const
{
    std::vector<StoredProcess> out;
    QSqlQuery q(m_db.connection());
    // "IS ?" matches both NULL and a concrete id with one prepared statement.
    q.prepare(QStringLiteral("SELECT process_id, session_id, label, arrival_time, burst_time, priority "
                             "FROM process WHERE session_id IS ? ORDER BY process_id"));
    q.addBindValue(sessionKey(sessionId));
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return out;
    }
    while (q.next()) {
        StoredProcess p;
        p.processId = q.value(0).toLongLong();
        if (!q.value(1).isNull())
            p.sessionId = q.value(1).toLongLong();
        p.label = q.value(2).toString();
        p.arrival = q.value(3).toInt();
        p.burst = q.value(4).toInt();
        p.priority = q.value(5).toInt();
        out.push_back(p);
    }
    return out;
}

bool ProcessService::workloadSizes(QHash<qint64, int>& out, QString* error) const
{
    out.clear();
    QSqlQuery q(m_db.connection());
    if (!q.exec(QStringLiteral("SELECT IFNULL(session_id, -1), COUNT(*) FROM process GROUP BY session_id"))) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    while (q.next())
        out.insert(q.value(0).toLongLong(), q.value(1).toInt());
    return true;
}

OpResult ProcessService::saveWorkload(std::optional<qint64> sessionId,
                                      const std::vector<StoredProcess>& processes) const
{
    if (processes.empty())
        return OpResult::failure(QStringLiteral("There are no processes to save."));

    QSqlDatabase db = m_db.connection();
    Transaction tx(db);
    if (!tx.isActive())
        return OpResult::failure(QStringLiteral("Could not start transaction: ") + tx.lastError());
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("DELETE FROM process WHERE session_id IS ?"));
        q.addBindValue(sessionKey(sessionId));
        if (!q.exec())
            return OpResult::failure(friendlySqlError(q.lastError()));
    }
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("INSERT INTO process (session_id, label, arrival_time, burst_time, priority) "
                                  "VALUES (?, ?, ?, ?, ?)"));
    for (const StoredProcess& p : processes) {
        insert.addBindValue(sessionKey(sessionId));
        insert.addBindValue(p.label.trimmed());
        insert.addBindValue(p.arrival);
        insert.addBindValue(p.burst);
        insert.addBindValue(p.priority);
        if (!insert.exec()) {
            return OpResult::failure(QStringLiteral("Process %1 was rejected (%2). Nothing was saved.")
                                         .arg(p.label, friendlySqlError(insert.lastError())));
        }
    }
    insert.finish();
    if (!tx.commit())
        return OpResult::failure(QStringLiteral("Commit failed: ") + tx.lastError());
    return OpResult::success(QStringLiteral("Saved %1 process(es).").arg(static_cast<int>(processes.size())));
}

}  // namespace labdesk
