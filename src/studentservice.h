#pragma once
// Student CRUD + authentication. All SQL uses prepared statements.

#include "models.h"
#include "opresult.h"

#include <optional>
#include <vector>

namespace labdesk {

class Database;

class StudentService {
public:
    explicit StudentService(Database& db) : m_db(db) {}

    std::vector<Student> listStudents(QString* error = nullptr) const;
    std::optional<Student> findStudent(const QString& studentId, QString* error = nullptr) const;

    OpResult addStudent(const Student& student, const QString& password) const;
    // Leave newPassword empty to keep the current password.
    OpResult updateStudent(const Student& student, const QString& newPassword) const;
    OpResult deleteStudent(const QString& studentId) const;

    // Returns the student on success; nullopt on bad credentials or error.
    // The message is deliberately generic so it does not reveal which IDs exist.
    std::optional<Student> authenticate(const QString& studentId, const QString& password,
                                        QString* error = nullptr) const;

    // Field validation shared by the GUI and the service. Empty string = valid.
    static QString validate(const Student& student, bool passwordRequired, const QString& password);

private:
    Database& m_db;
};

}  // namespace labdesk
