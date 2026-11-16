#pragma once
// Administrator accounts (schema v3). Passwords are salted and hashed with the
// same scheme as student passwords; plaintext is never stored.
// There is no default administrator: when the table is empty the login screen
// offers to create the first account (createFirstAdmin).

#include "opresult.h"

#include <QString>

#include <optional>
#include <vector>

namespace labdesk {

class Database;

struct AdminAccount {
    QString adminId;
    QString name;
};

class AdminService {
public:
    explicit AdminService(Database& db) : m_db(db) {}

    // -1 on a database error (error filled), otherwise the number of administrators.
    int adminCount(QString* error = nullptr) const;
    std::vector<AdminAccount> listAdmins(QString* error = nullptr) const;

    // Only succeeds while no administrator exists (first-run setup).
    OpResult createFirstAdmin(const QString& adminId, const QString& name, const QString& password) const;
    // Adds another administrator; the GUI only offers this to a signed-in administrator.
    OpResult addAdmin(const QString& adminId, const QString& name, const QString& password) const;

    std::optional<AdminAccount> authenticate(const QString& adminId, const QString& password,
                                             QString* error = nullptr) const;
    // Used by "labdesk --reset-admin": removes every administrator so the login
    // screen offers to create a new one. Requires access to the computer.
    OpResult removeAllAdmins() const;
    OpResult changePassword(const QString& adminId, const QString& currentPassword, const QString& newPassword) const;

    static QString validate(const QString& adminId, const QString& name, const QString& password);

private:
    OpResult insertAdmin(const QString& adminId, const QString& name, const QString& password, bool onlyIfEmpty) const;
    Database& m_db;
};

}  // namespace labdesk
