#include "reportservice.h"

#include "database.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QVariant>

namespace labdesk {

namespace {
// Minutes between two stored timestamps, as an SQL expression.
const QString kMinutes = QStringLiteral("((julianday(s.end_time) - julianday(s.start_time)) * 1440.0)");

QString text(const QSqlQuery& q, int column)
{
    return q.value(column).isNull() ? QString() : q.value(column).toString();
}
}  // namespace

bool ReportService::recentActivity(int limit, std::vector<ActivityItem>& out, QString* error) const
{
    out.clear();
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral(
        "SELECT t, kind, detail FROM ("
        " SELECT s.start_time AS t, 'SESSION_START' AS kind,"
        "        s.student_id || ' started using ' || s.computer_id AS detail FROM session s"
        " UNION ALL SELECT s.end_time, 'SESSION_END', s.student_id || ' released ' || s.computer_id"
        "        FROM session s WHERE s.end_time IS NOT NULL"
        " UNION ALL SELECT m.reported_at, 'MAINTENANCE_OPEN', m.computer_id || ' reported: ' || m.description"
        "        FROM maintenance m"
        " UNION ALL SELECT m.resolved_at, 'MAINTENANCE_RESOLVED', m.computer_id || ' repaired and back in service'"
        "        FROM maintenance m WHERE m.resolved_at IS NOT NULL"
        " UNION ALL SELECT r.requested_at, 'QUEUE_JOIN', r.student_id || ' joined the waiting queue'"
        "        FROM lab_request r WHERE r.granted_by IS NOT 'IMMEDIATE'"
        ") ORDER BY t DESC LIMIT ?"));
    q.addBindValue(qMax(1, limit));
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    while (q.next())
        out.push_back({text(q, 0), text(q, 1), text(q, 2)});
    return true;
}

bool ReportService::usage(const QString& sql, const QVariantList& binds, std::vector<UsageRow>& out,
                          QString* error) const
{
    out.clear();
    QSqlQuery q(m_db.connection());
    q.prepare(sql);
    for (const QVariant& v : binds)
        q.addBindValue(v);
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    while (q.next()) {
        UsageRow r;
        r.key = text(q, 0);
        r.label = text(q, 1);
        r.sessions = q.value(2).toInt();
        r.totalMinutes = q.value(3).toDouble();
        r.averageMinutes = q.value(4).toDouble();
        out.push_back(r);
    }
    return true;
}

bool ReportService::usageByLab(std::vector<UsageRow>& out, QString* error) const
{
    return usage(QStringLiteral("SELECT c.location, '', COUNT(*), SUM(%1), AVG(%1) "
                                "FROM session s JOIN computer c ON c.computer_id = s.computer_id "
                                "WHERE s.status = 'ENDED' GROUP BY c.location ORDER BY SUM(%1) DESC")
                     .arg(kMinutes),
                 {}, out, error);
}

bool ReportService::usageByDay(int days, std::vector<UsageRow>& out, QString* error) const
{
    const QString since = QDateTime::currentDateTimeUtc()
                              .addDays(-qMax(1, days))
                              .toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"));
    return usage(QStringLiteral("SELECT date(s.start_time, 'localtime') AS d, '', COUNT(*), SUM(%1), AVG(%1) "
                                "FROM session s WHERE s.status = 'ENDED' AND s.start_time >= ? "
                                "GROUP BY d ORDER BY d DESC")
                     .arg(kMinutes),
                 {since}, out, error);
}

bool ReportService::topStudents(int limit, std::vector<UsageRow>& out, QString* error) const
{
    return usage(QStringLiteral("SELECT s.student_id, st.name, COUNT(*), SUM(%1), AVG(%1) "
                                "FROM session s JOIN student st ON st.student_id = s.student_id "
                                "WHERE s.status = 'ENDED' GROUP BY s.student_id, st.name "
                                "ORDER BY SUM(%1) DESC LIMIT ?")
                     .arg(kMinutes),
                 {qMax(1, limit)}, out, error);
}

bool ReportService::queueByPolicy(std::vector<QueueRow>& out, QString* error) const
{
    out.clear();
    QSqlQuery q(m_db.connection());
    if (!q.exec(QStringLiteral(
            "SELECT r.granted_by, COUNT(*),"
            "       AVG((julianday(r.allocated_at) - julianday(r.requested_at)) * 1440.0),"
            "       MAX((julianday(r.allocated_at) - julianday(r.requested_at)) * 1440.0) "
            "FROM lab_request r WHERE r.status = 'ALLOCATED' GROUP BY r.granted_by ORDER BY r.granted_by"))) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    while (q.next()) {
        QueueRow r;
        r.grantedBy = text(q, 0);
        r.requests = q.value(1).toInt();
        r.averageWaitMinutes = q.value(2).toDouble();
        r.longestWaitMinutes = q.value(3).toDouble();
        out.push_back(r);
    }
    return true;
}

bool ReportService::studentHistory(const QString& studentId, std::vector<StudentVisit>& out, QString* error) const
{
    out.clear();
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral(
        "SELECT s.session_id, s.computer_id, c.location, s.start_time, s.end_time, s.status,"
        "       r.purpose, r.requested_at, r.expected_minutes "
        "FROM session s JOIN computer c ON c.computer_id = s.computer_id "
        "LEFT JOIN lab_request r ON r.session_id = s.session_id "
        "WHERE s.student_id = ? ORDER BY s.session_id DESC"));
    q.addBindValue(studentId.trimmed());
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    while (q.next()) {
        StudentVisit v;
        v.sessionId = q.value(0).toLongLong();
        v.computerId = text(q, 1);
        v.location = text(q, 2);
        v.startTime = text(q, 3);
        v.endTime = text(q, 4);
        v.status = text(q, 5);
        v.purpose = text(q, 6);
        v.requestedAt = text(q, 7);
        v.expectedMinutes = q.value(8).toInt();
        out.push_back(v);
    }
    return true;
}

}  // namespace labdesk
