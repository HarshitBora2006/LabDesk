#pragma once
// QueueService: lab requests with REAL timing data.
//
//   startNow   - student takes a specific free computer: the request (arrival)
//                and the session (start) are written in one transaction, so
//                waiting time is measured as 0.
//   joinQueue  - no free computer: a WAITING request records the arrival time.
//                If a computer is in fact free and nobody waits, it is assigned
//                immediately instead.
//   dispatchWithin - runs INSIDE the caller's transaction whenever a computer
//                becomes AVAILABLE (session ended, repair resolved, computer
//                added) and assigns it to the next waiting student chosen by the
//                admin's policy (FCFS / SJF / PRIORITY, see labqueue.h).
//
// Burst time is never entered: it is end_time - start_time of the linked
// session, known only when the session ends.

#include "labqueue.h"
#include "opresult.h"

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace labdesk {

class Database;

// A WAITING request, with its position under the current policy (1 = next).
struct QueueEntry {
    qint64 requestId = 0;
    QString studentId;
    QString studentName;
    QString purpose;  // EXAM / ASSIGNMENT / PRACTICE
    int priority = 3;
    int expectedMinutes = 0;
    QString requestedAt;
    int position = 0;
};

// One request with the timestamps of its session, for measured results.
struct RequestRecord {
    qint64 requestId = 0;
    QString studentId;
    QString studentName;
    QString purpose;
    int priority = 3;
    int expectedMinutes = 0;
    QString requestedAt;
    QString status;  // WAITING / ALLOCATED / CANCELLED
    QString grantedBy;
    QString allocatedAt;
    QString cancelledAt;
    qint64 sessionId = 0;
    QString computerId;
    QString startTime;
    QString endTime;  // empty while the session is running
};

class QueueService {
public:
    explicit QueueService(Database& db) : m_db(db) {}

    lab::Policy policy(QString* error = nullptr) const;
    OpResult setPolicy(lab::Policy policy) const;

    OpResult startNow(const QString& studentId, const QString& computerId, const QString& purposeCode,
                      int expectedMinutes) const;
    OpResult joinQueue(const QString& studentId, const QString& purposeCode, int expectedMinutes) const;
    OpResult cancelWaiting(const QString& studentId) const;  // student leaves the queue
    OpResult cancelRequest(qint64 requestId) const;          // administrator removes a request

    // WAITING requests in the order the current policy will serve them.
    bool waitingQueue(std::vector<QueueEntry>& out, QString* error = nullptr) const;
    // The student's WAITING entry (with position), if any; queueLength = total waiting.
    std::optional<QueueEntry> waitingEntryFor(const QString& studentId, int* queueLength = nullptr,
                                              QString* error = nullptr) const;
    bool history(std::vector<RequestRecord>& out, QString* error = nullptr) const;

    // Assigns every AVAILABLE computer to waiting students by policy, inside the
    // caller's already-open transaction. Returns false on a database error, in
    // which case the caller must roll back. Readable descriptions of the
    // assignments made are appended to `assignments` (may be null).
    static bool dispatchWithin(const QSqlDatabase& db, QStringList* assignments, QString* error);

    // Empty string = valid.
    static QString validateRequest(const QString& purposeCode, int expectedMinutes);

private:
    Database& m_db;
};

}  // namespace labdesk
