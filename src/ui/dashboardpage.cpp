#include "dashboardpage.h"

#include "services.h"
#include "theme.h"
#include "timeutil.h"
#include "uihelpers.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

namespace labdesk::ui {

DashboardPage::DashboardPage(Services& services, QWidget* parent) : Page(parent), m_services(services)
{
    QVBoxLayout* root = makePageLayout(this, QStringLiteral("Dashboard"),
                                       QStringLiteral("Live figures read from the database on every refresh."));

    auto* stats = new QGridLayout();
    stats->setHorizontalSpacing(12);
    stats->setVerticalSpacing(12);
    stats->addWidget(makeStat(QStringLiteral("total"), QStringLiteral("Total computers"), QStringLiteral("#1E2B45")), 0, 0);
    stats->addWidget(makeStat(QStringLiteral("available"), QStringLiteral("Available"), QStringLiteral("#24634F")), 0, 1);
    stats->addWidget(makeStat(QStringLiteral("occupied"), QStringLiteral("Occupied"), QStringLiteral("#7E550F")), 0, 2);
    stats->addWidget(makeStat(QStringLiteral("maintenance"), QStringLiteral("Under maintenance"), QStringLiteral("#3F4D68")), 1, 0);
    stats->addWidget(makeStat(QStringLiteral("sessions"), QStringLiteral("Active sessions"), QStringLiteral("#1E2B45")), 1, 1);
    stats->addWidget(makeStat(QStringLiteral("students"), QStringLiteral("Registered students"), QStringLiteral("#1E2B45")), 1, 2);
    root->addLayout(stats);

    m_consistency = makeLabel(QString(), QStringLiteral("banner"));
    root->addWidget(m_consistency);

    auto* lower = new QHBoxLayout();
    lower->setSpacing(12);

    QFrame* floorCard = makeCard();
    QVBoxLayout* floorLayout = cardLayout(floorCard);
    floorLayout->addWidget(makeLabel(QStringLiteral("Lab floor"), QStringLiteral("sectionTitle")));
    auto* legend = new QHBoxLayout();
    for (const QString& s : {QStringLiteral("AVAILABLE"), QStringLiteral("OCCUPIED"), QStringLiteral("MAINTENANCE")}) {
        const auto c = theme::statusColors(s);
        auto* chip = new QLabel(s.left(1) + s.mid(1).toLower());
        chip->setStyleSheet(QStringLiteral("background:%1; color:%2; border-radius:4px; padding:2px 8px;")
                                .arg(c.background.name(), c.foreground.name()));
        legend->addWidget(chip);
    }
    legend->addStretch();
    floorLayout->addLayout(legend);
    m_floorScroll = new QScrollArea();
    m_floorScroll->setWidgetResizable(true);
    m_floorScroll->setFrameShape(QFrame::NoFrame);
    m_floorScroll->setMinimumHeight(220);
    floorLayout->addWidget(m_floorScroll, 1);
    lower->addWidget(floorCard, 3);

    QFrame* activeCard = makeCard();
    QVBoxLayout* activeLayout = cardLayout(activeCard);
    activeLayout->addWidget(makeLabel(QStringLiteral("Active sessions"), QStringLiteral("sectionTitle")));
    m_activeTable = new QTableWidget();
    setupTable(m_activeTable, {QStringLiteral("Student"), QStringLiteral("Computer"), QStringLiteral("Started")});
    activeLayout->addWidget(m_activeTable, 1);
    m_activeEmpty = makeLabel(QStringLiteral("No one is using a lab computer right now."), QStringLiteral("emptyState"));
    activeLayout->addWidget(m_activeEmpty);
    lower->addWidget(activeCard, 2);

    root->addLayout(lower, 1);
}

QWidget* DashboardPage::makeStat(const QString& key, const QString& label, const QString& accent)
{
    QFrame* card = makeCard();
    QVBoxLayout* layout = cardLayout(card);
    layout->setSpacing(2);
    auto* value = makeLabel(QStringLiteral("-"), QStringLiteral("statValue"));
    value->setStyleSheet(QStringLiteral("color:%1;").arg(accent));
    layout->addWidget(value);
    layout->addWidget(makeLabel(label, QStringLiteral("statLabel")));
    m_statValues.insert(key, value);
    return card;
}

void DashboardPage::refresh()
{
    QString error;
    const DashboardStats s = m_services.computers.dashboardStats(&error);
    m_statValues.value(QStringLiteral("total"))->setText(QString::number(s.totalComputers));
    m_statValues.value(QStringLiteral("available"))->setText(QString::number(s.available));
    m_statValues.value(QStringLiteral("occupied"))->setText(QString::number(s.occupied));
    m_statValues.value(QStringLiteral("maintenance"))->setText(QString::number(s.maintenance));
    m_statValues.value(QStringLiteral("sessions"))->setText(QString::number(s.activeSessions));
    m_statValues.value(QStringLiteral("students"))->setText(QString::number(s.students));

    const QStringList problems = m_services.computers.consistencyProblems(&error);
    QString kind;
    if (!error.isEmpty()) {
        kind = QStringLiteral("error");
        m_consistency->setText(QStringLiteral("Could not read the database: ") + error);
    } else if (problems.isEmpty()) {
        kind = QStringLiteral("success");
        m_consistency->setText(
            QStringLiteral("Consistency check passed: every OCCUPIED computer has exactly one active session, "
                           "and every computer in MAINTENANCE has an open issue (%1 open).")
                .arg(s.openMaintenance));
    } else {
        kind = QStringLiteral("error");
        m_consistency->setText(QStringLiteral("Consistency problems found:\n") + problems.join(QLatin1Char('\n')));
    }
    m_consistency->setProperty("kind", kind);
    m_consistency->style()->unpolish(m_consistency);
    m_consistency->style()->polish(m_consistency);

    const auto active = m_services.sessions.listSessions(true, 500);
    m_activeTable->setRowCount(static_cast<int>(active.size()));
    for (int row = 0; row < static_cast<int>(active.size()); ++row) {
        const SessionRecord& r = active[static_cast<std::size_t>(row)];
        m_activeTable->setItem(row, 0, makeItem(QStringLiteral("%1 (%2)").arg(r.studentName, r.studentId)));
        m_activeTable->setItem(row, 1, makeItem(r.computerId));
        m_activeTable->setItem(row, 2, makeItem(timeutil::toDisplay(r.startTime)));
    }
    m_activeTable->resizeColumnsToContents();
    m_activeTable->setVisible(!active.empty());
    m_activeEmpty->setVisible(active.empty());

    rebuildFloor();
}

void DashboardPage::rebuildFloor()
{
    QHash<QString, QString> usedBy;
    for (const SessionRecord& r : m_services.sessions.listSessions(true, 1000))
        usedBy.insert(r.computerId, r.studentName + QStringLiteral(" (") + r.studentId + QLatin1Char(')'));

    auto* body = new QWidget();
    auto* grid = new QGridLayout(body);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(8);
    const auto computers = m_services.computers.listComputers();
    if (computers.empty()) {
        grid->addWidget(makeLabel(QStringLiteral("No computers yet. Add them on the Computers page."),
                                  QStringLiteral("emptyState")),
                        0, 0);
    }
    const int columns = 6;
    for (int i = 0; i < static_cast<int>(computers.size()); ++i) {
        const Computer& c = computers[static_cast<std::size_t>(i)];
        const auto colors = theme::statusColors(c.status);
        auto* tile = new QLabel(QStringLiteral("<b>%1</b><br><span style='font-size:8.5pt'>%2</span>")
                                    .arg(c.computerId.toHtmlEscaped(), c.status.toHtmlEscaped()));
        tile->setTextFormat(Qt::RichText);
        tile->setAlignment(Qt::AlignCenter);
        tile->setMinimumSize(104, 50);
        tile->setStyleSheet(QStringLiteral("background:%1; color:%2; border-radius:6px; padding:4px;")
                                .arg(colors.background.name(), colors.foreground.name()));
        QString tip = QStringLiteral("%1\n%2\n%3").arg(c.name, c.location, c.status);
        if (usedBy.contains(c.computerId))
            tip += QStringLiteral("\nIn use by ") + usedBy.value(c.computerId);
        tile->setToolTip(tip);
        grid->addWidget(tile, i / columns, i % columns);
    }
    grid->setRowStretch(grid->rowCount(), 1);
    grid->setColumnStretch(columns, 1);
    m_floorScroll->setWidget(body);  // deletes the previous body widget
}

}  // namespace labdesk::ui
