#include "maintenanceservice.h"

#include "database.h"
#include "timeutil.h"

#include <QSqlQuery>
#include <QVariant>

namespace labdesk {

namespace {
QString validateDescription(const QString& description)
{
    const QString d = description.trimmed();
    if (d.isEmpty())
        return QStringLiteral("Describe the problem.");
    if (d.size() > 500)
        return QStringLiteral("Description must be at most 500 characters.");
    return QString();
}
}  // namespace

std::vector<MaintenanceRecord> MaintenanceService::listRecords(bool openOnly, QString* error) const
{
    std::vector<MaintenanceRecord> out;
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("SELECT m.maintenance_id, m.computer_id, c.computer_name, m.description,"
                             "       m.reported_at, m.resolved_at, m.status "
                             "FROM maintenance m JOIN computer c ON c.computer_id = m.computer_id ")
              + (openOnly ? QStringLiteral("WHERE m.status = 'OPEN' ") : QString())
              + QStringLiteral("ORDER BY (m.status = 'OPEN') DESC, m.maintenance_id DESC"));
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return out;
    }
    while (q.next()) {
        MaintenanceRecord r;
        r.maintenanceId = q.value(0).toLongLong();
        r.computerId = q.value(1).toString();
        r.computerName = q.value(2).toString();
        r.description = q.value(3).toString();
        r.reportedAt = q.value(4).toString();
        r.resolvedAt = q.value(5).isNull() ? QString() : q.value(5).toString();
        r.status = q.value(6).toString();
        out.push_back(r);
    }
    return out;
}

OpResult MaintenanceService::reportIssue(const QString& computerIdIn, const QString& description) const
{
    const QString computerId = computerIdIn.trimmed();
    const QString invalid = validateDescription(description);
    if (computerId.isEmpty())
        return OpResult::failure(QStringLiteral("Choose a computer."));
    if (!invalid.isEmpty())
        return OpResult::failure(invalid);

    QSqlDatabase db = m_db.connection();
    Transaction tx(db);
    if (!tx.isActive())
        return OpResult::failure(QStringLiteral("Could not start transaction: ") + tx.lastError());

    QString status;
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT status FROM computer WHERE computer_id = ?"));
        q.addBindValue(computerId);
        if (!q.exec())
            return OpResult::failure(q.lastError().text());
        if (!q.next())
            return OpResult::failure(QStringLiteral("Computer %1 does not exist.").arg(computerId));
        status = q.value(0).toString();
    }
    if (status == ComputerStatus::Occupied) {
        return OpResult::failure(QStringLiteral("Computer %1 is in use by an active session. End the session "
                                                "before placing it into maintenance.")
                                     .arg(computerId));
    }
    qint64 newId = -1;
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO maintenance (computer_id, description, reported_at, status) "
                                 "VALUES (?, ?, ?, 'OPEN')"));
        q.addBindValue(computerId);
        q.addBindValue(description.trimmed());
        q.addBindValue(timeutil::nowUtc());
        if (!q.exec())
            return OpResult::failure(friendlySqlError(q.lastError()));
        newId = q.lastInsertId().toLongLong();
    }
    if (status == ComputerStatus::Available) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE computer SET status = 'MAINTENANCE' "
                                 "WHERE computer_id = ? AND status = 'AVAILABLE'"));
        q.addBindValue(computerId);
        if (!q.exec())
            return OpResult::failure(friendlySqlError(q.lastError()));
        if (q.numRowsAffected() != 1)
            return OpResult::failure(QStringLiteral("Computer %1 changed state; try again.").arg(computerId));
    }
    if (!tx.commit())
        return OpResult::failure(QStringLiteral("Commit failed: ") + tx.lastError());
    return OpResult::success(QStringLiteral("Issue #%1 recorded; %2 is under MAINTENANCE.").arg(newId).arg(computerId),
                             newId);
}

OpResult MaintenanceService::updateDescription(qint64 maintenanceId, const QString& description) const
{
    const QString invalid = validateDescription(description);
    if (!invalid.isEmpty())
        return OpResult::failure(invalid);
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("UPDATE maintenance SET description = ? WHERE maintenance_id = ?"));
    q.addBindValue(description.trimmed());
    q.addBindValue(maintenanceId);
    if (!q.exec())
        return OpResult::failure(friendlySqlError(q.lastError()));
    if (q.numRowsAffected() != 1)
        return OpResult::failure(QStringLiteral("Maintenance record #%1 was not found.").arg(maintenanceId));
    return OpResult::success(QStringLiteral("Issue #%1 updated.").arg(maintenanceId));
}

OpResult MaintenanceService::resolveIssue(qint64 maintenanceId) const
{
    QSqlDatabase db = m_db.connection();
    Transaction tx(db);
    if (!tx.isActive())
        return OpResult::failure(QStringLiteral("Could not start transaction: ") + tx.lastError());

    QString computerId;
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT computer_id, status FROM maintenance WHERE maintenance_id = ?"));
        q.addBindValue(maintenanceId);
        if (!q.exec())
            return OpResult::failure(q.lastError().text());
        if (!q.next())
            return OpResult::failure(QStringLiteral("Maintenance record #%1 was not found.").arg(maintenanceId));
        computerId = q.value(0).toString();
        if (q.value(1).toString() != MaintenanceStatus::Open)
            return OpResult::failure(QStringLiteral("Issue #%1 is already resolved.").arg(maintenanceId));
    }
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE maintenance SET status = 'RESOLVED', resolved_at = ? "
                                 "WHERE maintenance_id = ? AND status = 'OPEN'"));
        q.addBindValue(timeutil::nowUtc());
        q.addBindValue(maintenanceId);
        if (!q.exec())
            return OpResult::failure(friendlySqlError(q.lastError()));
        if (q.numRowsAffected() != 1)
            return OpResult::failure(QStringLiteral("Issue #%1 changed state; try again.").arg(maintenanceId));
    }

    int openIssues = 0;
    int activeSessions = 0;
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT "
                                 " (SELECT COUNT(*) FROM maintenance WHERE computer_id = ? AND status = 'OPEN'),"
                                 " (SELECT COUNT(*) FROM session WHERE computer_id = ? AND status = 'ACTIVE')"));
        q.addBindValue(computerId);
        q.addBindValue(computerId);
        if (!q.exec() || !q.next())
            return OpResult::failure(q.lastError().text());
        openIssues = q.value(0).toInt();
        activeSessions = q.value(1).toInt();
    }

    QString outcome;
    if (openIssues == 0 && activeSessions == 0) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE computer SET status = 'AVAILABLE' "
                                 "WHERE computer_id = ? AND status = 'MAINTENANCE'"));
        q.addBindValue(computerId);
        if (!q.exec())
            return OpResult::failure(friendlySqlError(q.lastError()));
        outcome = q.numRowsAffected() == 1
                      ? QStringLiteral("%1 is AVAILABLE again.").arg(computerId)
                      : QStringLiteral("%1 was not in MAINTENANCE; status unchanged.").arg(computerId);
    } else {
        outcome = QStringLiteral("%1 stays in MAINTENANCE: %2 other open issue(s).").arg(computerId).arg(openIssues);
    }

    if (!tx.commit())
        return OpResult::failure(QStringLiteral("Commit failed: ") + tx.lastError());
    return OpResult::success(QStringLiteral("Issue #%1 resolved. ").arg(maintenanceId) + outcome, maintenanceId);
}

}  // namespace labdesk
