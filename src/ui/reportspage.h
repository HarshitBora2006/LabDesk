#pragma once
// Reports: SQL aggregates over real session and request records.

#include "page.h"

class QLabel;
class QTableWidget;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class ReportsPage : public Page {
    Q_OBJECT
public:
    ReportsPage(Services& services, QWidget* parent = nullptr);
    void refresh() override;

private:
    struct Section {
        QTableWidget* table = nullptr;
        QLabel* state = nullptr;
    };
    Section addSection(const QString& title, const QString& caption, const QStringList& headers);

    Services& m_services;
    QWidget* m_body = nullptr;
    Section m_labs;
    Section m_days;
    Section m_students;
    Section m_queue;
};

}  // namespace labdesk::ui
