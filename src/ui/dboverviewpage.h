#pragma once
// DBMS Management > Database Overview: engine, connection, schema version,
// tables with live row counts and an integrity check of the REAL database.

#include "page.h"

#include <QHash>

class QLabel;
class QListWidget;
class QTableWidget;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class DbOverviewPage : public Page {
    Q_OBJECT
public:
    DbOverviewPage(Services& services, QWidget* parent = nullptr);
    void refresh() override;

private:
    void runIntegrityCheck();

    Services& m_services;
    QHash<QString, QLabel*> m_facts;
    QTableWidget* m_tables = nullptr;
    QLabel* m_tablesState = nullptr;
    QListWidget* m_integrity = nullptr;
};

}  // namespace labdesk::ui
