#include "studenthistorypage.h"

#include "services.h"
#include "timeutil.h"
#include "uihelpers.h"

#include <QDateTime>
#include <QFrame>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace labdesk::ui {

namespace {
double minutesBetween(const QString& a, const QString& b)
{
    const QDateTime x = QDateTime::fromString(a, Qt::ISODateWithMs);
    const QDateTime y = b.isEmpty() ? QDateTime::currentDateTimeUtc() : QDateTime::fromString(b, Qt::ISODateWithMs);
    if (!x.isValid() || !y.isValid())
        return 0;
    return x.msecsTo(y) / 60000.0;
}
}  // namespace

StudentHistoryPage::StudentHistoryPage(Services& services, const QString& studentId, QWidget* parent)
    : Page(parent), m_services(services), m_studentId(studentId)
{
    QVBoxLayout* root = makePageLayout(this, QStringLiteral("My Allocation History"),
                                       QStringLiteral("Every computer session you have had, newest first."));
    QFrame* card = makeCard();
    m_summary = makeLabel(QString(), QStringLiteral("caption"));
    cardLayout(card)->addWidget(m_summary);
    m_table = new QTableWidget();
    setupTable(m_table, {QStringLiteral("Session"), QStringLiteral("Computer"), QStringLiteral("Laboratory"),
                         QStringLiteral("Purpose"), QStringLiteral("Requested"), QStringLiteral("Start"),
                         QStringLiteral("End"), QStringLiteral("Waited"), QStringLiteral("Duration"),
                         QStringLiteral("Status")});
    alignNumericColumns(m_table, {7, 8});
    cardLayout(card)->addWidget(m_table, 1);
    m_state = makeStateMessage();
    cardLayout(card)->addWidget(m_state);
    root->addWidget(card, 1);
}

void StudentHistoryPage::refresh()
{
    std::vector<StudentVisit> visits;
    QString error;
    const bool ok = m_services.reports.studentHistory(m_studentId, visits, &error);
    m_table->setRowCount(static_cast<int>(visits.size()));
    double totalMinutes = 0;
    for (int r = 0; r < static_cast<int>(visits.size()); ++r) {
        const StudentVisit& v = visits[static_cast<std::size_t>(r)];
        const double duration = minutesBetween(v.startTime, v.endTime);
        if (!v.endTime.isEmpty())
            totalMinutes += duration;
        auto right = [](const QString& text) {
            QTableWidgetItem* item = makeItem(text);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return item;
        };
        m_table->setItem(r, 0, makeItem(QStringLiteral("#%1").arg(v.sessionId)));
        m_table->setItem(r, 1, makeItem(v.computerId));
        m_table->setItem(r, 2, makeItem(v.location));
        m_table->setItem(r, 3, makeItem(v.purpose.isEmpty() ? QStringLiteral("-")
                                                            : v.purpose.left(1) + v.purpose.mid(1).toLower()));
        m_table->setItem(r, 4, makeItem(timeutil::toDisplay(v.requestedAt)));
        m_table->setItem(r, 5, makeItem(timeutil::toDisplay(v.startTime)));
        m_table->setItem(r, 6, makeItem(v.endTime.isEmpty() ? QStringLiteral("in use") : timeutil::toDisplay(v.endTime)));
        m_table->setItem(r, 7, right(v.requestedAt.isEmpty() ? QStringLiteral("-")
                                                             : formatDuration(minutesBetween(v.requestedAt, v.startTime))));
        m_table->setItem(r, 8, right(formatDuration(duration)));
        m_table->setItem(r, 9, makeStatusItem(v.status));
    }
    m_table->resizeColumnsToContents();
    m_summary->setText(QStringLiteral("%1 session(s); total time at a computer: %2.")
                           .arg(static_cast<int>(visits.size()))
                           .arg(formatDuration(totalMinutes)));
    if (!ok) {
        m_table->hide();
        showErrorState(m_state, error);
    } else if (visits.empty()) {
        m_table->hide();
        showEmptyState(m_state, QStringLiteral("You have not used a lab computer yet. Request one on My Session."));
    } else {
        m_table->show();
        m_state->hide();
    }
}

}  // namespace labdesk::ui
