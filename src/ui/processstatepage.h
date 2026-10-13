#pragma once

#include "page.h"
#include "processstate.h"
#include "scheduler.h"

#include <vector>

class QLabel;
class QPushButton;
class QSlider;
class QTableWidget;

namespace labdesk::ui {

class MessageBanner;
class StateDiagramWidget;

// Educational process-lifecycle demonstration. Two parts:
//  1. An interactive single-CPU simulator where the user drives transitions
//     (including RUNNING -> WAITING for I/O) and illegal ones are refused.
//  2. The state trace produced by the last scheduler run, with a time slider
//     that shows every process's state at any instant.
// None of this touches real OS processes or the computer-session table.
class ProcessStatePage : public Page {
    Q_OBJECT
public:
    explicit ProcessStatePage(QWidget* parent = nullptr);

public slots:
    void showSchedule(const labdesk::os::ScheduleResult& result);

private:
    QWidget* buildSimulatorTab();
    QWidget* buildTraceTab();
    void refreshSimulator();
    void applyTransition(os::ProcessState to);
    void onProcessSelected();
    QString selectedProcess() const;
    void updateSnapshot(int time);

    // simulator
    os::LifecycleSimulator m_sim;
    MessageBanner* m_banner = nullptr;
    QTableWidget* m_processes = nullptr;
    QTableWidget* m_events = nullptr;
    StateDiagramWidget* m_diagram = nullptr;
    struct ActionButton {
        os::ProcessState from;
        os::ProcessState to;
        QPushButton* button;
    };
    std::vector<ActionButton> m_actions;  // one button per textbook transition

    // trace
    os::ScheduleResult m_trace;
    bool m_hasTrace = false;
    QLabel* m_traceTitle = nullptr;
    QSlider* m_slider = nullptr;
    QLabel* m_sliderLabel = nullptr;
    QTableWidget* m_snapshot = nullptr;
    QTableWidget* m_traceEvents = nullptr;
};

}  // namespace labdesk::ui
