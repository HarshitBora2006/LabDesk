#pragma once
// SessionService: the two atomic workflows at the heart of the project.
//
// allocateComputer()  BEGIN IMMEDIATE
//                     1. student exists?
//                     2. student has no ACTIVE session?
//                     3. computer exists and is AVAILABLE?   (re-checked here,
//                        never trusted from the GUI)
//                     4. UPDATE computer -> OCCUPIED (guarded: AND status='AVAILABLE')
//                     5. INSERT session (ACTIVE)
//                     COMMIT  -- or ROLLBACK on any failure, so the computer is
//                     never left OCCUPIED without a session and vice versa.
//
// endActiveSession()  BEGIN IMMEDIATE
//                     1. find ACTIVE session  2. set end_time + ENDED
//                     3. UPDATE computer -> AVAILABLE   COMMIT / ROLLBACK
//
// An optional StepLogger receives a line per step (used by the transaction
// demonstration screen and by tests).

#include "models.h"
#include "opresult.h"

#include <functional>
#include <optional>
#include <vector>

namespace labdesk {

class Database;

using StepLogger = std::function<void(const QString&)>;

class SessionService {
public:
    explicit SessionService(Database& db) : m_db(db) {}

    OpResult allocateComputer(const QString& studentId, const QString& computerId,
                              const StepLogger& log = StepLogger()) const;
    OpResult endActiveSession(const QString& studentId, const StepLogger& log = StepLogger()) const;
    OpResult endSessionById(qint64 sessionId, const StepLogger& log = StepLogger()) const;

    // Any session (active or ended) by id, joined with student and computer details.
    std::optional<SessionRecord> findSession(qint64 sessionId, QString* error = nullptr) const;
    std::optional<SessionRecord> activeSessionForStudent(const QString& studentId, QString* error = nullptr) const;
    std::vector<SessionRecord> listSessions(bool activeOnly, int limit = 500, QString* error = nullptr) const;

private:
    // studentId is used when sessionId is empty.
    OpResult endSession(std::optional<qint64> sessionId, const QString& studentId, const StepLogger& log) const;

    Database& m_db;
};

}  // namespace labdesk
