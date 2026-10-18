#pragma once
// "My Allocation History": the signed-in student's own sessions, newest first.

#include "page.h"

class QLabel;
class QTableWidget;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class StudentHistoryPage : public Page {
    Q_OBJECT
public:
    StudentHistoryPage(Services& services, const QString& studentId, QWidget* parent = nullptr);
    void refresh() override;

private:
    Services& m_services;
    QString m_studentId;
    QLabel* m_summary = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_state = nullptr;
};

}  // namespace labdesk::ui
