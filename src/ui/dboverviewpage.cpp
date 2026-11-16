#include "dboverviewpage.h"

#include "layouthelpers.h"
#include "services.h"
#include "uihelpers.h"

#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace labdesk::ui {

namespace {
QString purposeOf(const QString& table)
{
    static const QHash<QString, QString> purposes = {
        {QStringLiteral("student"), QStringLiteral("Registered students and their password hashes")},
        {QStringLiteral("computer"), QStringLiteral("Lab computers, their laboratory and status")},
        {QStringLiteral("session"), QStringLiteral("Who used which computer, from when to when (history is permanent)")},
        {QStringLiteral("maintenance"), QStringLiteral("Reported and resolved hardware issues")},
        {QStringLiteral("process"), QStringLiteral("Saved CPU-scheduling workloads (simulation inputs)")},
        {QStringLiteral("lab_request"), QStringLiteral("Computer requests: arrival, purpose, expected length, queue state")},
        {QStringLiteral("lab_setting"), QStringLiteral("Settings such as the active queue policy")},
        {QStringLiteral("admin"), QStringLiteral("Administrator accounts and their password hashes")},
        {QStringLiteral("sqlite_sequence"), QStringLiteral("SQLite's internal AUTOINCREMENT counters")},
    };
    return purposes.value(table, QStringLiteral("-"));
}
}  // namespace

DbOverviewPage::DbOverviewPage(Services& services, QWidget* parent) : Page(parent), m_services(services)
{
    QWidget* body = makeScrollPage(this);
    QVBoxLayout* root = makePageLayout(body, QStringLiteral("Database Overview"),
                                       QStringLiteral("SQLite database in use"));
    auto* facts = new ResponsiveGrid(170, 12);
    QList<QWidget*> tiles;
    const QPair<QString, QString> names[] = {{QStringLiteral("engine"), QStringLiteral("Database engine")},
                                             {QStringLiteral("connection"), QStringLiteral("Connection")},
                                             {QStringLiteral("fk"), QStringLiteral("Foreign keys")},
                                             {QStringLiteral("version"), QStringLiteral("Schema version")},
                                             {QStringLiteral("objects"), QStringLiteral("Indexes / triggers")}};
    for (const auto& n : names) {
        QFrame* tile = makeCard();
        cardLayout(tile)->setSpacing(2);
        cardLayout(tile)->addWidget(makeLabel(n.second, QStringLiteral("statLabel")));
        auto* value = makeLabel(QStringLiteral("-"), QStringLiteral("sectionTitle"));
        cardLayout(tile)->addWidget(value);
        m_facts.insert(n.first, value);
        tiles.append(tile);
    }
    facts->setWidgets(tiles);
    root->addWidget(facts);

    QFrame* tablesCard = makeCard();
    cardLayout(tablesCard)->addWidget(makeLabel(QStringLiteral("Tables and record counts"), QStringLiteral("sectionTitle")));
    m_tables = new QTableWidget();
    setupTable(m_tables, {QStringLiteral("Table"), QStringLiteral("Records"), QStringLiteral("What it stores")});
    alignNumericColumns(m_tables, {1});
    m_tables->setMinimumHeight(300);
    cardLayout(tablesCard)->addWidget(m_tables);
    m_tablesState = makeStateMessage();
    cardLayout(tablesCard)->addWidget(m_tablesState);
    root->addWidget(tablesCard);

    root->addStretch(1);
}

void DbOverviewPage::refresh()
{
    DatabaseOverview o;
    QString error;
    const bool ok = m_services.dbms.overview(o, &error);
    m_facts.value(QStringLiteral("engine"))->setText(ok ? o.engine : QStringLiteral("-"));
    m_facts.value(QStringLiteral("connection"))->setText(o.connected ? QStringLiteral("Open") : QStringLiteral("Not open"));
    m_facts.value(QStringLiteral("fk"))->setText(ok ? (o.foreignKeysOn ? QStringLiteral("Enforced (ON)")
                                                                        : QStringLiteral("OFF"))
                                                    : QStringLiteral("-"));
    m_facts.value(QStringLiteral("version"))->setText(ok ? QStringLiteral("%1 (journal: %2)").arg(o.schemaVersion).arg(o.journalMode)
                                                         : QStringLiteral("-"));
    m_facts.value(QStringLiteral("objects"))->setText(ok ? QStringLiteral("%1 / %2").arg(o.indexCount).arg(o.triggerCount)
                                                         : QStringLiteral("-"));
    m_tables->setRowCount(static_cast<int>(o.tables.size()));
    for (int row = 0; row < static_cast<int>(o.tables.size()); ++row) {
        const TableSummary& t = o.tables[static_cast<std::size_t>(row)];
        m_tables->setItem(row, 0, makeItem(t.name));
        m_tables->setItem(row, 1, makeNumberItem(static_cast<double>(t.rows)));
        m_tables->setItem(row, 2, makeItem(purposeOf(t.name)));
    }
    m_tables->resizeColumnsToContents();
    if (!ok) {
        m_tables->hide();
        showErrorState(m_tablesState, error);
    } else {
        m_tables->show();
        m_tablesState->hide();
    }
}

}  // namespace labdesk::ui
