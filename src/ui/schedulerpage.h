#pragma once

#include "page.h"
#include "scheduler.h"

#include <optional>

class QComboBox;
class QLabel;
class QSpinBox;
class QTableWidget;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class GanttChartWidget;
class MessageBanner;

// CPU scheduling simulator. The page only gathers input and shows output;
// all scheduling logic lives in os::Scheduler (scheduler.h).
class SchedulerPage : public Page {
    Q_OBJECT
public:
    SchedulerPage(Services& services, QWidget* parent = nullptr);
    void refresh() override;  // reloads the workload list (sessions) only

signals:
    void scheduleComputed(const labdesk::os::ScheduleResult& result);

private:
    void addProcessRow(const QString& id, int arrival, int burst, int priority);
    void onAddProcess();
    void onRemoveProcess();
    void loadTextbookSample();
    bool readProcesses(std::vector<os::Process>& out);
    void onRun();
    void onCompareAll();
    void onSaveWorkload();
    void onLoadWorkload();
    std::optional<qint64> selectedWorkloadKey() const;
    void showResult(const os::ScheduleResult& result);

    Services& m_services;
    MessageBanner* m_banner = nullptr;
    QTableWidget* m_input = nullptr;
    QComboBox* m_workload = nullptr;
    QComboBox* m_algorithm = nullptr;
    QSpinBox* m_quantum = nullptr;
    QLabel* m_summary = nullptr;
    GanttChartWidget* m_gantt = nullptr;
    QTableWidget* m_results = nullptr;
    QTableWidget* m_comparison = nullptr;
    QLabel* m_comparisonTitle = nullptr;
};

}  // namespace labdesk::ui
