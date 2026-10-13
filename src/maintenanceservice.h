#pragma once
// Maintenance workflows (each one atomic):
//   reportIssue   - refused for OCCUPIED computers; inserts an OPEN record and
//                   moves an AVAILABLE computer to MAINTENANCE.
//   resolveIssue  - marks the record RESOLVED, then releases the computer to
//                   AVAILABLE only if no other OPEN issue and no ACTIVE session remain.

#include "models.h"
#include "opresult.h"

#include <vector>

namespace labdesk {

class Database;

class MaintenanceService {
public:
    explicit MaintenanceService(Database& db) : m_db(db) {}

    std::vector<MaintenanceRecord> listRecords(bool openOnly, QString* error = nullptr) const;
    OpResult reportIssue(const QString& computerId, const QString& description) const;
    OpResult updateDescription(qint64 maintenanceId, const QString& description) const;
    OpResult resolveIssue(qint64 maintenanceId) const;

private:
    Database& m_db;
};

}  // namespace labdesk
