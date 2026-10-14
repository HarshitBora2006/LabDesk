#pragma once
// Computer CRUD and dashboard statistics.
// Status is NOT directly editable: it changes only through the allocation,
// session-end and maintenance workflows, which keep it consistent.

#include "models.h"
#include "opresult.h"

#include <QStringList>

#include <optional>
#include <vector>

namespace labdesk {

class Database;

class ComputerService {
public:
    explicit ComputerService(Database& db) : m_db(db) {}

    // statusFilter empty = all computers.
    std::vector<Computer> listComputers(const QString& statusFilter = QString(), QString* error = nullptr) const;
    std::optional<Computer> findComputer(const QString& computerId, QString* error = nullptr) const;

    OpResult addComputer(const Computer& computer) const;      // always starts AVAILABLE
    OpResult updateComputer(const Computer& computer) const;   // name + location only
    OpResult deleteComputer(const QString& computerId) const;  // refused if history exists

    DashboardStats dashboardStats(QString* error = nullptr) const;

    // One row per laboratory (distinct location) with status counts.
    // Returns false and fills `error` if the query fails.
    bool labSummaries(std::vector<LabSummary>& out, QString* error = nullptr) const;

    // Cross-table invariant checks; each string describes one violation.
    // An empty list means computer statuses agree with sessions and maintenance.
    QStringList consistencyProblems(QString* error = nullptr) const;

    static QString validate(const Computer& computer);

private:
    Database& m_db;
};

}  // namespace labdesk
