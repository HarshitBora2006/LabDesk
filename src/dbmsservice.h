#pragma once
// Introspection of the REAL application database for the DBMS Management
// module: engine information, tables and row counts, columns, keys, foreign
// keys, triggers, integrity checks and a restricted read-only query runner.
// It never opens a second or demo database.

#include <QString>
#include <QStringList>

#include <utility>
#include <vector>

namespace labdesk {

class Database;

struct TableSummary {
    QString name;
    qint64 rows = 0;
};

struct DatabaseOverview {
    QString engine;          // "SQLite 3.x.y"
    bool connected = false;
    bool foreignKeysOn = false;
    int schemaVersion = 0;   // PRAGMA user_version
    QString journalMode;
    std::vector<TableSummary> tables;
    int indexCount = 0;
    int triggerCount = 0;
};

struct ColumnInfo {
    QString name;
    QString type;
    bool notNull = false;
    bool primaryKey = false;
    QString defaultValue;
};

struct ForeignKeyInfo {
    QString fromTable;
    QString fromColumn;
    QString toTable;
    QString toColumn;
    QString onDelete;
};

struct QueryResult {
    QStringList columns;
    std::vector<QStringList> rows;
    bool truncated = false;
};

class DbmsService {
public:
    explicit DbmsService(Database& db) : m_db(db) {}

    bool overview(DatabaseOverview& out, QString* error = nullptr) const;
    bool columns(const QString& table, std::vector<ColumnInfo>& out, QString* error = nullptr) const;
    bool foreignKeys(const QString& table, std::vector<ForeignKeyInfo>& out, QString* error = nullptr) const;
    bool allForeignKeys(std::vector<ForeignKeyInfo>& out, QString* error = nullptr) const;
    // name -> table, for triggers and indexes defined in the schema
    bool schemaObjects(const QString& type, std::vector<std::pair<QString, QString>>& out,
                       QString* error = nullptr) const;
    // PRAGMA quick_check plus pragma_foreign_key_check; each line describes a finding or "ok".
    bool integrityCheck(QStringList& findings, QString* error = nullptr) const;

    // Runs ONE SELECT / WITH statement with SQLite's query_only mode switched on,
    // so the engine itself refuses any write. Columns whose name contains
    // "password" are masked. At most maxRows rows are returned.
    bool runReadOnly(const QString& sql, QueryResult& out, QString* error, int maxRows = 500) const;
    // Empty = acceptable to run; otherwise why it is refused.
    static QString checkReadOnly(const QString& sql);

private:
    Database& m_db;
};

}  // namespace labdesk
