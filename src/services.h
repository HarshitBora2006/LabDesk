#pragma once
// Bundles the services so the GUI receives one reference instead of five.
// None of them owns the database; they all share the single connection.

#include "computerservice.h"
#include "maintenanceservice.h"
#include "processservice.h"
#include "sessionservice.h"
#include "studentservice.h"

namespace labdesk {

class Database;

struct Services {
    explicit Services(Database& db)
        : students(db), computers(db), sessions(db), maintenance(db), processes(db)
    {
    }

    StudentService students;
    ComputerService computers;
    SessionService sessions;
    MaintenanceService maintenance;
    ProcessService processes;
};

}  // namespace labdesk
