#pragma once

#include "page.h"

#include <QHash>

class QLabel;
class QTableWidget;

namespace labdesk {
struct Computer;
struct LabSummary;
struct Services;
}

namespace labdesk::ui {

class MessageBanner;
class ResponsiveGrid;
class StatusDonut;

class DashboardPage : public Page {
    Q_OBJECT
public:
    DashboardPage(Services& services, QWidget* parent = nullptr);
    void refresh() override;

signals:
    // "View computers" on a laboratory card.
    void openComputers(const QString& location);

private:
    QWidget* makeStat(const QString& key, const QString& label);
    void setStat(const QString& key, const QString& value, const QString& caption);
    QWidget* makeLabCard(const LabSummary& lab, const QList<Computer>& computers,
                         const QHash<QString, QString>& usedBy);
    void refreshLabs(const QHash<QString, QString>& usedBy);
    void refreshActiveSessions();
    void refreshActivity();

    Services& m_services;
    MessageBanner* m_banner = nullptr;
    MessageBanner* m_consistency = nullptr;
    ResponsiveGrid* m_stats = nullptr;
    QHash<QString, QLabel*> m_statValues;
    QHash<QString, QLabel*> m_statCaptions;
    ResponsiveGrid* m_labs = nullptr;
    QLabel* m_labsState = nullptr;
    StatusDonut* m_donut = nullptr;
    QHash<QString, QLabel*> m_legend;
    QTableWidget* m_activity = nullptr;
    QLabel* m_activityState = nullptr;
    QLabel* m_labsCaption = nullptr;
    QTableWidget* m_activeTable = nullptr;
    QLabel* m_activeState = nullptr;
};

}  // namespace labdesk::ui
