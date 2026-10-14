#include "computerservice.h"

#include "database.h"

#include <QRegularExpression>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

namespace labdesk {

namespace {
Computer readComputer(const QSqlQuery& q)
{
    Computer c;
    c.computerId = q.value(0).toString();
    c.name = q.value(1).toString();
    c.location = q.value(2).toString();
    c.status = q.value(3).toString();
    return c;
}

Computer normalised(Computer c)
{
    c.computerId = c.computerId.trimmed().toUpper();
    c.name = c.name.simplified();
    c.location = c.location.simplified();
    return c;
}
}  // namespace

QString ComputerService::validate(const Computer& in)
{
    const Computer c = normalised(in);
    static const QRegularExpression idPattern(QStringLiteral("^[A-Z0-9_-]{2,20}$"));
    if (c.computerId.isEmpty())
        return QStringLiteral("Computer ID is required.");
    if (!idPattern.match(c.computerId).hasMatch())
        return QStringLiteral("Computer ID must be 2-20 characters: letters, digits, '-' or '_'.");
    if (c.name.isEmpty())
        return QStringLiteral("Computer name is required.");
    if (c.location.isEmpty())
        return QStringLiteral("Location is required.");
    if (c.name.size() > 60 || c.location.size() > 80)
        return QStringLiteral("Name (max 60) or location (max 80) is too long.");
    return QString();
}

std::vector<Computer> ComputerService::listComputers(const QString& statusFilter, QString* error) const
{
    std::vector<Computer> out;
    QSqlQuery q(m_db.connection());
    if (statusFilter.isEmpty()) {
        q.prepare(QStringLiteral("SELECT computer_id, computer_name, location, status FROM computer "
                                 "ORDER BY computer_id"));
    } else {
        q.prepare(QStringLiteral("SELECT computer_id, computer_name, location, status FROM computer "
                                 "WHERE status = ? ORDER BY computer_id"));
        q.addBindValue(statusFilter);
    }
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return out;
    }
    while (q.next())
        out.push_back(readComputer(q));
    return out;
}

std::optional<Computer> ComputerService::findComputer(const QString& computerId, QString* error) const
{
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("SELECT computer_id, computer_name, location, status FROM computer "
                             "WHERE computer_id = ?"));
    q.addBindValue(computerId.trimmed());
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return readComputer(q);
}

OpResult ComputerService::addComputer(const Computer& input) const
{
    const Computer c = normalised(input);
    const QString invalid = validate(c);
    if (!invalid.isEmpty())
        return OpResult::failure(invalid);

    QSqlDatabase db = m_db.connection();
    Transaction tx(db);
    if (!tx.isActive())
        return OpResult::failure(QStringLiteral("Could not start transaction: ") + tx.lastError());
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM computer WHERE computer_id = ?"));
        q.addBindValue(c.computerId);
        if (!q.exec() || !q.next())
            return OpResult::failure(q.lastError().text());
        if (q.value(0).toInt() > 0)
            return OpResult::failure(QStringLiteral("Computer ID %1 already exists.").arg(c.computerId));
    }
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO computer (computer_id, computer_name, location, status) "
                                 "VALUES (?, ?, ?, 'AVAILABLE')"));
        q.addBindValue(c.computerId);
        q.addBindValue(c.name);
        q.addBindValue(c.location);
        if (!q.exec())
            return OpResult::failure(friendlySqlError(q.lastError()));
    }
    if (!tx.commit())
        return OpResult::failure(QStringLiteral("Commit failed: ") + tx.lastError());
    return OpResult::success(QStringLiteral("Computer %1 added as AVAILABLE.").arg(c.computerId));
}

OpResult ComputerService::updateComputer(const Computer& input) const
{
    const Computer c = normalised(input);
    const QString invalid = validate(c);
    if (!invalid.isEmpty())
        return OpResult::failure(invalid);

    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("UPDATE computer SET computer_name = ?, location = ? WHERE computer_id = ?"));
    q.addBindValue(c.name);
    q.addBindValue(c.location);
    q.addBindValue(c.computerId);
    if (!q.exec())
        return OpResult::failure(friendlySqlError(q.lastError()));
    if (q.numRowsAffected() != 1)
        return OpResult::failure(QStringLiteral("Computer %1 was not found.").arg(c.computerId));
    return OpResult::success(QStringLiteral("Computer %1 updated.").arg(c.computerId));
}

OpResult ComputerService::deleteComputer(const QString& computerIdIn) const
{
    const QString computerId = computerIdIn.trimmed();
    QSqlDatabase db = m_db.connection();
    Transaction tx(db);
    if (!tx.isActive())
        return OpResult::failure(QStringLiteral("Could not start transaction: ") + tx.lastError());
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT "
                                 " (SELECT COUNT(*) FROM session WHERE computer_id = ?),"
                                 " (SELECT COUNT(*) FROM maintenance WHERE computer_id = ?)"));
        q.addBindValue(computerId);
        q.addBindValue(computerId);
        if (!q.exec() || !q.next())
            return OpResult::failure(q.lastError().text());
        const int sessions = q.value(0).toInt();
        const int repairs = q.value(1).toInt();
        if (sessions > 0 || repairs > 0) {
            return OpResult::failure(
                QStringLiteral("Computer %1 has %2 session and %3 maintenance record(s). "
                               "Historical records are preserved, so it cannot be deleted.")
                    .arg(computerId)
                    .arg(sessions)
                    .arg(repairs));
        }
    }
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("DELETE FROM computer WHERE computer_id = ?"));
        q.addBindValue(computerId);
        if (!q.exec())
            return OpResult::failure(friendlySqlError(q.lastError()));  // FK RESTRICT backstop
        if (q.numRowsAffected() != 1)
            return OpResult::failure(QStringLiteral("Computer %1 was not found.").arg(computerId));
    }
    if (!tx.commit())
        return OpResult::failure(QStringLiteral("Commit failed: ") + tx.lastError());
    return OpResult::success(QStringLiteral("Computer %1 deleted.").arg(computerId));
}

DashboardStats ComputerService::dashboardStats(QString* error) const
{
    DashboardStats s;
    QSqlQuery q(m_db.connection());
    const bool ok = q.exec(QStringLiteral(
        "SELECT "
        " (SELECT COUNT(DISTINCT location) FROM computer),"
        " (SELECT COUNT(*) FROM computer),"
        " (SELECT COUNT(*) FROM computer WHERE status = 'AVAILABLE'),"
        " (SELECT COUNT(*) FROM computer WHERE status = 'OCCUPIED'),"
        " (SELECT COUNT(*) FROM computer WHERE status = 'MAINTENANCE'),"
        " (SELECT COUNT(*) FROM session  WHERE status = 'ACTIVE'),"
        " (SELECT COUNT(*) FROM student),"
        " (SELECT COUNT(*) FROM maintenance WHERE status = 'OPEN')"));
    if (!ok || !q.next()) {
        if (error)
            *error = q.lastError().text();
        return s;
    }
    s.laboratories = q.value(0).toInt();
    s.totalComputers = q.value(1).toInt();
    s.available = q.value(2).toInt();
    s.occupied = q.value(3).toInt();
    s.maintenance = q.value(4).toInt();
    s.activeSessions = q.value(5).toInt();
    s.students = q.value(6).toInt();
    s.openMaintenance = q.value(7).toInt();
    return s;
}

bool ComputerService::labSummaries(std::vector<LabSummary>& out, QString* error) const
{
    out.clear();
    QSqlQuery q(m_db.connection());
    const bool ok = q.exec(QStringLiteral(
        "SELECT location, COUNT(*),"
        "       SUM(CASE WHEN status = 'AVAILABLE'   THEN 1 ELSE 0 END),"
        "       SUM(CASE WHEN status = 'OCCUPIED'    THEN 1 ELSE 0 END),"
        "       SUM(CASE WHEN status = 'MAINTENANCE' THEN 1 ELSE 0 END) "
        "FROM computer GROUP BY location ORDER BY location"));
    if (!ok) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    while (q.next()) {
        LabSummary lab;
        lab.location = q.value(0).toString();
        lab.total = q.value(1).toInt();
        lab.available = q.value(2).toInt();
        lab.occupied = q.value(3).toInt();
        lab.maintenance = q.value(4).toInt();
        out.push_back(lab);
    }
    return true;
}

QStringList ComputerService::consistencyProblems(QString* error) const
{
    QStringList problems;
    QSqlQuery q(m_db.connection());
    const bool ok = q.exec(QStringLiteral(
        "SELECT c.computer_id, c.status,"
        "       EXISTS (SELECT 1 FROM session s WHERE s.computer_id = c.computer_id AND s.status = 'ACTIVE'),"
        "       EXISTS (SELECT 1 FROM maintenance m WHERE m.computer_id = c.computer_id AND m.status = 'OPEN') "
        "FROM computer c ORDER BY c.computer_id"));
    if (!ok) {
        if (error)
            *error = q.lastError().text();
        return problems;
    }
    while (q.next()) {
        const QString id = q.value(0).toString();
        const QString status = q.value(1).toString();
        const bool active = q.value(2).toInt() != 0;
        const bool open = q.value(3).toInt() != 0;
        if (status == ComputerStatus::Occupied && !active)
            problems << QStringLiteral("%1 is OCCUPIED but has no active session.").arg(id);
        if (status != ComputerStatus::Occupied && active)
            problems << QStringLiteral("%1 has an active session but is %2.").arg(id, status);
        if (status == ComputerStatus::Maintenance && !open)
            problems << QStringLiteral("%1 is in MAINTENANCE with no open issue.").arg(id);
        if (status == ComputerStatus::Available && open)
            problems << QStringLiteral("%1 is AVAILABLE but has an open maintenance issue.").arg(id);
    }
    return problems;
}

}  // namespace labdesk
