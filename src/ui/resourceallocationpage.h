#pragma once
// OS Management > Resource Allocation (administrators): the REAL lab seen as a
// resource-allocation system. Each laboratory is a resource type, each
// computer an instance, a session an allocation, ending it a release, and the
// waiting queue holds requests that cannot be satisfied yet. Algorithmic
// experiments on a simulated system are on the Deadlock Avoidance tab.

#include "page.h"

class QLabel;
class QTableWidget;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class ResourceAllocationPage : public Page {
    Q_OBJECT
public:
    ResourceAllocationPage(Services& services, QWidget* parent = nullptr);
    void refresh() override;

private:
    Services& m_services;
    QLabel* m_summary = nullptr;
    QTableWidget* m_resources = nullptr;
    QLabel* m_resourcesState = nullptr;
    QTableWidget* m_events = nullptr;
    QLabel* m_eventsState = nullptr;
};

}  // namespace labdesk::ui
