// Integration tests for the database layer and services (requires Qt 6 Core + Sql).
// Each test uses a fresh SQLite file in a temporary directory.
// Numbering follows section 12 of the project brief.

#include "database.h"
#include "services.h"

#include <QCoreApplication>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>

#include <iostream>
#include <memory>

using namespace labdesk;

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool condition, const std::string& name)
{
    if (condition) {
        ++g_passed;
        std::cout << "  PASS  " << name << "\n";
    } else {
        ++g_failed;
        std::cout << "  FAIL  " << name << "\n";
    }
}

int scalar(Database& db, const QString& sql, const QVariantList& binds = {})
{
    QSqlQuery q(db.connection());
    q.prepare(sql);
    for (const QVariant& v : binds)
        q.addBindValue(v);
    if (!q.exec() || !q.next())
        return -9999;
    return q.value(0).toInt();
}

QString text(Database& db, const QString& sql, const QVariantList& binds = {})
{
    QSqlQuery q(db.connection());
    q.prepare(sql);
    for (const QVariant& v : binds)
        q.addBindValue(v);
    if (!q.exec() || !q.next())
        return QStringLiteral("<error>");
    return q.value(0).toString();
}

QString computerStatus(Database& db, const QString& id)
{
    return text(db, QStringLiteral("SELECT status FROM computer WHERE computer_id = ?"), {id});
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::cout << "Cannot create temporary directory\n";
        return 2;
    }
    const QString path = dir.filePath(QStringLiteral("labdesk_test.db"));

    auto db = std::make_unique<Database>(path, QStringLiteral("test_connection"));
    QString error;
    check(db->open(&error), "database opens and schema applies");
    if (!error.isEmpty())
        std::cout << "        " << error.toStdString() << "\n";
    check(scalar(*db, QStringLiteral("PRAGMA foreign_keys")) == 1, "foreign keys are enforced");

    auto services = std::make_unique<Services>(*db);
    Services& s = *services;

    std::cout << "TEST 1 - create student\n";
    check(s.students.addStudent({QStringLiteral("S100"), QStringLiteral("Test Student"),
                                 QStringLiteral("test@uni.edu"), QStringLiteral("CSE")},
                                QStringLiteral("secret1"))
              .ok,
          "add student S100");
    check(scalar(*db, QStringLiteral("SELECT COUNT(*) FROM student WHERE student_id = 'S100'")) == 1,
          "S100 is in the database");
    check(text(*db, QStringLiteral("SELECT password_hash FROM student WHERE student_id = 'S100'")) != "secret1",
          "password is not stored in plaintext");
    check(!s.students.addStudent({QStringLiteral("S100"), QStringLiteral("Dup"), QStringLiteral("d@uni.edu"),
                                  QStringLiteral("CSE")},
                                 QStringLiteral("secret1"))
               .ok,
          "duplicate student ID rejected");
    check(!s.students.addStudent({QStringLiteral("S101"), QStringLiteral("Dup"), QStringLiteral("TEST@uni.edu"),
                                  QStringLiteral("CSE")},
                                 QStringLiteral("secret1"))
               .ok,
          "duplicate email (different case) rejected");
    check(!s.students.addStudent({QStringLiteral("S102"), QString(), QStringLiteral("x@uni.edu"),
                                  QStringLiteral("CSE")},
                                 QStringLiteral("secret1"))
               .ok,
          "missing name rejected");
    s.students.addStudent({QStringLiteral("S200"), QStringLiteral("Second Student"), QStringLiteral("two@uni.edu"),
                           QStringLiteral("ECE")},
                          QStringLiteral("secret2"));

    std::cout << "TEST 2 - create computer\n";
    check(s.computers.addComputer({QStringLiteral("PC-01"), QStringLiteral("Dell 1"), QStringLiteral("Lab A"), {}})
              .ok,
          "add PC-01");
    s.computers.addComputer({QStringLiteral("PC-02"), QStringLiteral("Dell 2"), QStringLiteral("Lab A"), {}});
    s.computers.addComputer({QStringLiteral("PC-03"), QStringLiteral("Dell 3"), QStringLiteral("Lab B"), {}});
    check(computerStatus(*db, QStringLiteral("PC-01")) == ComputerStatus::Available, "PC-01 starts AVAILABLE");

    std::cout << "TEST 3/4 - authentication\n";
    check(s.students.authenticate(QStringLiteral("S100"), QStringLiteral("secret1")).has_value(),
          "valid credentials accepted");
    check(!s.students.authenticate(QStringLiteral("S100"), QStringLiteral("wrong")).has_value(),
          "wrong password rejected");
    check(!s.students.authenticate(QStringLiteral("NOPE"), QStringLiteral("secret1")).has_value(),
          "unknown ID rejected");

    std::cout << "TEST 5 - allocate\n";
    OpResult r = s.sessions.allocateComputer(QStringLiteral("S100"), QStringLiteral("PC-01"));
    check(r.ok, "allocate PC-01 to S100");
    check(scalar(*db, QStringLiteral("SELECT COUNT(*) FROM session WHERE status = 'ACTIVE'")) == 1,
          "exactly one active session");
    check(computerStatus(*db, QStringLiteral("PC-01")) == ComputerStatus::Occupied, "PC-01 is OCCUPIED");
    check(text(*db, QStringLiteral("SELECT student_id FROM session WHERE session_id = ?"), {r.id}) == "S100",
          "session belongs to S100");
    const qint64 firstSession = r.id;

    std::cout << "TEST 6 - occupied computer\n";
    r = s.sessions.allocateComputer(QStringLiteral("S200"), QStringLiteral("PC-01"));
    check(!r.ok, "second allocation of PC-01 rejected");
    check(scalar(*db, QStringLiteral("SELECT COUNT(*) FROM session")) == 1, "no extra session row");
    check(computerStatus(*db, QStringLiteral("PC-01")) == ComputerStatus::Occupied, "PC-01 still OCCUPIED");

    std::cout << "TEST 7 - one session per student\n";
    r = s.sessions.allocateComputer(QStringLiteral("S100"), QStringLiteral("PC-02"));
    check(!r.ok, "S100 cannot take PC-02 while active");
    check(computerStatus(*db, QStringLiteral("PC-02")) == ComputerStatus::Available, "PC-02 untouched");

    std::cout << "TEST 10 (guard) - maintenance on occupied computer\n";
    check(!s.maintenance.reportIssue(QStringLiteral("PC-01"), QStringLiteral("fan noise")).ok,
          "cannot report maintenance on OCCUPIED computer");

    std::cout << "TEST 8 - end session\n";
    r = s.sessions.endActiveSession(QStringLiteral("S100"));
    check(r.ok, "end S100 session");
    check(text(*db, QStringLiteral("SELECT status FROM session WHERE session_id = ?"), {firstSession}) == "ENDED",
          "session ENDED");
    check(!text(*db, QStringLiteral("SELECT end_time FROM session WHERE session_id = ?"), {firstSession}).isEmpty(),
          "end_time recorded");
    check(computerStatus(*db, QStringLiteral("PC-01")) == ComputerStatus::Available, "PC-01 AVAILABLE again");
    check(scalar(*db, QStringLiteral("SELECT COUNT(*) FROM session")) == 1, "history row retained");
    check(!s.sessions.endActiveSession(QStringLiteral("S100")).ok, "ending twice is rejected");

    std::cout << "TEST 9 - delete with history\n";
    r = s.computers.deleteComputer(QStringLiteral("PC-01"));
    check(!r.ok, "service refuses to delete PC-01 (has history)");
    {
        QSqlQuery raw(db->connection());
        const bool deleted = raw.exec(QStringLiteral("DELETE FROM computer WHERE computer_id = 'PC-01'"));
        check(!deleted && raw.lastError().text().contains(QStringLiteral("FOREIGN KEY")),
              "raw DELETE blocked by FOREIGN KEY constraint");
    }
    check(scalar(*db, QStringLiteral("SELECT COUNT(*) FROM computer WHERE computer_id = 'PC-01'")) == 1,
          "PC-01 still exists");
    check(!s.students.deleteStudent(QStringLiteral("S100")).ok, "student with history cannot be deleted");
    check(s.computers.deleteComputer(QStringLiteral("PC-03")).ok, "computer without history can be deleted");

    std::cout << "TEST 10 - maintenance\n";
    r = s.maintenance.reportIssue(QStringLiteral("PC-02"), QStringLiteral("Monitor flickers"));
    check(r.ok, "report issue on AVAILABLE PC-02");
    check(computerStatus(*db, QStringLiteral("PC-02")) == ComputerStatus::Maintenance, "PC-02 -> MAINTENANCE");
    check(!s.sessions.allocateComputer(QStringLiteral("S200"), QStringLiteral("PC-02")).ok,
          "computer under maintenance cannot be allocated");
    const qint64 issue1 = r.id;
    const qint64 issue2 = s.maintenance.reportIssue(QStringLiteral("PC-02"), QStringLiteral("Mouse broken")).id;
    check(s.maintenance.resolveIssue(issue1).ok, "resolve first issue");
    check(computerStatus(*db, QStringLiteral("PC-02")) == ComputerStatus::Maintenance,
          "still MAINTENANCE while another issue is open");
    check(s.maintenance.resolveIssue(issue2).ok, "resolve second issue");
    check(computerStatus(*db, QStringLiteral("PC-02")) == ComputerStatus::Available,
          "released to AVAILABLE when no open issue remains");

    std::cout << "TEST 12 - rollback after a mid-transaction failure\n";
    {
        QStringList log;
        FaultInjector fault(db->connection());
        check(fault.isInstalled(), "fault injector installed");
        r = s.sessions.allocateComputer(QStringLiteral("S200"), QStringLiteral("PC-02"),
                                        [&log](const QString& line) { log << line; });
        check(!r.ok && r.message.contains(FaultInjector::failureMessage()), "allocation fails at INSERT step");
        check(log.join(QLatin1Char('\n')).contains(QStringLiteral("UPDATE computer SET status = 'OCCUPIED'")),
              "the UPDATE had already run before the failure");
        check(log.last().startsWith(QStringLiteral("ROLLBACK")), "workflow ended with ROLLBACK");
    }
    check(computerStatus(*db, QStringLiteral("PC-02")) == ComputerStatus::Available,
          "PC-02 is NOT left OCCUPIED (partial update rolled back)");
    check(scalar(*db, QStringLiteral("SELECT COUNT(*) FROM session WHERE student_id = 'S200'")) == 0,
          "no session row was created");
    check(s.sessions.allocateComputer(QStringLiteral("S200"), QStringLiteral("PC-02")).ok,
          "same allocation succeeds after the fault is removed");

    std::cout << "TEST 14 - shared state seen by admin and student queries\n";
    const DashboardStats stats = s.computers.dashboardStats();
    const auto active = s.sessions.activeSessionForStudent(QStringLiteral("S200"));
    check(active.has_value() && active->computerId == "PC-02", "student view: S200 on PC-02");
    check(stats.occupied == 1 && stats.activeSessions == 1, "admin view: 1 occupied, 1 active session");
    check(s.computers.consistencyProblems().isEmpty(), "no consistency problems");

    std::cout << "Process workload persistence\n";
    std::vector<StoredProcess> workload(2);
    workload[0].label = QStringLiteral("P1");
    workload[0].burst = 4;
    workload[1].label = QStringLiteral("P2");
    workload[1].arrival = 1;
    workload[1].burst = 0;  // invalid: CHECK burst_time > 0
    check(!s.processes.saveWorkload(std::nullopt, workload).ok, "invalid workload rejected");
    check(scalar(*db, QStringLiteral("SELECT COUNT(*) FROM process")) == 0, "nothing partially saved");
    workload[1].burst = 2;
    check(s.processes.saveWorkload(active->sessionId, workload).ok, "workload linked to session saved");
    check(s.processes.loadWorkload(active->sessionId).size() == 2, "workload loads back");

    std::cout << "TEST 13 - persistence after restart\n";
    const int sessionsBefore = scalar(*db, QStringLiteral("SELECT COUNT(*) FROM session"));
    services.reset();
    db.reset();  // closes and removes the connection
    db = std::make_unique<Database>(path, QStringLiteral("test_connection_2"));
    check(db->open(&error), "database reopens");
    check(scalar(*db, QStringLiteral("SELECT COUNT(*) FROM session")) == sessionsBefore, "sessions persisted");
    check(computerStatus(*db, QStringLiteral("PC-02")) == ComputerStatus::Occupied, "computer status persisted");
    check(scalar(*db, QStringLiteral("PRAGMA user_version")) == Database::kSchemaVersion, "schema version stored");
    db.reset();

    std::cout << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
