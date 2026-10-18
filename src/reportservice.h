#pragma once
// Read-only SQL reports over REAL records (sessions, maintenance, requests).
// Every figure is computed by an aggregate query; nothing is stored or invented.
// Durations use julianday() on the stored UTC ISO-8601 timestamps.

#include <QString>
#include <QVariant>

#include <vector>

namespace labdesk {

class Database;

struct ActivityItem {
    QString time;  // UTC ISO-8601
    QString kind;  // SESSION_START, SESSION_END, MAINTENANCE_OPEN, MAINTENANCE_RESOLVED, QUEUE_JOIN
    QString text;
};

struct UsageRow {
    QString key;     // laboratory, day (yyyy-MM-dd, local time) or student id
    QString label;   // extra label (student name), may be empty
    int sessions = 0;
    double totalMinutes = 0;
    double averageMinutes = 0;
};

struct QueueRow {
    QString grantedBy;  // IMMEDIATE, FCFS, SJF, PRIORITY
    int requests = 0;
    double averageWaitMinutes = 0;
    double longestWaitMinutes = 0;
};

struct StudentVisit {
    qint64 sessionId = 0;
    QString computerId;
    QString location;
    QString startTime;
    QString endTime;
    QString status;
    QString purpose;       // empty if the session had no request (older or admin demo sessions)
    QString requestedAt;   // empty likewise
    int expectedMinutes = 0;
};

class ReportService {
public:
    explicit ReportService(Database& db) : m_db(db) {}

    bool recentActivity(int limit, std::vector<ActivityItem>& out, QString* error = nullptr) const;
    bool usageByLab(std::vector<UsageRow>& out, QString* error = nullptr) const;
    bool usageByDay(int days, std::vector<UsageRow>& out, QString* error = nullptr) const;
    bool topStudents(int limit, std::vector<UsageRow>& out, QString* error = nullptr) const;
    bool queueByPolicy(std::vector<QueueRow>& out, QString* error = nullptr) const;
    bool studentHistory(const QString& studentId, std::vector<StudentVisit>& out, QString* error = nullptr) const;

private:
    bool usage(const QString& sql, const QVariantList& binds, std::vector<UsageRow>& out, QString* error) const;
    Database& m_db;
};

}  // namespace labdesk
