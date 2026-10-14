#include "queueservice.h"

#include "database.h"
#include "timeutil.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QVariant>

namespace labdesk {

namespace {

qint64 toMs(const QString& iso)
{
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(iso, Qt::ISODate);
    return dt.isValid() ? dt.toMSecsSinceEpoch() : 0;
}

lab::Policy readPolicy(const QSqlDatabase& db, QString* error)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT value FROM lab_setting WHERE setting_key = 'queue_policy'"))) {
        if (error)
            *error = q.lastError().text();
        return lab::Policy::FCFS;
    }
    if (q.next()) {
        if (const auto p = lab::policyFromCode(q.value(0).toString().toStdString()))
            return *p;
    }
    return lab::Policy::FCFS;  // default when the setting row is missing
}

// WAITING requests (optionally skipping students who somehow already hold a
// session), plus the view the pure dispatch logic needs.
bool loadWaiting(const QSqlDatabase& db, bool skipStudentsInSession, std::vector<QueueEntry>& rows,
                 std::vector<lab::WaitingRequest>& view, QString* error)
{
    rows.clear();
    view.clear();
    QSqlQuery q(db);
    QString sql = QStringLiteral(
        "SELECT r.request_id, r.student_id, st.name, r.purpose, r.priority, r.expected_minutes, r.requested_at "
        "FROM lab_request r JOIN student st ON st.student_id = r.student_id "
        "WHERE r.status = 'WAITING' ");
    if (skipStudentsInSession)
        sql += QStringLiteral("AND NOT EXISTS (SELECT 1 FROM session s WHERE s.student_id = r.student_id "
                              "AND s.status = 'ACTIVE') ");
    sql += QStringLiteral("ORDER BY r.request_id");
    if (!q.exec(sql)) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    while (q.next()) {
        QueueEntry e;
        e.requestId = q.value(0).toLongLong();
        e.studentId = q.value(1).toString();
        e.studentName = q.value(2).toString();
        e.purpose = q.value(3).toString();
        e.priority = q.value(4).toInt();
        e.expectedMinutes = q.value(5).toInt();
        e.requestedAt = q.value(6).toString();
        rows.push_back(e);
        view.push_back({e.requestId, toMs(e.requestedAt), e.expectedMinutes, e.priority});
    }
    return true;
}

// Marks the computer OCCUPIED and inserts an ACTIVE session starting at `now`.
// Returns the new session id, or -1 with `error` filled.
qint64 startSessionWithin(const QSqlDatabase& db, const QString& studentId, const QString& computerId,
                          const QString& now, QString* error)
{
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE computer SET status = 'OCCUPIED' "
                                 "WHERE computer_id = ? AND status = 'AVAILABLE'"));
        q.addBindValue(computerId);
        if (!q.exec()) {
            if (error)
                *error = friendlySqlError(q.lastError());
            return -1;
        }
        if (q.numRowsAffected() != 1) {
            if (error)
                *error = QStringLiteral("Computer %1 is no longer available.").arg(computerId);
            return -1;
        }
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO session (student_id, computer_id, start_time, status) "
                             "VALUES (?, ?, ?, 'ACTIVE')"));
    q.addBindValue(studentId);
    q.addBindValue(computerId);
    q.addBindValue(now);
    if (!q.exec()) {
        if (error)
            *error = friendlySqlError(q.lastError());
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

// Checks shared by startNow and joinQueue. Returns an empty string when the
// student may request, otherwise the reason. Runs inside the transaction.
QString studentMayRequest(const QSqlDatabase& db, const QString& studentId, QString* studentName)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT st.name,"
        " (SELECT computer_id FROM session WHERE student_id = st.student_id AND status = 'ACTIVE'),"
        " (SELECT COUNT(*) FROM lab_request WHERE student_id = st.student_id AND status = 'WAITING') "
        "FROM student st WHERE st.student_id = ?"));
    q.addBindValue(studentId);
    if (!q.exec())
        return q.lastError().text();
    if (!q.next())
        return QStringLiteral("Student %1 does not exist.").arg(studentId);
    if (studentName)
        *studentName = q.value(0).toString();
    if (!q.value(1).isNull())
        return QStringLiteral("You are already using %1. End that session first.").arg(q.value(1).toString());
    if (q.value(2).toInt() > 0)
        return QStringLiteral("You are already waiting in the queue.");
    return QString();
}

int waitingCount(const QSqlDatabase& db, QString* error)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM lab_request WHERE status = 'WAITING'")) || !q.next()) {
        if (error)
            *error = q.lastError().text();
        return -1;
    }
    return q.value(0).toInt();
}

// Inserts a request that is granted at once (arrival = start = now).
bool insertImmediateRequest(const QSqlDatabase& db, const QString& studentId, lab::Purpose purpose,
                            int expectedMinutes, qint64 sessionId, const QString& now, qint64* requestId,
                            QString* error)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO lab_request (student_id, purpose, priority, expected_minutes, requested_at,"
                             " status, session_id, allocated_at, granted_by) "
                             "VALUES (?, ?, ?, ?, ?, 'ALLOCATED', ?, ?, 'IMMEDIATE')"));
    q.addBindValue(studentId);
    q.addBindValue(QString::fromLatin1(lab::purposeCode(purpose)));
    q.addBindValue(lab::priorityFor(purpose));
    q.addBindValue(expectedMinutes);
    q.addBindValue(now);
    q.addBindValue(sessionId);
    q.addBindValue(now);
    if (!q.exec()) {
        if (error)
            *error = friendlySqlError(q.lastError());
        return false;
    }
    if (requestId)
        *requestId = q.lastInsertId().toLongLong();
    return true;
}

}  // namespace

QString QueueService::validateRequest(const QString& purposeCode, int expectedMinutes)
{
    if (!lab::purposeFromCode(purposeCode.toStdString()))
        return QStringLiteral("Choose the purpose of your visit (exam, assignment or practice).");
    if (expectedMinutes < lab::kMinExpectedMinutes || expectedMinutes > lab::kMaxExpectedMinutes)
        return QStringLiteral("Expected length must be between %1 and %2 minutes.")
            .arg(lab::kMinExpectedMinutes)
            .arg(lab::kMaxExpectedMinutes);
    return QString();
}

lab::Policy QueueService::policy(QString* error) const
{
    return readPolicy(m_db.connection(), error);
}

OpResult QueueService::setPolicy(lab::Policy policy) const
{
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO lab_setting (setting_key, value) VALUES ('queue_policy', ?)"));
    q.addBindValue(QString::fromLatin1(lab::policyCode(policy)));
    if (!q.exec())
        return OpResult::failure(friendlySqlError(q.lastError()));
    return OpResult::success(QStringLiteral("Queue policy set to %1. It decides who gets the next free computer.")
                                 .arg(QString::fromStdString(lab::policyName(policy))));
}

bool QueueService::dispatchWithin(const QSqlDatabase& db, QStringList* assignments, QString* error)
{
    QString policyError;
    const lab::Policy policy = readPolicy(db, &policyError);
    if (!policyError.isEmpty()) {
        if (error)
            *error = policyError;
        return false;
    }
    const QString code = QString::fromLatin1(lab::policyCode(policy));
    for (int guard = 0; guard < 100000; ++guard) {
        QString computerId;
        {
            QSqlQuery q(db);
            if (!q.exec(QStringLiteral("SELECT computer_id FROM computer WHERE status = 'AVAILABLE' "
                                       "ORDER BY computer_id LIMIT 1"))) {
                if (error)
                    *error = q.lastError().text();
                return false;
            }
            if (!q.next())
                return true;  // no free computer
            computerId = q.value(0).toString();
        }
        std::vector<QueueEntry> rows;
        std::vector<lab::WaitingRequest> view;
        if (!loadWaiting(db, true, rows, view, error))
            return false;
        if (rows.empty())
            return true;  // nobody waiting
        const QueueEntry& next = rows[static_cast<std::size_t>(lab::selectNext(policy, view))];

        const QString now = timeutil::nowUtc();
        const qint64 sessionId = startSessionWithin(db, next.studentId, computerId, now, error);
        if (sessionId < 0)
            return false;
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE lab_request SET status = 'ALLOCATED', session_id = ?, allocated_at = ?,"
                                 " granted_by = ? WHERE request_id = ? AND status = 'WAITING'"));
        q.addBindValue(sessionId);
        q.addBindValue(now);
        q.addBindValue(code);
        q.addBindValue(next.requestId);
        if (!q.exec()) {
            if (error)
                *error = friendlySqlError(q.lastError());
            return false;
        }
        if (q.numRowsAffected() != 1) {
            if (error)
                *error = QStringLiteral("Request #%1 changed state during dispatch.").arg(next.requestId);
            return false;
        }
        if (assignments) {
            assignments->append(QStringLiteral("%1 assigned to waiting student %2 (request #%3, %4 policy, waited "
                                               "%5)")
                                    .arg(computerId, next.studentId)
                                    .arg(next.requestId)
                                    .arg(code, timeutil::durationText(next.requestedAt, now)));
        }
    }
    if (error)
        *error = QStringLiteral("Dispatch did not finish.");
    return false;
}

OpResult QueueService::startNow(const QString& studentIdIn, const QString& computerIdIn, const QString& purposeCode,
                                int expectedMinutes) const
{
    const QString studentId = studentIdIn.trimmed();
    const QString computerId = computerIdIn.trimmed();
    const QString invalid = validateRequest(purposeCode, expectedMinutes);
    if (!invalid.isEmpty())
        return OpResult::failure(invalid);
    if (computerId.isEmpty())
        return OpResult::failure(QStringLiteral("Select a computer first."));
    const lab::Purpose purpose = *lab::purposeFromCode(purposeCode.toStdString());

    QSqlDatabase db = m_db.connection();
    Transaction tx(db);
    if (!tx.isActive())
        return OpResult::failure(QStringLiteral("Could not start transaction: ") + tx.lastError());

    QString name;
    const QString refusal = studentMayRequest(db, studentId, &name);
    if (!refusal.isEmpty())
        return OpResult::failure(refusal);
    QString error;
    const int waiting = waitingCount(db, &error);
    if (waiting < 0)
        return OpResult::failure(error);
    if (waiting > 0) {
        return OpResult::failure(QStringLiteral("%1 student(s) are already waiting, so computers are assigned from "
                                                "the queue. Join the queue instead.")
                                     .arg(waiting));
    }
    const QString now = timeutil::nowUtc();
    const qint64 sessionId = startSessionWithin(db, studentId, computerId, now, &error);
    if (sessionId < 0)
        return OpResult::failure(error);
    qint64 requestId = -1;
    if (!insertImmediateRequest(db, studentId, purpose, expectedMinutes, sessionId, now, &requestId, &error))
        return OpResult::failure(error);
    if (!tx.commit())
        return OpResult::failure(QStringLiteral("Commit failed: ") + tx.lastError());
    return OpResult::success(QStringLiteral("You are now using %1. Request #%2 recorded at %3; you waited 0 minutes. "
                                            "Your actual session length is measured when you end the session.")
                                 .arg(computerId)
                                 .arg(requestId)
                                 .arg(timeutil::toDisplay(now)),
                             sessionId);
}

OpResult QueueService::joinQueue(const QString& studentIdIn, const QString& purposeCode, int expectedMinutes) const
{
    const QString studentId = studentIdIn.trimmed();
    const QString invalid = validateRequest(purposeCode, expectedMinutes);
    if (!invalid.isEmpty())
        return OpResult::failure(invalid);
    const lab::Purpose purpose = *lab::purposeFromCode(purposeCode.toStdString());

    QSqlDatabase db = m_db.connection();
    Transaction tx(db);
    if (!tx.isActive())
        return OpResult::failure(QStringLiteral("Could not start transaction: ") + tx.lastError());

    QString name;
    const QString refusal = studentMayRequest(db, studentId, &name);
    if (!refusal.isEmpty())
        return OpResult::failure(refusal);
    QString error;
    const int waiting = waitingCount(db, &error);
    if (waiting < 0)
        return OpResult::failure(error);
    const QString now = timeutil::nowUtc();

    // A computer is free and nobody is ahead: no reason to wait.
    if (waiting == 0) {
        QString freeComputer;
        {
            QSqlQuery q(db);
            if (!q.exec(QStringLiteral("SELECT computer_id FROM computer WHERE status = 'AVAILABLE' "
                                       "ORDER BY computer_id LIMIT 1")))
                return OpResult::failure(q.lastError().text());
            if (q.next())
                freeComputer = q.value(0).toString();
        }
        if (!freeComputer.isEmpty()) {
            const qint64 sessionId = startSessionWithin(db, studentId, freeComputer, now, &error);
            if (sessionId < 0)
                return OpResult::failure(error);
            if (!insertImmediateRequest(db, studentId, purpose, expectedMinutes, sessionId, now, nullptr, &error))
                return OpResult::failure(error);
            if (!tx.commit())
                return OpResult::failure(QStringLiteral("Commit failed: ") + tx.lastError());
            return OpResult::success(QStringLiteral("A computer was free, so you did not have to wait: you are now "
                                                    "using %1.")
                                         .arg(freeComputer),
                                     sessionId);
        }
    }

    qint64 requestId = -1;
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO lab_request (student_id, purpose, priority, expected_minutes,"
                                 " requested_at, status) VALUES (?, ?, ?, ?, ?, 'WAITING')"));
        q.addBindValue(studentId);
        q.addBindValue(QString::fromLatin1(lab::purposeCode(purpose)));
        q.addBindValue(lab::priorityFor(purpose));
        q.addBindValue(expectedMinutes);
        q.addBindValue(now);
        if (!q.exec())
            return OpResult::failure(friendlySqlError(q.lastError()));
        requestId = q.lastInsertId().toLongLong();
    }
    if (!tx.commit())
        return OpResult::failure(QStringLiteral("Commit failed: ") + tx.lastError());

    int length = 0;
    const auto entry = waitingEntryFor(studentId, &length);
    const QString policyText = QString::fromStdString(lab::policyName(policy()));
    return OpResult::success(QStringLiteral("You joined the queue at %1 (request #%2). You are number %3 of %4 under "
                                            "the %5 policy; the next free computer is assigned automatically.")
                                 .arg(timeutil::toDisplay(now))
                                 .arg(requestId)
                                 .arg(entry ? entry->position : 0)
                                 .arg(length)
                                 .arg(policyText),
                             requestId);
}

OpResult QueueService::cancelWaiting(const QString& studentId) const
{
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("UPDATE lab_request SET status = 'CANCELLED', cancelled_at = ? "
                             "WHERE student_id = ? AND status = 'WAITING'"));
    q.addBindValue(timeutil::nowUtc());
    q.addBindValue(studentId.trimmed());
    if (!q.exec())
        return OpResult::failure(friendlySqlError(q.lastError()));
    if (q.numRowsAffected() != 1)
        return OpResult::failure(QStringLiteral("You are not waiting in the queue."));
    return OpResult::success(QStringLiteral("You left the queue. The request is kept as cancelled history."));
}

OpResult QueueService::cancelRequest(qint64 requestId) const
{
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("UPDATE lab_request SET status = 'CANCELLED', cancelled_at = ? "
                             "WHERE request_id = ? AND status = 'WAITING'"));
    q.addBindValue(timeutil::nowUtc());
    q.addBindValue(requestId);
    if (!q.exec())
        return OpResult::failure(friendlySqlError(q.lastError()));
    if (q.numRowsAffected() != 1)
        return OpResult::failure(QStringLiteral("Request #%1 is not waiting any more.").arg(requestId));
    return OpResult::success(QStringLiteral("Request #%1 removed from the queue.").arg(requestId));
}

bool QueueService::waitingQueue(std::vector<QueueEntry>& out, QString* error) const
{
    out.clear();
    const QSqlDatabase db = m_db.connection();
    QString policyError;
    const lab::Policy policy = readPolicy(db, &policyError);
    if (!policyError.isEmpty()) {
        if (error)
            *error = policyError;
        return false;
    }
    std::vector<QueueEntry> rows;
    std::vector<lab::WaitingRequest> view;
    if (!loadWaiting(db, false, rows, view, error))
        return false;
    const auto order = lab::dispatchOrder(policy, view);
    int position = 1;
    for (std::size_t index : order) {
        QueueEntry e = rows[index];
        e.position = position++;
        out.push_back(e);
    }
    return true;
}

std::optional<QueueEntry> QueueService::waitingEntryFor(const QString& studentId, int* queueLength,
                                                        QString* error) const
{
    std::vector<QueueEntry> queue;
    if (!waitingQueue(queue, error))
        return std::nullopt;
    if (queueLength)
        *queueLength = static_cast<int>(queue.size());
    for (const QueueEntry& e : queue)
        if (e.studentId == studentId.trimmed())
            return e;
    return std::nullopt;
}

bool QueueService::history(std::vector<RequestRecord>& out, QString* error) const
{
    out.clear();
    QSqlQuery q(m_db.connection());
    if (!q.exec(QStringLiteral(
            "SELECT r.request_id, r.student_id, st.name, r.purpose, r.priority, r.expected_minutes, r.requested_at,"
            "       r.status, r.granted_by, r.allocated_at, r.cancelled_at, r.session_id,"
            "       s.computer_id, s.start_time, s.end_time "
            "FROM lab_request r JOIN student st ON st.student_id = r.student_id "
            "LEFT JOIN session s ON s.session_id = r.session_id "
            "ORDER BY r.request_id DESC LIMIT 5000"))) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    auto text = [&q](int column) { return q.value(column).isNull() ? QString() : q.value(column).toString(); };
    while (q.next()) {
        RequestRecord r;
        r.requestId = q.value(0).toLongLong();
        r.studentId = text(1);
        r.studentName = text(2);
        r.purpose = text(3);
        r.priority = q.value(4).toInt();
        r.expectedMinutes = q.value(5).toInt();
        r.requestedAt = text(6);
        r.status = text(7);
        r.grantedBy = text(8);
        r.allocatedAt = text(9);
        r.cancelledAt = text(10);
        r.sessionId = q.value(11).isNull() ? 0 : q.value(11).toLongLong();
        r.computerId = text(12);
        r.startTime = text(13);
        r.endTime = text(14);
        out.push_back(r);
    }
    return true;
}

}  // namespace labdesk
