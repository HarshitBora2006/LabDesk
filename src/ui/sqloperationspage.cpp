#include "sqloperationspage.h"

#include "services.h"
#include "uihelpers.h"

#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace labdesk::ui {

namespace {
struct Operation {
    const char* kind;
    const char* usedBy;
    const char* function;
    const char* sql;       // as written in the service (with ? parameters)
    const char* runnable;  // read-only version that can run now; empty if not a SELECT
};

// Statements copied from the services; the "runnable" variants replace ? by example values.
const Operation kOperations[] = {
    {"SELECT (aggregate sub-queries)", "Dashboard figures", "ComputerService::dashboardStats",
     "SELECT (SELECT COUNT(DISTINCT location) FROM computer), (SELECT COUNT(*) FROM computer),\n"
     "       (SELECT COUNT(*) FROM computer WHERE status = 'AVAILABLE'), ...,\n"
     "       (SELECT COUNT(*) FROM lab_request WHERE status = 'WAITING')",
     "SELECT (SELECT COUNT(*) FROM computer) AS computers,\n"
     "       (SELECT COUNT(*) FROM computer WHERE status = 'AVAILABLE') AS available,\n"
     "       (SELECT COUNT(*) FROM computer WHERE status = 'OCCUPIED') AS in_use,\n"
     "       (SELECT COUNT(*) FROM computer WHERE status = 'MAINTENANCE') AS maintenance,\n"
     "       (SELECT COUNT(*) FROM session WHERE status = 'ACTIVE') AS active_sessions,\n"
     "       (SELECT COUNT(*) FROM student) AS students"},
    {"SELECT + GROUP BY", "Dashboard laboratory cards", "ComputerService::labSummaries",
     "SELECT location, COUNT(*), SUM(CASE WHEN status = 'AVAILABLE' THEN 1 ELSE 0 END), ...\n"
     "FROM computer GROUP BY location ORDER BY location",
     "SELECT location, COUNT(*) AS computers,\n"
     "       SUM(CASE WHEN status = 'AVAILABLE' THEN 1 ELSE 0 END) AS available,\n"
     "       SUM(CASE WHEN status = 'OCCUPIED' THEN 1 ELSE 0 END) AS in_use,\n"
     "       SUM(CASE WHEN status = 'MAINTENANCE' THEN 1 ELSE 0 END) AS maintenance\n"
     "FROM computer GROUP BY location ORDER BY location"},
    {"SELECT + JOIN", "Usage Logs", "SessionService::listSessions",
     "SELECT s.session_id, s.student_id, st.name, s.computer_id, c.computer_name, c.location,\n"
     "       s.start_time, s.end_time, s.status\n"
     "FROM session s JOIN student st ON st.student_id = s.student_id\n"
     "JOIN computer c ON c.computer_id = s.computer_id ORDER BY s.session_id DESC LIMIT ?",
     "SELECT s.session_id, s.student_id, st.name, s.computer_id, c.location, s.start_time, s.end_time, s.status\n"
     "FROM session s JOIN student st ON st.student_id = s.student_id\n"
     "JOIN computer c ON c.computer_id = s.computer_id ORDER BY s.session_id DESC LIMIT 20"},
    {"SELECT + LEFT JOIN", "Allocation Management (measured results)", "QueueService::history",
     "SELECT r.request_id, r.student_id, ..., s.start_time, s.end_time\n"
     "FROM lab_request r JOIN student st ON st.student_id = r.student_id\n"
     "LEFT JOIN session s ON s.session_id = r.session_id ORDER BY r.request_id DESC",
     "SELECT r.request_id, r.student_id, r.purpose, r.requested_at, s.start_time, s.end_time, r.status\n"
     "FROM lab_request r LEFT JOIN session s ON s.session_id = r.session_id\n"
     "ORDER BY r.request_id DESC LIMIT 20"},
    {"SELECT + aggregate + date functions", "Reports (usage by laboratory)", "ReportService::usageByLab",
     "SELECT c.location, '', COUNT(*), SUM(minutes), AVG(minutes) FROM session s\n"
     "JOIN computer c ON c.computer_id = s.computer_id WHERE s.status = 'ENDED' GROUP BY c.location\n"
     "-- minutes = (julianday(s.end_time) - julianday(s.start_time)) * 1440",
     "SELECT c.location, COUNT(*) AS sessions,\n"
     "       ROUND(SUM((julianday(s.end_time) - julianday(s.start_time)) * 1440.0), 1) AS total_minutes\n"
     "FROM session s JOIN computer c ON c.computer_id = s.computer_id\n"
     "WHERE s.status = 'ENDED' GROUP BY c.location ORDER BY total_minutes DESC"},
    {"SELECT + UNION ALL", "Dashboard recent activity", "ReportService::recentActivity",
     "SELECT t, kind, detail FROM ( SELECT s.start_time, 'SESSION_START', ... FROM session s\n"
     "  UNION ALL ... FROM maintenance m UNION ALL ... FROM lab_request r ) ORDER BY t DESC LIMIT ?",
     "SELECT t, kind, detail FROM (\n"
     "  SELECT start_time AS t, 'SESSION_START' AS kind, student_id || ' started ' || computer_id AS detail FROM session\n"
     "  UNION ALL SELECT end_time, 'SESSION_END', student_id || ' released ' || computer_id FROM session\n"
     "      WHERE end_time IS NOT NULL\n"
     "  UNION ALL SELECT reported_at, 'MAINTENANCE_OPEN', computer_id FROM maintenance\n"
     ") ORDER BY t DESC LIMIT 10"},
    {"INSERT", "Student Management (add)", "StudentService::addStudent",
     "INSERT INTO student (student_id, name, email, password_hash, department) VALUES (?, ?, ?, ?, ?)", ""},
    {"UPDATE (guarded)", "Starting a session", "SessionService::allocateComputer / QueueService::startNow",
     "UPDATE computer SET status = 'OCCUPIED' WHERE computer_id = ? AND status = 'AVAILABLE'\n"
     "-- exactly 1 affected row is required, otherwise the transaction rolls back", ""},
    {"DELETE (restricted)", "Computer Management (delete)", "ComputerService::deleteComputer",
     "DELETE FROM computer WHERE computer_id = ?\n"
     "-- refused by ON DELETE RESTRICT when session or maintenance rows refer to the computer", ""},
    {"Transaction", "Allocation, session end, maintenance, workload save", "Transaction (database.cpp)",
     "BEGIN IMMEDIATE TRANSACTION;  -- take the write lock before checking\n"
     "  ... checks and updates ...\n"
     "COMMIT;                        -- or ROLLBACK on any failure", ""},
    {"Schema introspection", "Schema and Relationships", "DbmsService::columns / foreignKeys",
     "SELECT name, type, \"notnull\", dflt_value, pk FROM pragma_table_info(?)",
     "SELECT name, type, \"notnull\", dflt_value, pk FROM pragma_table_info('session')"},
};
}  // namespace

SqlOperationsPage::SqlOperationsPage(Services& services, QWidget* parent) : Page(parent), m_services(services)
{
    QWidget* body = makeScrollPage(this);
    QVBoxLayout* root = makePageLayout(body, QStringLiteral("SQL Operations"),
                                       QStringLiteral("The statements LabDesk actually runs, and where."));
    m_banner = new MessageBanner();
    root->addWidget(m_banner);

    QFrame* catalogueCard = makeCard();
    cardLayout(catalogueCard)->addWidget(makeLabel(QStringLiteral("Operations used by the application"), QStringLiteral("sectionTitle")));
    cardLayout(catalogueCard)->addWidget(makeLabel(QStringLiteral("All input is passed as ? parameters (prepared "
                                                                  "statements), never pasted into the SQL text. Select "
                                                                  "a row to see the statement."),
                                                   QStringLiteral("caption")));
    m_catalogue = new QTableWidget();
    setupTable(m_catalogue, {QStringLiteral("Operation"), QStringLiteral("Used by"), QStringLiteral("Code")});
    m_catalogue->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    const int count = static_cast<int>(sizeof(kOperations) / sizeof(kOperations[0]));
    m_catalogue->setRowCount(count);
    for (int i = 0; i < count; ++i) {
        m_catalogue->setItem(i, 0, makeItem(QString::fromUtf8(kOperations[i].kind), i));
        m_catalogue->setItem(i, 1, makeItem(QString::fromUtf8(kOperations[i].usedBy)));
        m_catalogue->setItem(i, 2, makeItem(QString::fromUtf8(kOperations[i].function)));
    }
    m_catalogue->setMinimumHeight(330);
    cardLayout(catalogueCard)->addWidget(m_catalogue);
    m_sql = makeLabel(QString(), QStringLiteral("mono"));
    m_sql->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cardLayout(catalogueCard)->addWidget(m_sql);
    m_note = makeLabel(QString(), QStringLiteral("caption"));
    cardLayout(catalogueCard)->addWidget(m_note);
    auto* runExample = makeButton(QStringLiteral("Run the read-only example"), QStringLiteral("primary"));
    cardLayout(catalogueCard)->addWidget(runExample, 0, Qt::AlignLeft);
    root->addWidget(catalogueCard);

    QFrame* consoleCard = makeCard();
    cardLayout(consoleCard)->addWidget(makeLabel(QStringLiteral("Read-only query console"), QStringLiteral("sectionTitle")));
    cardLayout(consoleCard)->addWidget(makeLabel(
        QStringLiteral("Administrators only. One SELECT (or WITH ... SELECT) at a time. While it runs, SQLite's "
                       "query_only mode is on, so the database engine refuses any change. Columns whose name contains "
                       "\"password\" are hidden. At most 500 rows are shown."),
        QStringLiteral("caption")));
    m_console = new QPlainTextEdit();
    m_console->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_console->setPlaceholderText(QStringLiteral("SELECT computer_id, status FROM computer WHERE status = 'AVAILABLE'"));
    m_console->setFixedHeight(90);
    cardLayout(consoleCard)->addWidget(m_console);
    auto* run = makeButton(QStringLiteral("Run query"), QStringLiteral("primary"));
    cardLayout(consoleCard)->addWidget(run, 0, Qt::AlignLeft);
    m_result = new QTableWidget();
    setupTable(m_result, {});
    m_result->setMinimumHeight(240);
    m_result->hide();
    cardLayout(consoleCard)->addWidget(m_result);
    m_resultState = makeStateMessage();
    cardLayout(consoleCard)->addWidget(m_resultState);
    root->addWidget(consoleCard);
    root->addStretch(1);

    connect(m_catalogue, &QTableWidget::itemSelectionChanged, this, &SqlOperationsPage::onOperationSelected);
    connect(runExample, &QPushButton::clicked, this, [this]() {
        const QVariant index = selectedRowData(m_catalogue);
        if (!index.isValid())
            return;
        const QString runnable = QString::fromUtf8(kOperations[index.toInt()].runnable);
        if (runnable.isEmpty()) {
            m_banner->showInfo(QStringLiteral("This operation changes data, so it only runs inside its own screen and "
                                              "transaction; it cannot be run from here."));
            return;
        }
        m_console->setPlainText(runnable);
        runQuery(runnable);
    });
    connect(run, &QPushButton::clicked, this, [this]() { runQuery(m_console->toPlainText()); });
    m_catalogue->selectRow(0);
}

void SqlOperationsPage::onOperationSelected()
{
    const QVariant index = selectedRowData(m_catalogue);
    if (!index.isValid())
        return;
    const Operation& op = kOperations[index.toInt()];
    m_sql->setText(QString::fromUtf8(op.sql));
    m_note->setText(QString::fromUtf8(op.runnable).isEmpty()
                        ? QStringLiteral("Writes data: runs only inside its screen's transaction.")
                        : QStringLiteral("Read-only: the example below replaces ? parameters with sample values."));
}

void SqlOperationsPage::runQuery(const QString& sql)
{
    QueryResult result;
    QString error;
    if (!m_services.dbms.runReadOnly(sql, result, &error)) {
        m_result->hide();
        showErrorState(m_resultState, error);
        return;
    }
    m_result->clear();
    m_result->setColumnCount(static_cast<int>(result.columns.size()));
    m_result->setHorizontalHeaderLabels(result.columns);
    m_result->setRowCount(static_cast<int>(result.rows.size()));
    for (int r = 0; r < static_cast<int>(result.rows.size()); ++r)
        for (int c = 0; c < result.columns.size(); ++c)
            m_result->setItem(r, c, makeItem(result.rows[static_cast<std::size_t>(r)].value(c)));
    m_result->resizeColumnsToContents();
    if (result.rows.empty()) {
        m_result->show();
        showEmptyState(m_resultState, QStringLiteral("The query ran and returned no rows."));
    } else {
        m_result->show();
        m_resultState->hide();
    }
    m_banner->showInfo(QStringLiteral("%1 row(s)%2.").arg(static_cast<int>(result.rows.size()))
                           .arg(result.truncated ? QStringLiteral(" (truncated at 500)") : QString()));
}

}  // namespace labdesk::ui
