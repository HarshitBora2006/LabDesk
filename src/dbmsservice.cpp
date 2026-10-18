#include "dbmsservice.h"

#include "database.h"

#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>

#include <utility>

namespace labdesk {

namespace {
QString quoted(const QString& identifier)
{
    QString escaped = identifier;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}
}  // namespace

bool DbmsService::overview(DatabaseOverview& out, QString* error) const
{
    out = DatabaseOverview();
    const QSqlDatabase db = m_db.connection();
    out.connected = db.isOpen();
    if (!out.connected) {
        if (error)
            *error = QStringLiteral("The database connection is not open.");
        return false;
    }
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT sqlite_version()")) || !q.next()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    out.engine = QStringLiteral("SQLite ") + q.value(0).toString();
    if (q.exec(QStringLiteral("PRAGMA foreign_keys")) && q.next())
        out.foreignKeysOn = q.value(0).toInt() == 1;
    if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next())
        out.schemaVersion = q.value(0).toInt();
    if (q.exec(QStringLiteral("PRAGMA journal_mode")) && q.next())
        out.journalMode = q.value(0).toString();
    if (!q.exec(QStringLiteral("SELECT type, name FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' "
                               "ORDER BY type, name"))) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    QStringList tables;
    while (q.next()) {
        const QString type = q.value(0).toString();
        if (type == QStringLiteral("table"))
            tables << q.value(1).toString();
        else if (type == QStringLiteral("index"))
            ++out.indexCount;
        else if (type == QStringLiteral("trigger"))
            ++out.triggerCount;
    }
    for (const QString& table : tables) {
        QSqlQuery c(db);
        // Identifiers cannot be bound as parameters; the name comes from sqlite_master and is quoted.
        if (!c.exec(QStringLiteral("SELECT COUNT(*) FROM ") + quoted(table)) || !c.next()) {
            if (error)
                *error = c.lastError().text();
            return false;
        }
        out.tables.push_back({table, c.value(0).toLongLong()});
    }
    return true;
}

bool DbmsService::columns(const QString& table, std::vector<ColumnInfo>& out, QString* error) const
{
    out.clear();
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("SELECT name, type, \"notnull\", dflt_value, pk FROM pragma_table_info(?) ORDER BY cid"));
    q.addBindValue(table);
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    while (q.next()) {
        ColumnInfo c;
        c.name = q.value(0).toString();
        c.type = q.value(1).toString();
        c.notNull = q.value(2).toInt() != 0;
        c.defaultValue = q.value(3).isNull() ? QString() : q.value(3).toString();
        c.primaryKey = q.value(4).toInt() > 0;
        out.push_back(c);
    }
    return true;
}

bool DbmsService::foreignKeys(const QString& table, std::vector<ForeignKeyInfo>& out, QString* error) const
{
    out.clear();
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("SELECT \"from\", \"table\", \"to\", on_delete FROM pragma_foreign_key_list(?) "
                             "ORDER BY id, seq"));
    q.addBindValue(table);
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    while (q.next())
        out.push_back({table, q.value(0).toString(), q.value(1).toString(), q.value(2).toString(),
                       q.value(3).toString()});
    return true;
}

bool DbmsService::allForeignKeys(std::vector<ForeignKeyInfo>& out, QString* error) const
{
    out.clear();
    DatabaseOverview o;
    if (!overview(o, error))
        return false;
    for (const TableSummary& t : o.tables) {
        std::vector<ForeignKeyInfo> keys;
        if (!foreignKeys(t.name, keys, error))
            return false;
        out.insert(out.end(), keys.begin(), keys.end());
    }
    return true;
}

bool DbmsService::schemaObjects(const QString& type, std::vector<std::pair<QString, QString>>& out,
                                QString* error) const
{
    out.clear();
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("SELECT name, tbl_name FROM sqlite_master WHERE type = ? AND name NOT LIKE 'sqlite_%' "
                             "ORDER BY tbl_name, name"));
    q.addBindValue(type);
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    while (q.next())
        out.emplace_back(q.value(0).toString(), q.value(1).toString());
    return true;
}

bool DbmsService::integrityCheck(QStringList& findings, QString* error) const
{
    findings.clear();
    QSqlQuery q(m_db.connection());
    if (!q.exec(QStringLiteral("PRAGMA quick_check"))) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    while (q.next())
        findings << QStringLiteral("Structure check (PRAGMA quick_check): ") + q.value(0).toString();
    if (!q.exec(QStringLiteral("SELECT \"table\", rowid, parent FROM pragma_foreign_key_check"))) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    int violations = 0;
    while (q.next()) {
        ++violations;
        findings << QStringLiteral("Foreign-key violation: %1 row %2 refers to a missing %3 row")
                        .arg(q.value(0).toString(), q.value(1).toString(), q.value(2).toString());
    }
    if (violations == 0)
        findings << QStringLiteral("Foreign-key check (pragma_foreign_key_check): no orphaned rows");
    return true;
}

QString DbmsService::checkReadOnly(const QString& sqlIn)
{
    QString sql = sqlIn.trimmed();
    while (sql.endsWith(QLatin1Char(';')))
        sql = sql.left(sql.size() - 1).trimmed();
    if (sql.isEmpty())
        return QStringLiteral("Type a SELECT query.");
    if (sql.contains(QLatin1Char(';')))
        return QStringLiteral("Run one statement at a time.");
    const QString lower = sql.toLower();
    if (!lower.startsWith(QStringLiteral("select")) && !lower.startsWith(QStringLiteral("with")))
        return QStringLiteral("Only read-only SELECT (or WITH ... SELECT) queries are allowed here.");
    return QString();
}

bool DbmsService::runReadOnly(const QString& sqlIn, QueryResult& out, QString* error, int maxRows) const
{
    out = QueryResult();
    const QString refusal = checkReadOnly(sqlIn);
    if (!refusal.isEmpty()) {
        if (error)
            *error = refusal;
        return false;
    }
    QString sql = sqlIn.trimmed();
    while (sql.endsWith(QLatin1Char(';')))
        sql = sql.left(sql.size() - 1).trimmed();

    QSqlDatabase db = m_db.connection();
    QSqlQuery guard(db);
    if (!guard.exec(QStringLiteral("PRAGMA query_only = ON"))) {
        if (error)
            *error = guard.lastError().text();
        return false;
    }
    bool ok = false;
    {
        QSqlQuery q(db);
        q.setForwardOnly(true);
        if (!q.exec(sql)) {
            if (error)
                *error = q.lastError().text();
        } else {
            const QSqlRecord record = q.record();
            std::vector<bool> masked;
            for (int c = 0; c < record.count(); ++c) {
                out.columns << record.fieldName(c);
                masked.push_back(record.fieldName(c).contains(QStringLiteral("password"), Qt::CaseInsensitive));
            }
            while (q.next()) {
                if (static_cast<int>(out.rows.size()) >= maxRows) {
                    out.truncated = true;
                    break;
                }
                QStringList row;
                for (int c = 0; c < record.count(); ++c)
                    row << (masked[static_cast<std::size_t>(c)] ? QStringLiteral("(hidden)")
                                                                 : (q.value(c).isNull() ? QStringLiteral("NULL")
                                                                                        : q.value(c).toString()));
                out.rows.push_back(row);
            }
            ok = true;
        }
    }  // the statement is finalised before query_only is switched off
    guard.exec(QStringLiteral("PRAGMA query_only = OFF"));
    return ok;
}

}  // namespace labdesk
