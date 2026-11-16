#pragma once
// OS Management > OS Simulator: one simulated lab computer, advanced tick by
// tick. All decisions are made by os::OsSimulator (ossimulator.h); this page
// only collects the programs and shows what the OS is doing.

#include "ossimulator.h"
#include "page.h"

#include <vector>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace labdesk::ui {

class GanttChartWidget;
class MessageBanner;

class OsSimulatorPage : public Page {
    Q_OBJECT
public:
    explicit OsSimulatorPage(QWidget* parent = nullptr);

private:
    QWidget* buildSetupCard();
    QWidget* buildRunCard();
    QWidget* buildMachineCards();
    QWidget* buildDetailsCard();
    void addProgramRow(const os::SimProgram& program);
    void fillPrograms(const std::vector<os::SimProgram>& programs);
    bool startIfNeeded();
    void setLocked(bool locked);
    void onStep();
    void onStartPause();
    void onRunToEnd();
    void onReset();
    void showState();
    QString nameOf(int index) const;

    os::OsSimulator m_sim;
    bool m_started = false;

    MessageBanner* m_banner = nullptr;
    QTableWidget* m_programs = nullptr;
    QComboBox* m_algorithm = nullptr;
    QSpinBox* m_quantum = nullptr;
    QSpinBox* m_memoryBlocks = nullptr;
    QComboBox* m_speed = nullptr;
    QWidget* m_setupControls = nullptr;
    QPushButton* m_startPause = nullptr;
    QLabel* m_clock = nullptr;
    QLabel* m_cpu = nullptr;
    QLabel* m_cpuDetail = nullptr;
    QWidget* m_memoryGrid = nullptr;
    QLabel* m_memoryDetail = nullptr;
    QLabel* m_readyQueue = nullptr;
    QTableWidget* m_pcb = nullptr;
    GanttChartWidget* m_gantt = nullptr;
    QListWidget* m_log = nullptr;
    QLabel* m_results = nullptr;
    QTimer* m_timer = nullptr;
};

}  // namespace labdesk::ui
