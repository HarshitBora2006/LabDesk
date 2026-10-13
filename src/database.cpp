#include "database.h"

#include "passwordhasher.h"
#include "timeutil.h"

#include <QDir>
#include <QFile>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QVariant>

#include <utility>

namespace labdesk {

// ------------------------------------------------------------------ Database

Database::Database(QString filePath, QString connectionName)
    : m_filePath(std::move(filePath)), m_connectionName(std::move(connectionName))
{
}

Database::~Database()
{
    close();
}

QString Database::defaultFilePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QDir::currentPath();
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("labdesk.db"));
}

QSqlDatabase Database::connection() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

bool Database::isOpen() const
{
    return QSqlDatabase::contains(m_connectionName) && connection().isOpen();
}

void Database::close()
{
    if (!QSqlDatabase::contains(m_connectionName))
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isOpen())
            db.close();
    }  // the handle must be released before removeDatabase()
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool Database::exec(const QString& sql, QString* error)
{
    QSqlQuery q(connection());
    if (!q.exec(sql)) {
        if (error)
            *error = q.lastError().text() + QStringLiteral("\nSQL: ") + sql.left(300);
        return false;
    }
    return true;
}

bool Database::open(QString* error)
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        if (error)
            *error = QStringLiteral("The Qt SQLite driver (QSQLITE) is not available. "
                                    "Check that the Qt SQL plugins are deployed with the application.");
        return false;
    }
    if (QSqlDatabase::contains(m_connectionName))
        close();

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_filePath);
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));
    if (!db.open()) {
        if (error)
            *error = QStringLiteral("Could not open database '%1': %2").arg(m_filePath, db.lastError().text());
        return false;
    }

    // Foreign keys are OFF by default in SQLite and must be enabled per
    // connection, outside any transaction. Verify instead of assuming.
    if (!exec(QStringLiteral("PRAGMA foreign_keys = ON"), error))
        return false;
    {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("PRAGMA foreign_keys")) || !q.next() || q.value(0).toInt() != 1) {
            if (error)
                *error = QStringLiteral("SQLite foreign-key enforcement could not be enabled.");
            return false;
        }
    }
    return applySchema(error);
}

bool Database::applySchema(QString* error)
{
    QFile file(QStringLiteral(":/sql/schema.sql"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Embedded schema resource :/sql/schema.sql is missing.");
        return false;
    }
    const QString text = QString::fromUtf8(file.readAll());

    // Split on separator lines "-- @@" and drop full-line comments.
    QStringList statements;
    QStringList current;
    const QStringList lines = text.split(QLatin1Char('\n'));
    auto flush = [&]() {
        QStringList body;
        for (const QString& line : current) {
            if (!line.trimmed().startsWith(QLatin1String("--")))
                body << line;
        }
        const QString sql = body.join(QLatin1Char('\n')).trimmed();
        if (!sql.isEmpty())
            statements << sql;
        current.clear();
    };
    for (const QString& line : lines) {
        if (line.trimmed() == QLatin1String("-- @@"))
            flush();
        else
            current << line;
    }
    flush();

    Transaction tx(connection());
    if (!tx.isActive()) {
        if (error)
            *error = QStringLiteral("Could not start schema transaction: ") + tx.lastError();
        return false;
    }
    for (const QString& sql : statements) {
        if (!exec(sql, error))
            return false;  // Transaction destructor rolls back
    }
    if (!exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion), error))
        return false;
    if (!tx.commit()) {
        if (error)
            *error = QStringLiteral("Could not commit schema: ") + tx.lastError();
        return false;
    }
    return true;
}

bool Database::seedDemoData(QString* error)
{
    QSqlDatabase db = connection();
    {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("SELECT (SELECT COUNT(*) FROM student) + (SELECT COUNT(*) FROM computer)"))
            || !q.next()) {
            if (error)
                *error = q.lastError().text();
            return false;
        }
        if (q.value(0).toInt() > 0)
            return true;  // never touch a database that already has data
    }

    Transaction tx(db);
    if (!tx.isActive()) {
        if (error)
            *error = tx.lastError();
        return false;
    }

    struct DemoStudent {
        const char* id;
        const char* name;
        const char* email;
        const char* department;
    };
    const DemoStudent students[] = {
        {"DEMO01", "Ananya Rawat (demo)", "ananya.demo@labdesk.example", "Computer Science"},
        {"DEMO02", "Kabir Negi (demo)", "kabir.demo@labdesk.example", "Computer Science"},
        {"DEMO03", "Meera Bisht (demo)", "meera.demo@labdesk.example", "Electronics"},
        {"DEMO04", "Rohan Joshi (demo)", "rohan.demo@labdesk.example", "Information Technology"},
    };
    QSqlQuery insertStudent(db);
    insertStudent.prepare(QStringLiteral("INSERT INTO student (student_id, name, email, password_hash, department) "
                                         "VALUES (?, ?, ?, ?, ?)"));
    for (const auto& s : students) {
        insertStudent.addBindValue(QString::fromUtf8(s.id));
        insertStudent.addBindValue(QString::fromUtf8(s.name));
        insertStudent.addBindValue(QString::fromUtf8(s.email));
        insertStudent.addBindValue(password::hash(QStringLiteral("demo123")));
        insertStudent.addBindValue(QString::fromUtf8(s.department));
        if (!insertStudent.exec()) {
            if (error)
                *error = insertStudent.lastError().text();
            return false;
        }
    }

    QSqlQuery insertComputer(db);
    insertComputer.prepare(QStringLiteral("INSERT INTO computer (computer_id, computer_name, location, status) "
                                          "VALUES (?, ?, ?, ?)"));
    auto addComputer = [&](const QString& id, const QString& name, const QString& location, const QString& status) {
        insertComputer.addBindValue(id);
        insertComputer.addBindValue(name);
        insertComputer.addBindValue(location);
        insertComputer.addBindValue(status);
        return insertComputer.exec();
    };
    for (int i = 1; i <= 12; ++i) {
        const QString id = QStringLiteral("LAB-A-%1").arg(i, 2, 10, QLatin1Char('0'));
        if (!addComputer(id, QStringLiteral("Workstation A%1").arg(i), QStringLiteral("Lab A, Block 1 (demo)"),
                         QStringLiteral("AVAILABLE"))) {
            if (error)
                *error = insertComputer.lastError().text();
            return false;
        }
    }
    for (int i = 1; i <= 8; ++i) {
        const QString id = QStringLiteral("LAB-B-%1").arg(i, 2, 10, QLatin1Char('0'));
        const QString status = (i == 8) ? QStringLiteral("MAINTENANCE") : QStringLiteral("AVAILABLE");
        if (!addComputer(id, QStringLiteral("Workstation B%1").arg(i), QStringLiteral("Lab B, Block 2 (demo)"),
                         status)) {
            if (error)
                *error = insertComputer.lastError().text();
            return false;
        }
    }

    QSqlQuery maintenance(db);
    maintenance.prepare(QStringLiteral("INSERT INTO maintenance (computer_id, description, reported_at, status) "
                                       "VALUES ('LAB-B-08', ?, ?, 'OPEN')"));
    maintenance.addBindValue(QStringLiteral("Demo record: keyboard not responding"));
    maintenance.addBindValue(timeutil::nowUtc());
    if (!maintenance.exec()) {
        if (error)
            *error = maintenance.lastError().text();
        return false;
    }

    // Unlinked demo workload for the CPU scheduler (textbook set A).
    QSqlQuery process(db);
    process.prepare(QStringLiteral("INSERT INTO process (session_id, label, arrival_time, burst_time, priority) "
                                   "VALUES (NULL, ?, ?, ?, ?)"));
    const int workload[][3] = {{0, 5, 2}, {1, 3, 3}, {2, 8, 1}, {3, 6, 4}};
    for (int i = 0; i < 4; ++i) {
        process.addBindValue(QStringLiteral("P%1").arg(i + 1));
        process.addBindValue(workload[i][0]);
        process.addBindValue(workload[i][1]);
        process.addBindValue(workload[i][2]);
        if (!process.exec()) {
            if (error)
                *error = process.lastError().text();
            return false;
        }
    }

    if (!tx.commit()) {
        if (error)
            *error = tx.lastError();
        return false;
    }
    return true;
}

// --------------------------------------------------------------- Transaction

Transaction::Transaction(const QSqlDatabase& db) : m_db(db)
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("BEGIN IMMEDIATE TRANSACTION")))
        m_active = true;
    else
        m_error = q.lastError().text();
}

Transaction::~Transaction()
{
    if (m_active)
        rollback();
}

bool Transaction::commit()
{
    if (!m_active) {
        m_error = QStringLiteral("No active transaction to commit.");
        return false;
    }
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("COMMIT"))) {
        m_error = q.lastError().text();
        return false;  // still active: caller (or destructor) must roll back
    }
    m_active = false;
    return true;
}

bool Transaction::rollback()
{
    if (!m_active)
        return true;
    QSqlQuery q(m_db);
    const bool ok = q.exec(QStringLiteral("ROLLBACK"));
    if (!ok)
        m_error = q.lastError().text();
    m_active = false;  // SQLite ends the transaction even if ROLLBACK reports an error
    return ok;
}

// ------------------------------------------------------------- FaultInjector

QString FaultInjector::failureMessage()
{
    return QStringLiteral("Injected failure for rollback demonstration");
}

FaultInjector::FaultInjector(const QSqlDatabase& db) : m_db(db)
{
    QSqlQuery q(m_db);
    const QString sql = QStringLiteral(
        "CREATE TEMP TRIGGER IF NOT EXISTS labdesk_injected_failure "
        "BEFORE INSERT ON main.session "
        "BEGIN SELECT RAISE(ABORT, '%1'); END")
                            .arg(failureMessage());
    if (q.exec(sql))
        m_installed = true;
    else
        m_error = q.lastError().text();
}

FaultInjector::~FaultInjector()
{
    if (!m_installed)
        return;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("DROP TRIGGER IF EXISTS temp.labdesk_injected_failure"));
}

// ------------------------------------------------------------------- helpers

QString friendlySqlError(const QSqlError& error)
{
    const QString text = error.databaseText().isEmpty() ? error.text() : error.databaseText();
    if (text.contains(QLatin1String("FOREIGN KEY constraint failed")))
        return QStringLiteral("This record is referenced by other records (foreign-key constraint), "
                              "so the operation was refused.");
    if (text.contains(QLatin1String("UNIQUE constraint failed: student.email")))
        return QStringLiteral("That email address is already registered.");
    if (text.contains(QLatin1String("UNIQUE constraint failed: student.student_id")))
        return QStringLiteral("That student ID already exists.");
    if (text.contains(QLatin1String("UNIQUE constraint failed: computer.computer_id")))
        return QStringLiteral("That computer ID already exists.");
    if (text.contains(QLatin1String("UNIQUE constraint failed: session.computer_id")))
        return QStringLiteral("This computer already has an active session.");
    if (text.contains(QLatin1String("UNIQUE constraint failed: session.student_id")))
        return QStringLiteral("This student already has an active session.");
    if (text.contains(QLatin1String("CHECK constraint failed")))
        return QStringLiteral("A value breaks a database rule (%1).").arg(text);
    return text;  // includes trigger RAISE messages, which are already readable
}

}  // namespace labdesk
