#include "reportspage.h"

#include "services.h"
#include "uihelpers.h"

#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace labdesk::ui {

namespace {
QTableWidgetItem* durationCell(double minutes)
{
    QTableWidgetItem* item = makeItem(formatDuration(minutes));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

template <typename Row, typename Fill>
void fill(QTableWidget* table, QLabel* state, bool ok, const QString& error, const std::vector<Row>& rows,
          const QString& emptyText, Fill fillRow)
{
    table->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < static_cast<int>(rows.size()); ++r)
        fillRow(r, rows[static_cast<std::size_t>(r)]);
    if (!ok) {
        table->hide();
        showErrorState(state, error);
    } else if (rows.empty()) {
        table->hide();
        showEmptyState(state, emptyText);
    } else {
        table->show();
        state->hide();
    }
}
}  // namespace

ReportsPage::ReportsPage(Services& services, QWidget* parent) : Page(parent), m_services(services)
{
    m_body = makeScrollPage(this);
    QVBoxLayout* root = makePageLayout(m_body, QStringLiteral("Reports"),
                                       QStringLiteral("Usage summaries calculated by SQL from real session and "
                                                      "request records. Durations are h:mm:ss or m:ss."));
    m_labs = addSection(QStringLiteral("Usage by laboratory"), QStringLiteral("Finished sessions only."),
                        {QStringLiteral("Laboratory"), QStringLiteral("Sessions"), QStringLiteral("Total time"),
                         QStringLiteral("Average session")});
    m_days = addSection(QStringLiteral("Last 14 days"), QStringLiteral("Grouped by the local date a session started."),
                        {QStringLiteral("Date"), QStringLiteral("Sessions"), QStringLiteral("Total time"),
                         QStringLiteral("Average session")});
    m_students = addSection(QStringLiteral("Most active students"), QStringLiteral("Top 10 by total time."),
                            {QStringLiteral("Student"), QStringLiteral("Sessions"), QStringLiteral("Total time"),
                             QStringLiteral("Average session")});
    m_queue = addSection(QStringLiteral("Waiting time by allocation rule"),
                         QStringLiteral("IMMEDIATE = a computer was free; otherwise the queue policy that chose the "
                                        "student."),
                         {QStringLiteral("Granted by"), QStringLiteral("Requests"), QStringLiteral("Average wait"),
                          QStringLiteral("Longest wait")});
    root->addStretch(1);
}

ReportsPage::Section ReportsPage::addSection(const QString& title, const QString& caption, const QStringList& headers)
{
    QFrame* card = makeCard();
    cardLayout(card)->addWidget(makeLabel(title, QStringLiteral("sectionTitle")));
    cardLayout(card)->addWidget(makeLabel(caption, QStringLiteral("caption")));
    Section s;
    s.table = new QTableWidget();
    setupTable(s.table, headers);
    s.table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    alignNumericColumns(s.table, {1, 2, 3});
    s.table->setMinimumHeight(170);
    cardLayout(card)->addWidget(s.table);
    s.state = makeStateMessage();
    cardLayout(card)->addWidget(s.state);
    qobject_cast<QVBoxLayout*>(m_body->layout())->addWidget(card);
    return s;
}

void ReportsPage::refresh()
{
    const QString noSessions = QStringLiteral("No finished sessions yet.");
    std::vector<UsageRow> rows;
    QString error;
    bool ok = m_services.reports.usageByLab(rows, &error);
    auto usageFill = [](QTableWidget* t) {
        return [t](int r, const UsageRow& u) {
            t->setItem(r, 0, makeItem(u.label.isEmpty() ? u.key : QStringLiteral("%1 (%2)").arg(u.label, u.key)));
            t->setItem(r, 1, makeNumberItem(u.sessions));
            t->setItem(r, 2, durationCell(u.totalMinutes));
            t->setItem(r, 3, durationCell(u.averageMinutes));
        };
    };
    fill(m_labs.table, m_labs.state, ok, error, rows, noSessions, usageFill(m_labs.table));
    error.clear();
    ok = m_services.reports.usageByDay(14, rows, &error);
    fill(m_days.table, m_days.state, ok, error, rows, QStringLiteral("No finished sessions in the last 14 days."),
         usageFill(m_days.table));
    error.clear();
    ok = m_services.reports.topStudents(10, rows, &error);
    fill(m_students.table, m_students.state, ok, error, rows, noSessions, usageFill(m_students.table));

    std::vector<QueueRow> queue;
    error.clear();
    ok = m_services.reports.queueByPolicy(queue, &error);
    QTableWidget* qt = m_queue.table;
    fill(qt, m_queue.state, ok, error, queue,
         QStringLiteral("No requests have been allocated yet. They appear when students use the Student portal."),
         [qt](int r, const QueueRow& q) {
             qt->setItem(r, 0, makeItem(q.grantedBy));
             qt->setItem(r, 1, makeNumberItem(q.requests));
             qt->setItem(r, 2, durationCell(q.averageWaitMinutes));
             qt->setItem(r, 3, durationCell(q.longestWaitMinutes));
         });
}

}  // namespace labdesk::ui
