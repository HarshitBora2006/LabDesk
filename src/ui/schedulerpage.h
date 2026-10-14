#pragma once

#include "page.h"
#include "scheduler.h"

#include <QHash>

#include <optional>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class GanttChartWidget;
class MessageBanner;

// CPU scheduling simulator. The page gathers input and shows output; all
// scheduling logic lives in os::Scheduler (scheduler.h).
//
// Saved workloads: the PROCESS table stores simulation inputs (process ID,
// arrival, burst, priority) either unlinked (the demo workload) or linked to
// one lab session. Choosing a session loads that session's details and its
// saved workload. Session duration is never turned into CPU burst times.
class SchedulerPage : public Page {
    Q_OBJECT
public:
    SchedulerPage(Services& services, QWidget* parent = nullptr);
    void refresh() override;  // refreshes the workload list; never discards table edits

signals:
    void scheduleComputed(const labdesk::os::ScheduleResult& result);

private:
    enum class RowSource { Saved, New, Example, Measured };

    QWidget* buildWorkloadCard();
    QWidget* buildProcessCard();
    QWidget* buildAlgorithmCard();
    QWidget* buildResultsCard();

    // workload handling
    void onWorkloadChosen(int index);
    void loadSelectedWorkload();
    void showSessionDetails(std::optional<qint64> key);
    std::optional<qint64> selectedWorkloadKey() const;
    bool measuredSelected() const;
    int loadMeasuredVisits(QString* error);  // fills the table; returns row count
    QString workloadName(std::optional<qint64> key) const;
    void updateWorkloadStatus();
    void onSaveWorkload();
    void markDirty();

    // process table
    void addProcessRow(const QString& id, int arrival, int burst, int priority, RowSource source,
                       qint64 databaseRow = 0);
    void onAddProcess();
    void onRemoveProcess();
    void onClearProcesses();
    void loadTextbookSample();
    void onCellChanged(QTableWidgetItem* item);
    bool readProcesses(std::vector<os::Process>& out);
    void updateTableState();
    enum class TimeLabels { Abstract, Minutes, Seconds };
    void setTimeUnit(TimeLabels labels);  // column headings and quantum label show the unit in use
    void onMeasuredUnitChanged();

    // simulation
    void onAlgorithmChanged();
    void onRun();
    void onCompareAll();
    void showResult(const os::ScheduleResult& result);
    void clearResults();

    Services& m_services;
    MessageBanner* m_banner = nullptr;

    QComboBox* m_workload = nullptr;
    QLabel* m_sessionDetails = nullptr;
    MessageBanner* m_workloadStatus = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_revert = nullptr;
    QWidget* m_timeUnitRow = nullptr;      // shown only for measured lab visits
    QComboBox* m_timeUnit = nullptr;

    QTableWidget* m_input = nullptr;
    QLabel* m_inputState = nullptr;

    QComboBox* m_algorithm = nullptr;
    QLabel* m_algorithmHelp = nullptr;
    QLabel* m_quantumLabel = nullptr;
    QSpinBox* m_quantum = nullptr;

    QLabel* m_resultTitle = nullptr;
    QHash<QString, QLabel*> m_metricValues;
    QWidget* m_metrics = nullptr;
    GanttChartWidget* m_gantt = nullptr;
    QWidget* m_ganttArea = nullptr;
    QTableWidget* m_results = nullptr;
    QLabel* m_resultsState = nullptr;
    QLabel* m_comparisonTitle = nullptr;
    QTableWidget* m_comparison = nullptr;

    // What the table currently shows, relative to the database.
    bool m_loaded = false;                 // a workload has been loaded at least once
    std::optional<qint64> m_loadedKey;     // workload the table belongs to
    bool m_hasSavedWorkload = false;       // database had rows for m_loadedKey at load time
    QList<qint64> m_savedRowIds;           // process_id values of those rows
    bool m_dirty = false;                  // table differs from what was loaded/saved
    bool m_loadedMeasured = false;         // table holds the read-only measured lab visits
    int m_measuredTotal = 0;               // finished visits available when loaded
    bool m_measuredInSeconds = false;      // unit of the loaded measured values (default minutes)
    int m_distortedVisits = 0;             // visits changed by >10 % by rounding to the unit
    bool m_populating = false;             // suppresses dirty tracking while filling the table
};

}  // namespace labdesk::ui
