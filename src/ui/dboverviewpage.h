#pragma once
// DBMS Management > Database Overview: engine, connection, schema version,
// tables with live row counts of the real database.

#include "page.h"

#include <QHash>

class QLabel;
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
    Services& m_services;
    QHash<QString, QLabel*> m_facts;
    QTableWidget* m_tables = nullptr;
    QLabel* m_tablesState = nullptr;
};

}  // namespace labdesk::ui
