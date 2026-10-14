#pragma once
// Plain data records mirroring the database tables.

#include <QString>
#include <QtGlobal>

#include <optional>

namespace labdesk {

namespace ComputerStatus {
inline const QString Available = QStringLiteral("AVAILABLE");
inline const QString Occupied = QStringLiteral("OCCUPIED");
inline const QString Maintenance = QStringLiteral("MAINTENANCE");
}  // namespace ComputerStatus

namespace SessionStatus {
inline const QString Active = QStringLiteral("ACTIVE");
inline const QString Ended = QStringLiteral("ENDED");
}  // namespace SessionStatus

namespace MaintenanceStatus {
inline const QString Open = QStringLiteral("OPEN");
inline const QString Resolved = QStringLiteral("RESOLVED");
}  // namespace MaintenanceStatus

struct Student {
    QString studentId;
    QString name;
    QString email;
    QString department;
};

struct Computer {
    QString computerId;
    QString name;
    QString location;
    QString status;
};

struct SessionRecord {
    qint64 sessionId = 0;
    QString studentId;
    QString studentName;
    QString computerId;
    QString computerName;
    QString location;
    QString startTime;  // UTC ISO-8601
    QString endTime;    // empty while ACTIVE
    QString status;
};

struct MaintenanceRecord {
    qint64 maintenanceId = 0;
    QString computerId;
    QString computerName;
    QString description;
    QString reportedAt;
    QString resolvedAt;
    QString status;
};

struct StoredProcess {
    qint64 processId = 0;
    std::optional<qint64> sessionId;
    QString label;
    int arrival = 0;
    int burst = 1;
    int priority = 0;
};

// A "laboratory" is a distinct computer.location value. There is no separate
// laboratory table, so labs are derived from the computer inventory.
struct LabSummary {
    QString location;
    int total = 0;
    int available = 0;
    int occupied = 0;
    int maintenance = 0;
};

struct DashboardStats {
    int laboratories = 0;
    int totalComputers = 0;
    int available = 0;
    int occupied = 0;
    int maintenance = 0;
    int activeSessions = 0;
    int students = 0;
    int openMaintenance = 0;
};

}  // namespace labdesk
