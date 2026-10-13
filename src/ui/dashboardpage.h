#pragma once

#include "page.h"

#include <QHash>

class QLabel;
class QScrollArea;
class QTableWidget;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class DashboardPage : public Page {
    Q_OBJECT
public:
    DashboardPage(Services& services, QWidget* parent = nullptr);
    void refresh() override;

private:
    QWidget* makeStat(const QString& key, const QString& label, const QString& accent);
    void rebuildFloor();

    Services& m_services;
    QHash<QString, QLabel*> m_statValues;
    QLabel* m_consistency = nullptr;
    QScrollArea* m_floorScroll = nullptr;
    QTableWidget* m_activeTable = nullptr;
    QLabel* m_activeEmpty = nullptr;
};

}  // namespace labdesk::ui
