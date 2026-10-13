#pragma once
// Database: owns one named QSQLITE connection, enables and VERIFIES foreign-key
// enforcement, applies resources/sql/schema.sql, and seeds demo data.
//
// Transaction: RAII guard. BEGIN IMMEDIATE on construction (takes SQLite's
// write lock up-front so the "check then update" steps of a workflow cannot
// interleave with another writer); ROLLBACK automatically if the guard is
// destroyed without a successful commit().
//
// FaultInjector: RAII helper for TEST 12 and the transaction demo. Installs a
// TEMP trigger that makes INSERT INTO session fail, so the allocation
// transaction fails AFTER it has already updated the computer row.

#include <QSqlDatabase>
#include <QSqlError>
#include <QString>

namespace labdesk {

class Database {
public:
    explicit Database(QString filePath, QString connectionName = QStringLiteral("labdesk_main"));
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(QString* error);
    bool isOpen() const;
    void close();

    // Inserts clearly-labelled demo records, only when student and computer
    // tables are both empty. Runs in one transaction.
    bool seedDemoData(QString* error);

    QSqlDatabase connection() const;
    QString filePath() const { return m_filePath; }
    QString connectionName() const { return m_connectionName; }

    static QString defaultFilePath();
    static constexpr int kSchemaVersion = 1;

private:
    bool applySchema(QString* error);
    bool exec(const QString& sql, QString* error);

    QString m_filePath;
    QString m_connectionName;
};

class Transaction {
public:
    explicit Transaction(const QSqlDatabase& db);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    bool isActive() const { return m_active; }
    bool commit();
    bool rollback();
    QString lastError() const { return m_error; }

private:
    QSqlDatabase m_db;
    bool m_active = false;
    QString m_error;
};

class FaultInjector {
public:
    explicit FaultInjector(const QSqlDatabase& db);
    ~FaultInjector();

    FaultInjector(const FaultInjector&) = delete;
    FaultInjector& operator=(const FaultInjector&) = delete;

    bool isInstalled() const { return m_installed; }
    QString lastError() const { return m_error; }

    static QString failureMessage();

private:
    QSqlDatabase m_db;
    bool m_installed = false;
    QString m_error;
};

// Maps raw SQLite constraint messages to text a user can act on.
QString friendlySqlError(const QSqlError& error);

}  // namespace labdesk
