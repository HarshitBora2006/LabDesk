#include "adminservice.h"

#include "database.h"
#include "passwordhasher.h"
#include "timeutil.h"

#include <QRegularExpression>
#include <QSqlQuery>
#include <QVariant>

namespace labdesk {

QString AdminService::validate(const QString& adminId, const QString& name, const QString& password)
{
    static const QRegularExpression idPattern(QStringLiteral("^[A-Za-z0-9_-]{2,20}$"));
    if (!idPattern.match(adminId.trimmed()).hasMatch())
        return QStringLiteral("Admin ID must be 2-20 characters: letters, digits, '-' or '_'.");
    if (name.simplified().isEmpty())
        return QStringLiteral("Enter the administrator's name.");
    if (password.size() < 8)
        return QStringLiteral("Administrator passwords must be at least 8 characters.");
    return QString();
}

int AdminService::adminCount(QString* error) const
{
    QSqlQuery q(m_db.connection());
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM admin")) || !q.next()) {
        if (error)
            *error = q.lastError().text();
        return -1;
    }
    return q.value(0).toInt();
}

std::vector<AdminAccount> AdminService::listAdmins(QString* error) const
{
    std::vector<AdminAccount> out;
    QSqlQuery q(m_db.connection());
    if (!q.exec(QStringLiteral("SELECT admin_id, name FROM admin ORDER BY admin_id"))) {
        if (error)
            *error = q.lastError().text();
        return out;
    }
    while (q.next())
        out.push_back({q.value(0).toString(), q.value(1).toString()});
    return out;
}

OpResult AdminService::insertAdmin(const QString& adminIdIn, const QString& nameIn, const QString& password,
                                   bool onlyIfEmpty) const
{
    const QString adminId = adminIdIn.trimmed();
    const QString name = nameIn.simplified();
    const QString invalid = validate(adminId, name, password);
    if (!invalid.isEmpty())
        return OpResult::failure(invalid);

    QSqlDatabase db = m_db.connection();
    Transaction tx(db);
    if (!tx.isActive())
        return OpResult::failure(QStringLiteral("Could not start transaction: ") + tx.lastError());
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT (SELECT COUNT(*) FROM admin), (SELECT COUNT(*) FROM admin WHERE admin_id = ?)"));
        q.addBindValue(adminId);
        if (!q.exec() || !q.next())
            return OpResult::failure(q.lastError().text());
        if (onlyIfEmpty && q.value(0).toInt() > 0)
            return OpResult::failure(QStringLiteral("An administrator account already exists. Sign in instead."));
        if (q.value(1).toInt() > 0)
            return OpResult::failure(QStringLiteral("Admin ID %1 already exists.").arg(adminId));
    }
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO admin (admin_id, name, password_hash, created_at) VALUES (?, ?, ?, ?)"));
        q.addBindValue(adminId);
        q.addBindValue(name);
        q.addBindValue(password::hash(password));
        q.addBindValue(timeutil::nowUtc());
        if (!q.exec())
            return OpResult::failure(friendlySqlError(q.lastError()));
    }
    if (!tx.commit())
        return OpResult::failure(QStringLiteral("Commit failed: ") + tx.lastError());
    return OpResult::success(QStringLiteral("Administrator %1 created.").arg(adminId));
}

OpResult AdminService::createFirstAdmin(const QString& adminId, const QString& name, const QString& password) const
{
    return insertAdmin(adminId, name, password, true);
}

OpResult AdminService::addAdmin(const QString& adminId, const QString& name, const QString& password) const
{
    return insertAdmin(adminId, name, password, false);
}

std::optional<AdminAccount> AdminService::authenticate(const QString& adminId, const QString& password,
                                                       QString* error) const
{
    if (adminId.trimmed().isEmpty() || password.isEmpty()) {
        if (error)
            *error = QStringLiteral("Enter your admin ID and password.");
        return std::nullopt;
    }
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("SELECT admin_id, name, password_hash FROM admin WHERE admin_id = ?"));
    q.addBindValue(adminId.trimmed());
    if (!q.exec()) {
        if (error)
            *error = QStringLiteral("Login failed because of a database error: ") + q.lastError().text();
        return std::nullopt;
    }
    if (!q.next() || !password::verify(password, q.value(2).toString())) {
        if (error)
            *error = QStringLiteral("Invalid admin ID or password.");
        return std::nullopt;
    }
    return AdminAccount{q.value(0).toString(), q.value(1).toString()};
}

OpResult AdminService::changePassword(const QString& adminId, const QString& currentPassword,
                                      const QString& newPassword) const
{
    if (!authenticate(adminId, currentPassword))
        return OpResult::failure(QStringLiteral("The current password is not correct."));
    if (newPassword.size() < 8)
        return OpResult::failure(QStringLiteral("The new password must be at least 8 characters."));
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("UPDATE admin SET password_hash = ? WHERE admin_id = ?"));
    q.addBindValue(password::hash(newPassword));
    q.addBindValue(adminId.trimmed());
    if (!q.exec())
        return OpResult::failure(friendlySqlError(q.lastError()));
    if (q.numRowsAffected() != 1)
        return OpResult::failure(QStringLiteral("Administrator %1 was not found.").arg(adminId));
    return OpResult::success(QStringLiteral("Password changed."));
}

}  // namespace labdesk
