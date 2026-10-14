#pragma once
// Persists scheduler workloads in the PROCESS table. A workload is either
// unlinked (session_id NULL, the demo set) or linked to one lab session.

#include "models.h"
#include "opresult.h"

#include <QHash>

#include <optional>
#include <vector>

namespace labdesk {

class Database;

class ProcessService {
public:
    explicit ProcessService(Database& db) : m_db(db) {}

    std::vector<StoredProcess> loadWorkload(std::optional<qint64> sessionId, QString* error = nullptr) const;

    // Number of saved processes per workload key: session id, or kUnlinkedKey
    // for the unlinked demo workload. Returns false if the query fails.
    static constexpr qint64 kUnlinkedKey = -1;
    bool workloadSizes(QHash<qint64, int>& out, QString* error = nullptr) const;

    // Atomically replaces the workload for that key (DELETE + INSERTs in one
    // transaction): either the whole new set is stored or the old one remains.
    OpResult saveWorkload(std::optional<qint64> sessionId, const std::vector<StoredProcess>& processes) const;

private:
    Database& m_db;
};

}  // namespace labdesk
