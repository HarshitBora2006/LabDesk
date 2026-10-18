#pragma once
// DBMS Management > Schema and Relationships: the REAL schema read from
// SQLite (pragma_table_info, pragma_foreign_key_list, sqlite_master).

#include "page.h"

class QLabel;
class QListWidget;
class QTableWidget;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class SchemaPage : public Page {
    Q_OBJECT
public:
    SchemaPage(Services& services, QWidget* parent = nullptr);
    void refresh() override;

private:
    void showTable(const QString& table);

    Services& m_services;
    QListWidget* m_tableList = nullptr;
    QLabel* m_tableTitle = nullptr;
    QTableWidget* m_columns = nullptr;
    QTableWidget* m_keys = nullptr;
    QLabel* m_keysState = nullptr;
    QTableWidget* m_objects = nullptr;
    QTableWidget* m_relationships = nullptr;
    QLabel* m_message = nullptr;
};

}  // namespace labdesk::ui
