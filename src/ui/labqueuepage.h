#pragma once

#include "page.h"

#include <QHash>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class MessageBanner;

// Administrator view of the lab queue:
//  - choose the policy that assigns free computers (FCFS / SJF / Priority),
//  - see waiting students in the order they will be served,
//  - read MEASURED arrival / waiting / burst / turnaround times of finished visits,
//  - replay the recorded visits under each policy ("what if").
class LabQueuePage : public Page {
    Q_OBJECT
public:
    LabQueuePage(Services& services, QWidget* parent = nullptr);
    void refresh() override;

private:
    QWidget* buildPolicyCard();
    QWidget* buildQueueCard();
    QWidget* buildMeasuredCard();
    QWidget* buildReplayCard();
    void refreshQueue();
    void refreshMeasured();
    void onPolicyChosen();
    void onApplyPolicy();
    void onRemoveRequest();
    void onReplay();

    Services& m_services;
    MessageBanner* m_banner = nullptr;

    QComboBox* m_policy = nullptr;
    QLabel* m_policyHelp = nullptr;
    QLabel* m_currentPolicy = nullptr;

    QTableWidget* m_queue = nullptr;
    QLabel* m_queueState = nullptr;
    QPushButton* m_remove = nullptr;

    QHash<QString, QLabel*> m_figures;
    QTableWidget* m_history = nullptr;
    QLabel* m_historyState = nullptr;

    QSpinBox* m_computers = nullptr;
    QTableWidget* m_replay = nullptr;
    QLabel* m_replayState = nullptr;

    QTimer* m_tick = nullptr;  // keeps "waiting so far" current
};

}  // namespace labdesk::ui
