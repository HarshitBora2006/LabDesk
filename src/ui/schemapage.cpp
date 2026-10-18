#include "schemapage.h"

#include "services.h"
#include "uihelpers.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace labdesk::ui {

SchemaPage::SchemaPage(Services& services, QWidget* parent) : Page(parent), m_services(services)
{
    QWidget* body = makeScrollPage(this);
    QVBoxLayout* root = makePageLayout(body, QStringLiteral("Schema and Relationships"),
                                       QStringLiteral("Tables, columns, keys and constraints exactly as stored in the "
                                                      "database."));
    m_message = makeStateMessage();
    root->addWidget(m_message);

    auto* top = new QHBoxLayout();
    top->setSpacing(16);
    QFrame* listCard = makeCard();
    listCard->setFixedWidth(220);
    cardLayout(listCard)->addWidget(makeLabel(QStringLiteral("Tables"), QStringLiteral("sectionTitle")));
    m_tableList = new QListWidget();
    m_tableList->setObjectName(QStringLiteral("log"));
    cardLayout(listCard)->addWidget(m_tableList, 1);
    top->addWidget(listCard);

    QFrame* detailCard = makeCard();
    m_tableTitle = makeLabel(QString(), QStringLiteral("sectionTitle"));
    cardLayout(detailCard)->addWidget(m_tableTitle);
    m_columns = new QTableWidget();
    setupTable(m_columns, {QStringLiteral("Column"), QStringLiteral("Type"), QStringLiteral("Primary key"),
                           QStringLiteral("NOT NULL"), QStringLiteral("Default")});
    m_columns->setMinimumHeight(200);
    cardLayout(detailCard)->addWidget(m_columns);
    cardLayout(detailCard)->addWidget(makeLabel(QStringLiteral("Foreign keys of this table"), QStringLiteral("fieldLabel")));
    m_keys = new QTableWidget();
    setupTable(m_keys, {QStringLiteral("Column"), QStringLiteral("References"), QStringLiteral("On delete")});
    m_keys->setFixedHeight(32 * 3 + 40);
    cardLayout(detailCard)->addWidget(m_keys);
    m_keysState = makeStateMessage();
    cardLayout(detailCard)->addWidget(m_keysState);
    cardLayout(detailCard)->addWidget(makeLabel(QStringLiteral("Indexes and triggers on this table"), QStringLiteral("fieldLabel")));
    m_objects = new QTableWidget();
    setupTable(m_objects, {QStringLiteral("Kind"), QStringLiteral("Name")});
    m_objects->setMinimumHeight(170);
    cardLayout(detailCard)->addWidget(m_objects);
    top->addWidget(detailCard, 1);
    root->addLayout(top);

    QFrame* relCard = makeCard();
    cardLayout(relCard)->addWidget(makeLabel(QStringLiteral("All relationships"), QStringLiteral("sectionTitle")));
    cardLayout(relCard)->addWidget(makeLabel(
        QStringLiteral("Each row is a foreign key: the child column may only hold a value that exists in the parent "
                       "key. With ON DELETE RESTRICT the parent row cannot be deleted while children refer to it; this "
                       "is how referential integrity keeps session history attached to real students and computers."),
        QStringLiteral("caption")));
    m_relationships = new QTableWidget();
    setupTable(m_relationships, {QStringLiteral("Child table.column"), QStringLiteral("Parent table.column"),
                                 QStringLiteral("On delete"), QStringLiteral("Cardinality")});
    m_relationships->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_relationships->setMinimumHeight(220);
    cardLayout(relCard)->addWidget(m_relationships);
    root->addWidget(relCard);
    root->addStretch(1);

    connect(m_tableList, &QListWidget::currentTextChanged, this, &SchemaPage::showTable);
}

void SchemaPage::refresh()
{
    DatabaseOverview o;
    QString error;
    if (!m_services.dbms.overview(o, &error)) {
        showErrorState(m_message, error);
        return;
    }
    m_message->hide();
    const QString keep = m_tableList->currentItem() ? m_tableList->currentItem()->text() : QString();
    m_tableList->blockSignals(true);
    m_tableList->clear();
    for (const TableSummary& t : o.tables)
        if (t.name != QStringLiteral("sqlite_sequence"))
            m_tableList->addItem(t.name);
    m_tableList->blockSignals(false);
    const auto found = m_tableList->findItems(keep.isEmpty() ? QStringLiteral("session") : keep, Qt::MatchExactly);
    if (!found.isEmpty())
        m_tableList->setCurrentItem(found.first());
    else if (m_tableList->count() > 0)
        m_tableList->setCurrentRow(0);
    if (m_tableList->currentItem())
        showTable(m_tableList->currentItem()->text());

    std::vector<ForeignKeyInfo> all;
    if (!m_services.dbms.allForeignKeys(all, &error)) {
        showErrorState(m_message, error);
        return;
    }
    m_relationships->setRowCount(static_cast<int>(all.size()));
    for (int row = 0; row < static_cast<int>(all.size()); ++row) {
        const ForeignKeyInfo& k = all[static_cast<std::size_t>(row)];
        m_relationships->setItem(row, 0, makeItem(k.fromTable + QLatin1Char('.') + k.fromColumn));
        m_relationships->setItem(row, 1, makeItem(k.toTable + QLatin1Char('.') + k.toColumn));
        m_relationships->setItem(row, 2, makeItem(k.onDelete));
        m_relationships->setItem(row, 3, makeItem(QStringLiteral("each %1 row belongs to one %2; a %2 has many").arg(k.fromTable, k.toTable)));
    }
}

void SchemaPage::showTable(const QString& table)
{
    if (table.isEmpty())
        return;
    QString error;
    std::vector<ColumnInfo> columns;
    if (!m_services.dbms.columns(table, columns, &error)) {
        showErrorState(m_message, error);
        return;
    }
    m_tableTitle->setText(QStringLiteral("Table %1").arg(table));
    m_columns->setRowCount(static_cast<int>(columns.size()));
    for (int row = 0; row < static_cast<int>(columns.size()); ++row) {
        const ColumnInfo& c = columns[static_cast<std::size_t>(row)];
        m_columns->setItem(row, 0, makeItem(c.name));
        m_columns->setItem(row, 1, makeItem(c.type));
        m_columns->setItem(row, 2, makeItem(c.primaryKey ? QStringLiteral("PK") : QString()));
        m_columns->setItem(row, 3, makeItem(c.notNull ? QStringLiteral("yes") : QString()));
        m_columns->setItem(row, 4, makeItem(c.defaultValue));
    }
    m_columns->resizeColumnsToContents();

    std::vector<ForeignKeyInfo> keys;
    m_services.dbms.foreignKeys(table, keys, &error);
    m_keys->setRowCount(static_cast<int>(keys.size()));
    for (int row = 0; row < static_cast<int>(keys.size()); ++row) {
        const ForeignKeyInfo& k = keys[static_cast<std::size_t>(row)];
        m_keys->setItem(row, 0, makeItem(k.fromColumn));
        m_keys->setItem(row, 1, makeItem(k.toTable + QLatin1Char('.') + k.toColumn));
        m_keys->setItem(row, 2, makeItem(k.onDelete));
    }
    if (keys.empty()) {
        m_keys->hide();
        showEmptyState(m_keysState, QStringLiteral("%1 has no foreign keys: it is a parent (or independent) table.").arg(table));
    } else {
        m_keys->show();
        m_keysState->hide();
    }

    std::vector<std::pair<QString, QString>> indexes, triggers;
    m_services.dbms.schemaObjects(QStringLiteral("index"), indexes, &error);
    m_services.dbms.schemaObjects(QStringLiteral("trigger"), triggers, &error);
    m_objects->setRowCount(0);
    auto add = [this, &table](const QString& kind, const std::vector<std::pair<QString, QString>>& objects) {
        for (const auto& o : objects) {
            if (o.second != table)
                continue;
            const int row = m_objects->rowCount();
            m_objects->insertRow(row);
            m_objects->setItem(row, 0, makeItem(kind));
            m_objects->setItem(row, 1, makeItem(o.first));
        }
    };
    add(QStringLiteral("Index"), indexes);
    add(QStringLiteral("Trigger"), triggers);
    m_objects->resizeColumnsToContents();
}

}  // namespace labdesk::ui
