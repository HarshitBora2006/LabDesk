#include "resourceallocationpage.h"

#include "services.h"
#include "timeutil.h"
#include "uihelpers.h"

#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace labdesk::ui {

ResourceAllocationPage::ResourceAllocationPage(Services& services, QWidget* parent)
    : Page(parent), m_services(services)
{
    QWidget* body = makeScrollPage(this);
    QVBoxLayout* root = makePageLayout(body, QStringLiteral("Resource Allocation"),
                                       QStringLiteral("The real lab viewed as a resource-allocation system."));
    root->addWidget(makeExplainer(QStringLiteral(
        "Model used here (real data from the database): each laboratory is a resource type and each computer is one "
        "instance of it. Starting a session ALLOCATES an instance, ending it RELEASES the instance, an instance under "
        "maintenance is out of service, and a student in the waiting queue is a request that cannot be satisfied yet. "
        "A student can hold at most one computer and never waits while holding one, so the hold-and-wait condition "
        "for deadlock cannot occur in the real lab. For algorithmic experiments (several resource types, safe "
        "states), use the Deadlock Avoidance tab, which works on a separate simulated system.")));

    QFrame* card = makeCard();
    cardLayout(card)->addWidget(makeLabel(QStringLiteral("Resource instances per laboratory"), QStringLiteral("sectionTitle")));
    m_summary = makeLabel(QString(), QStringLiteral("caption"));
    cardLayout(card)->addWidget(m_summary);
    m_resources = new QTableWidget();
    setupTable(m_resources, {QStringLiteral("Laboratory (resource type)"), QStringLiteral("Instances"),
                             QStringLiteral("Allocated"), QStringLiteral("Available"),
                             QStringLiteral("Out of service")});
    m_resources->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    alignNumericColumns(m_resources, {1, 2, 3, 4});
    m_resources->setMinimumHeight(180);
    cardLayout(card)->addWidget(m_resources);
    m_resourcesState = makeStateMessage();
    cardLayout(card)->addWidget(m_resourcesState);
    root->addWidget(card);

    QFrame* events = makeCard();
    cardLayout(events)->addWidget(makeLabel(QStringLiteral("Recent allocation and release events"), QStringLiteral("sectionTitle")));
    cardLayout(events)->addWidget(makeLabel(QStringLiteral("Taken from the session and request records (newest first)."),
                                            QStringLiteral("caption")));
    m_events = new QTableWidget();
    setupTable(m_events, {QStringLiteral("Time"), QStringLiteral("Event"), QStringLiteral("Details")});
    m_events->setMinimumHeight(240);
    cardLayout(events)->addWidget(m_events);
    m_eventsState = makeStateMessage();
    cardLayout(events)->addWidget(m_eventsState);
    root->addWidget(events);
    root->addStretch(1);
}

void ResourceAllocationPage::refresh()
{
    std::vector<LabSummary> labs;
    QString error;
    const bool ok = m_services.computers.labSummaries(labs, &error);
    m_resources->setRowCount(static_cast<int>(labs.size()));
    int total = 0, allocated = 0, available = 0;
    for (int row = 0; row < static_cast<int>(labs.size()); ++row) {
        const LabSummary& l = labs[static_cast<std::size_t>(row)];
        m_resources->setItem(row, 0, makeItem(l.location));
        m_resources->setItem(row, 1, makeNumberItem(l.total));
        m_resources->setItem(row, 2, makeNumberItem(l.occupied));
        m_resources->setItem(row, 3, makeNumberItem(l.available));
        m_resources->setItem(row, 4, makeNumberItem(l.maintenance));
        total += l.total;
        allocated += l.occupied;
        available += l.available;
    }
    const DashboardStats stats = m_services.computers.dashboardStats();
    m_summary->setText(QStringLiteral("%1 instance(s) in total: %2 allocated, %3 available. Requests waiting in the "
                                      "queue: %4.")
                           .arg(total)
                           .arg(allocated)
                           .arg(available)
                           .arg(stats.waitingRequests));
    if (!ok) {
        m_resources->hide();
        showErrorState(m_resourcesState, error);
    } else if (labs.empty()) {
        m_resources->hide();
        showEmptyState(m_resourcesState, QStringLiteral("No computers are registered, so there are no resources yet."));
    } else {
        m_resources->show();
        m_resourcesState->hide();
    }

    std::vector<ActivityItem> activity;
    QString activityError;
    const bool activityOk = m_services.reports.recentActivity(60, activity, &activityError);
    std::vector<ActivityItem> events;
    for (const ActivityItem& a : activity)
        if (a.kind == QStringLiteral("SESSION_START") || a.kind == QStringLiteral("SESSION_END")
            || a.kind == QStringLiteral("QUEUE_JOIN"))
            events.push_back(a);
    m_events->setRowCount(static_cast<int>(events.size()));
    for (int row = 0; row < static_cast<int>(events.size()); ++row) {
        const ActivityItem& a = events[static_cast<std::size_t>(row)];
        const QString kind = a.kind == QStringLiteral("SESSION_START") ? QStringLiteral("Allocate")
                             : a.kind == QStringLiteral("SESSION_END") ? QStringLiteral("Release")
                                                                       : QStringLiteral("Request waits");
        m_events->setItem(row, 0, makeItem(timeutil::toDisplay(a.time)));
        m_events->setItem(row, 1, makeItem(kind));
        m_events->setItem(row, 2, makeItem(a.text));
    }
    m_events->resizeColumnsToContents();
    if (!activityOk) {
        m_events->hide();
        showErrorState(m_eventsState, activityError);
    } else if (events.empty()) {
        m_events->hide();
        showEmptyState(m_eventsState, QStringLiteral("No allocations yet. Events appear when students start and end "
                                                     "sessions."));
    } else {
        m_events->show();
        m_eventsState->hide();
    }
}

}  // namespace labdesk::ui
