#include "sessionservice.h"

#include "database.h"
#include "timeutil.h"

#include <QSqlQuery>
#include <QVariant>

namespace labdesk {

namespace {

const QString kSessionSelect = QStringLiteral(
    "SELECT s.session_id, s.student_id, st.name, s.computer_id, c.computer_name, c.location,"
    "       s.start_time, s.end_time, s.status "
    "FROM session s "
    "JOIN student st ON st.student_id = s.student_id "
    "JOIN computer c ON c.computer_id = s.computer_id ");

SessionRecord readSession(const QSqlQuery& q)
{
    SessionRecord r;
    r.sessionId = q.value(0).toLongLong();
    r.studentId = q.value(1).toString();
    r.studentName = q.value(2).toString();
    r.computerId = q.value(3).toString();
    r.computerName = q.value(4).toString();
    r.location = q.value(5).toString();
    r.startTime = q.value(6).toString();
    r.endTime = q.value(7).isNull() ? QString() : q.value(7).toString();
    r.status = q.value(8).toString();
    return r;
}

// Small helper so every workflow logs, rolls back and reports in one place.
class Workflow {
public:
    Workflow(const QSqlDatabase& db, const StepLogger& log) : m_log(log), m_tx(db)
    {
        step(QStringLiteral("BEGIN IMMEDIATE TRANSACTION"));
        if (!m_tx.isActive())
            step(QStringLiteral("  ✗ could not begin: ") + m_tx.lastError());
    }

    bool started() const { return m_tx.isActive(); }
    QString beginError() const { return m_tx.lastError(); }

    void step(const QString& line) const
    {
        if (m_log)
            m_log(line);
    }

    OpResult fail(const QString& reason)
    {
        step(QStringLiteral("  ✗ ") + reason);
        step(QStringLiteral("ROLLBACK  -- every change made in this transaction is undone"));
        m_tx.rollback();
        return OpResult::failure(reason);
    }

    OpResult commit(const QString& successMessage, qint64 id)
    {
        if (!m_tx.commit())
            return fail(QStringLiteral("COMMIT failed: ") + m_tx.lastError());
        step(QStringLiteral("COMMIT  -- all changes are now durable"));
        return OpResult::success(successMessage, id);
    }

private:
    const StepLogger& m_log;
    Transaction m_tx;
};

}  // namespace

OpResult SessionService::allocateComputer(const QString& studentIdIn, const QString& computerIdIn,
                                          const StepLogger& log) const
{
    const QString studentId = studentIdIn.trimmed();
    const QString computerId = computerIdIn.trimmed();
    if (studentId.isEmpty() || computerId.isEmpty())
        return OpResult::failure(QStringLiteral("Choose both a student and a computer."));

    QSqlDatabase db = m_db.connection();
    Workflow wf(db, log);
    if (!wf.started())
        return OpResult::failure(QStringLiteral("Could not start transaction: ") + wf.beginError());

    // 1. Student exists
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT name FROM student WHERE student_id = ?"));
        q.addBindValue(studentId);
        wf.step(QStringLiteral("SELECT name FROM student WHERE student_id = '%1'").arg(studentId));
        if (!q.exec())
            return wf.fail(QStringLiteral("Could not verify the student: ") + q.lastError().text());
        if (!q.next())
            return wf.fail(QStringLiteral("Student %1 does not exist.").arg(studentId));
        wf.step(QStringLiteral("  ✓ student found: ") + q.value(0).toString());
    }

    // 2. Student has no active session
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT session_id, computer_id FROM session "
                                 "WHERE student_id = ? AND status = 'ACTIVE'"));
        q.addBindValue(studentId);
        wf.step(QStringLiteral("SELECT ... FROM session WHERE student_id = '%1' AND status = 'ACTIVE'").arg(studentId));
        if (!q.exec())
            return wf.fail(QStringLiteral("Could not check active sessions: ") + q.lastError().text());
        if (q.next()) {
            const QString reason = QStringLiteral("Student %1 already has an active session on %2. End it first.")
                                       .arg(studentId, q.value(1).toString());
            q.finish();  // release the read cursor before ROLLBACK
            return wf.fail(reason);
        }
        wf.step(QStringLiteral("  ✓ student has no active session"));
    }

    // 3. Computer exists and is AVAILABLE (re-checked inside the transaction)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT status FROM computer WHERE computer_id = ?"));
        q.addBindValue(computerId);
        wf.step(QStringLiteral("SELECT status FROM computer WHERE computer_id = '%1'").arg(computerId));
        if (!q.exec())
            return wf.fail(QStringLiteral("Could not read computer status: ") + q.lastError().text());
        if (!q.next())
            return wf.fail(QStringLiteral("Computer %1 does not exist.").arg(computerId));
        const QString status = q.value(0).toString();
        q.finish();
        if (status != ComputerStatus::Available)
            return wf.fail(QStringLiteral("Computer %1 is %2, not AVAILABLE.").arg(computerId, status));
        wf.step(QStringLiteral("  ✓ computer is AVAILABLE"));
    }

    // 4. Mark computer OCCUPIED (guarded update: affects 0 rows if status changed)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE computer SET status = 'OCCUPIED' "
                                 "WHERE computer_id = ? AND status = 'AVAILABLE'"));
        q.addBindValue(computerId);
        wf.step(QStringLiteral("UPDATE computer SET status = 'OCCUPIED' WHERE computer_id = '%1' "
                               "AND status = 'AVAILABLE'")
                    .arg(computerId));
        if (!q.exec())
            return wf.fail(friendlySqlError(q.lastError()));
        if (q.numRowsAffected() != 1)
            return wf.fail(QStringLiteral("Computer %1 was taken by another request.").arg(computerId));
        wf.step(QStringLiteral("  ✓ 1 row updated (not yet committed)"));
    }

    // 5. Insert ACTIVE session
    qint64 sessionId = -1;
    const QString startTime = timeutil::nowUtc();
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO session (student_id, computer_id, start_time, status) "
                                 "VALUES (?, ?, ?, 'ACTIVE')"));
        q.addBindValue(studentId);
        q.addBindValue(computerId);
        q.addBindValue(startTime);
        wf.step(QStringLiteral("INSERT INTO session (student_id, computer_id, start_time, status) "
                               "VALUES ('%1', '%2', '%3', 'ACTIVE')")
                    .arg(studentId, computerId, startTime));
        if (!q.exec())
            return wf.fail(QStringLiteral("Could not create the session: ") + friendlySqlError(q.lastError()));
        sessionId = q.lastInsertId().toLongLong();
        wf.step(QStringLiteral("  ✓ session #%1 inserted (not yet committed)").arg(sessionId));
    }

    return wf.commit(QStringLiteral("Computer %1 allocated to %2 (session #%3).")
                         .arg(computerId, studentId)
                         .arg(sessionId),
                     sessionId);
}

OpResult SessionService::endActiveSession(const QString& studentId, const StepLogger& log) const
{
    return endSession(std::nullopt, studentId.trimmed(), log);
}

OpResult SessionService::endSessionById(qint64 sessionId, const StepLogger& log) const
{
    return endSession(sessionId, QString(), log);
}

OpResult SessionService::endSession(std::optional<qint64> sessionIdIn, const QString& studentId,
                                    const StepLogger& log) const
{
    if (!sessionIdIn && studentId.isEmpty())
        return OpResult::failure(QStringLiteral("No student or session was specified."));

    QSqlDatabase db = m_db.connection();
    Workflow wf(db, log);
    if (!wf.started())
        return OpResult::failure(QStringLiteral("Could not start transaction: ") + wf.beginError());

    // 1. Find the active session
    qint64 sessionId = -1;
    QString computerId;
    QString owner;
    {
        QSqlQuery q(db);
        if (sessionIdIn) {
            q.prepare(QStringLiteral("SELECT session_id, computer_id, student_id FROM session "
                                     "WHERE session_id = ? AND status = 'ACTIVE'"));
            q.addBindValue(*sessionIdIn);
            wf.step(QStringLiteral("SELECT ... FROM session WHERE session_id = %1 AND status = 'ACTIVE'")
                        .arg(*sessionIdIn));
        } else {
            q.prepare(QStringLiteral("SELECT session_id, computer_id, student_id FROM session "
                                     "WHERE student_id = ? AND status = 'ACTIVE'"));
            q.addBindValue(studentId);
            wf.step(QStringLiteral("SELECT ... FROM session WHERE student_id = '%1' AND status = 'ACTIVE'")
                        .arg(studentId));
        }
        if (!q.exec())
            return wf.fail(QStringLiteral("Could not look up the session: ") + q.lastError().text());
        if (!q.next())
            return wf.fail(QStringLiteral("There is no active session to end."));
        sessionId = q.value(0).toLongLong();
        computerId = q.value(1).toString();
        owner = q.value(2).toString();
        wf.step(QStringLiteral("  ✓ found session #%1 on %2").arg(sessionId).arg(computerId));
    }

    // 2. Record end time and mark ENDED (row stays as history)
    const QString endTime = timeutil::nowUtc();
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE session SET end_time = ?, status = 'ENDED' "
                                 "WHERE session_id = ? AND status = 'ACTIVE'"));
        q.addBindValue(endTime);
        q.addBindValue(sessionId);
        wf.step(QStringLiteral("UPDATE session SET end_time = '%1', status = 'ENDED' WHERE session_id = %2")
                    .arg(endTime)
                    .arg(sessionId));
        if (!q.exec())
            return wf.fail(friendlySqlError(q.lastError()));
        if (q.numRowsAffected() != 1)
            return wf.fail(QStringLiteral("Session #%1 was already ended.").arg(sessionId));
        wf.step(QStringLiteral("  ✓ session marked ENDED (not yet committed)"));
    }

    // 3. Release the computer
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE computer SET status = 'AVAILABLE' "
                                 "WHERE computer_id = ? AND status = 'OCCUPIED'"));
        q.addBindValue(computerId);
        wf.step(QStringLiteral("UPDATE computer SET status = 'AVAILABLE' WHERE computer_id = '%1' "
                               "AND status = 'OCCUPIED'")
                    .arg(computerId));
        if (!q.exec())
            return wf.fail(friendlySqlError(q.lastError()));
        if (q.numRowsAffected() != 1)
            return wf.fail(QStringLiteral("Computer %1 was not OCCUPIED; data is inconsistent.").arg(computerId));
        wf.step(QStringLiteral("  ✓ computer released (not yet committed)"));
    }

    return wf.commit(QStringLiteral("Session #%1 ended for %2; %3 is AVAILABLE again.")
                         .arg(sessionId)
                         .arg(owner, computerId),
                     sessionId);
}

std::optional<SessionRecord> SessionService::activeSessionForStudent(const QString& studentId, QString* error) const
{
    QSqlQuery q(m_db.connection());
    q.prepare(kSessionSelect + QStringLiteral("WHERE s.student_id = ? AND s.status = 'ACTIVE'"));
    q.addBindValue(studentId.trimmed());
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return readSession(q);
}

std::vector<SessionRecord> SessionService::listSessions(bool activeOnly, int limit, QString* error) const
{
    std::vector<SessionRecord> out;
    QSqlQuery q(m_db.connection());
    q.prepare(kSessionSelect + (activeOnly ? QStringLiteral("WHERE s.status = 'ACTIVE' ") : QString())
              + QStringLiteral("ORDER BY s.session_id DESC LIMIT ?"));
    q.addBindValue(qMax(1, limit));
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return out;
    }
    while (q.next())
        out.push_back(readSession(q));
    return out;
}

}  // namespace labdesk
