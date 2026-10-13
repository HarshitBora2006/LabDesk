#include "studentservice.h"

#include "database.h"
#include "passwordhasher.h"

#include <QRegularExpression>
#include <QSqlQuery>
#include <QVariant>

namespace labdesk {

namespace {
Student readStudent(const QSqlQuery& q)
{
    Student s;
    s.studentId = q.value(0).toString();
    s.name = q.value(1).toString();
    s.email = q.value(2).toString();
    s.department = q.value(3).toString();
    return s;
}

Student normalised(Student s)
{
    s.studentId = s.studentId.trimmed();
    s.name = s.name.simplified();
    s.email = s.email.trimmed().toLower();
    s.department = s.department.simplified();
    return s;
}
}  // namespace

QString StudentService::validate(const Student& in, bool passwordRequired, const QString& password)
{
    const Student s = normalised(in);
    static const QRegularExpression idPattern(QStringLiteral("^[A-Za-z0-9_-]{2,20}$"));
    static const QRegularExpression emailPattern(QStringLiteral("^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$"));

    if (s.studentId.isEmpty())
        return QStringLiteral("Student ID is required.");
    if (!idPattern.match(s.studentId).hasMatch())
        return QStringLiteral("Student ID must be 2-20 characters: letters, digits, '-' or '_'.");
    if (s.name.isEmpty())
        return QStringLiteral("Name is required.");
    if (s.name.size() > 100)
        return QStringLiteral("Name must be at most 100 characters.");
    if (s.email.isEmpty())
        return QStringLiteral("Email is required.");
    if (!emailPattern.match(s.email).hasMatch())
        return QStringLiteral("Enter a valid email address, for example name@university.edu.");
    if (s.department.isEmpty())
        return QStringLiteral("Department is required.");
    if (passwordRequired || !password.isEmpty()) {
        if (password.size() < 6)
            return QStringLiteral("Password must be at least 6 characters.");
    }
    return QString();
}

std::vector<Student> StudentService::listStudents(QString* error) const
{
    std::vector<Student> out;
    QSqlQuery q(m_db.connection());
    if (!q.exec(QStringLiteral("SELECT student_id, name, email, department FROM student ORDER BY student_id"))) {
        if (error)
            *error = q.lastError().text();
        return out;
    }
    while (q.next())
        out.push_back(readStudent(q));
    return out;
}

std::optional<Student> StudentService::findStudent(const QString& studentId, QString* error) const
{
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("SELECT student_id, name, email, department FROM student WHERE student_id = ?"));
    q.addBindValue(studentId.trimmed());
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return readStudent(q);
}

OpResult StudentService::addStudent(const Student& input, const QString& password) const
{
    const Student s = normalised(input);
    const QString invalid = validate(s, true, password);
    if (!invalid.isEmpty())
        return OpResult::failure(invalid);

    QSqlDatabase db = m_db.connection();
    Transaction tx(db);
    if (!tx.isActive())
        return OpResult::failure(QStringLiteral("Could not start transaction: ") + tx.lastError());

    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT "
                                 " (SELECT COUNT(*) FROM student WHERE student_id = ?),"
                                 " (SELECT COUNT(*) FROM student WHERE email = ?)"));
        q.addBindValue(s.studentId);
        q.addBindValue(s.email);
        if (!q.exec() || !q.next())
            return OpResult::failure(q.lastError().text());
        if (q.value(0).toInt() > 0)
            return OpResult::failure(QStringLiteral("Student ID %1 already exists.").arg(s.studentId));
        if (q.value(1).toInt() > 0)
            return OpResult::failure(QStringLiteral("Email %1 is already registered.").arg(s.email));
    }
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO student (student_id, name, email, password_hash, department) "
                                 "VALUES (?, ?, ?, ?, ?)"));
        q.addBindValue(s.studentId);
        q.addBindValue(s.name);
        q.addBindValue(s.email);
        q.addBindValue(password::hash(password));
        q.addBindValue(s.department);
        if (!q.exec())
            return OpResult::failure(friendlySqlError(q.lastError()));
    }
    if (!tx.commit())
        return OpResult::failure(QStringLiteral("Commit failed: ") + tx.lastError());
    return OpResult::success(QStringLiteral("Student %1 added.").arg(s.studentId));
}

OpResult StudentService::updateStudent(const Student& input, const QString& newPassword) const
{
    const Student s = normalised(input);
    const QString invalid = validate(s, false, newPassword);
    if (!invalid.isEmpty())
        return OpResult::failure(invalid);

    QSqlDatabase db = m_db.connection();
    Transaction tx(db);
    if (!tx.isActive())
        return OpResult::failure(QStringLiteral("Could not start transaction: ") + tx.lastError());

    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM student WHERE email = ? AND student_id <> ?"));
        q.addBindValue(s.email);
        q.addBindValue(s.studentId);
        if (!q.exec() || !q.next())
            return OpResult::failure(q.lastError().text());
        if (q.value(0).toInt() > 0)
            return OpResult::failure(QStringLiteral("Email %1 belongs to another student.").arg(s.email));
    }
    {
        QSqlQuery q(db);
        if (newPassword.isEmpty()) {
            q.prepare(QStringLiteral("UPDATE student SET name = ?, email = ?, department = ? WHERE student_id = ?"));
            q.addBindValue(s.name);
            q.addBindValue(s.email);
            q.addBindValue(s.department);
            q.addBindValue(s.studentId);
        } else {
            q.prepare(QStringLiteral("UPDATE student SET name = ?, email = ?, department = ?, password_hash = ? "
                                     "WHERE student_id = ?"));
            q.addBindValue(s.name);
            q.addBindValue(s.email);
            q.addBindValue(s.department);
            q.addBindValue(password::hash(newPassword));
            q.addBindValue(s.studentId);
        }
        if (!q.exec())
            return OpResult::failure(friendlySqlError(q.lastError()));
        if (q.numRowsAffected() != 1)
            return OpResult::failure(QStringLiteral("Student %1 no longer exists.").arg(s.studentId));
    }
    if (!tx.commit())
        return OpResult::failure(QStringLiteral("Commit failed: ") + tx.lastError());
    return OpResult::success(QStringLiteral("Student %1 updated.").arg(s.studentId));
}

OpResult StudentService::deleteStudent(const QString& studentIdIn) const
{
    const QString studentId = studentIdIn.trimmed();
    QSqlDatabase db = m_db.connection();
    Transaction tx(db);
    if (!tx.isActive())
        return OpResult::failure(QStringLiteral("Could not start transaction: ") + tx.lastError());

    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM session WHERE student_id = ?"));
        q.addBindValue(studentId);
        if (!q.exec() || !q.next())
            return OpResult::failure(q.lastError().text());
        const int sessions = q.value(0).toInt();
        if (sessions > 0) {
            return OpResult::failure(
                QStringLiteral("Student %1 has %2 session record(s). Session history is preserved, "
                               "so this student cannot be deleted.")
                    .arg(studentId)
                    .arg(sessions));
        }
    }
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("DELETE FROM student WHERE student_id = ?"));
        q.addBindValue(studentId);
        if (!q.exec())
            return OpResult::failure(friendlySqlError(q.lastError()));  // FK is the backstop
        if (q.numRowsAffected() != 1)
            return OpResult::failure(QStringLiteral("Student %1 was not found.").arg(studentId));
    }
    if (!tx.commit())
        return OpResult::failure(QStringLiteral("Commit failed: ") + tx.lastError());
    return OpResult::success(QStringLiteral("Student %1 deleted.").arg(studentId));
}

std::optional<Student> StudentService::authenticate(const QString& studentId, const QString& password,
                                                    QString* error) const
{
    const QString generic = QStringLiteral("Invalid student ID or password.");
    if (studentId.trimmed().isEmpty() || password.isEmpty()) {
        if (error)
            *error = QStringLiteral("Enter your student ID and password.");
        return std::nullopt;
    }
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("SELECT student_id, name, email, department, password_hash "
                             "FROM student WHERE student_id = ?"));
    q.addBindValue(studentId.trimmed());
    if (!q.exec()) {
        if (error)
            *error = QStringLiteral("Login failed because of a database error: ") + q.lastError().text();
        return std::nullopt;
    }
    if (!q.next()) {
        if (error)
            *error = generic;
        return std::nullopt;
    }
    const Student s = readStudent(q);
    const QString stored = q.value(4).toString();
    if (!password::verify(password, stored)) {
        if (error)
            *error = generic;
        return std::nullopt;
    }
    return s;
}

}  // namespace labdesk
