#pragma once
// Bundles the services so the GUI receives one reference instead of five.
// None of them owns the database; they all share the single connection.

#include "adminservice.h"
#include "computerservice.h"
#include "dbmsservice.h"
#include "maintenanceservice.h"
#include "processservice.h"
#include "queueservice.h"
#include "reportservice.h"
#include "sessionservice.h"
#include "studentservice.h"

namespace labdesk {

class Database;

struct Services {
    explicit Services(Database& db)
        : students(db), computers(db), sessions(db), maintenance(db), processes(db), queue(db), admins(db), reports(db),
          dbms(db)
    {
    }

    StudentService students;
    ComputerService computers;
    SessionService sessions;
    MaintenanceService maintenance;
    ProcessService processes;
    QueueService queue;
    AdminService admins;
    ReportService reports;
    DbmsService dbms;
};

}  // namespace labdesk
