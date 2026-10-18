#pragma once
// DBMS Management > SQL Operations: the SQL statements LabDesk really runs,
// where they are used, and (for SELECTs) a read-only "run it now" button.
// The query console accepts ONE read-only SELECT; SQLite's query_only mode is
// switched on while it runs, so the engine itself refuses writes.

#include "page.h"

class QLabel;
class QPlainTextEdit;
class QTableWidget;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class MessageBanner;

class SqlOperationsPage : public Page {
    Q_OBJECT
public:
    SqlOperationsPage(Services& services, QWidget* parent = nullptr);

private:
    void onOperationSelected();
    void runQuery(const QString& sql);

    Services& m_services;
    MessageBanner* m_banner = nullptr;
    QTableWidget* m_catalogue = nullptr;
    QLabel* m_sql = nullptr;
    QLabel* m_note = nullptr;
    QPlainTextEdit* m_console = nullptr;
    QTableWidget* m_result = nullptr;
    QLabel* m_resultState = nullptr;
};

}  // namespace labdesk::ui
