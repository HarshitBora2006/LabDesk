#include "processstatepage.h"

#include "statediagramwidget.h"
#include "uihelpers.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <map>
#include <optional>
#include <utility>

namespace labdesk::ui {

using S = os::ProcessState;

namespace {

QString stateName(S s) { return QString::fromLatin1(os::toString(s)); }

// Button / history wording for each edge of the state diagram.
QString actionLabel(S from, S to)
{
    if (from == S::New && to == S::Ready) return QStringLiteral("Admit to ready queue");
    if (from == S::Ready && to == S::Running) return QStringLiteral("Dispatch to CPU");
    if (from == S::Running && to == S::Ready) return QStringLiteral("Preempt / return to ready");
    if (from == S::Running && to == S::Waiting) return QStringLiteral("Block for I/O");
    if (from == S::Waiting && to == S::Ready) return QStringLiteral("Complete I/O");
    if (from == S::Running && to == S::Terminated) return QStringLiteral("Terminate process");
    return QStringLiteral("%1 to %2").arg(stateName(from), stateName(to));
}

QWidget* makeTabPage()
{
    auto* tab = new QWidget();
    tab->setObjectName(QStringLiteral("tabPage"));
    tab->setAttribute(Qt::WA_StyledBackground, true);
    return tab;
}

}  // namespace

ProcessStatePage::ProcessStatePage(QWidget* parent) : Page(parent)
{
    QWidget* body = makeScrollPage(this);
    QVBoxLayout* root = makePageLayout(body, QStringLiteral("Process Management"),
                                       QStringLiteral("Simulated Processes - Educational OS Demonstration"));
    root->addWidget(makeExplainer(QStringLiteral(
        "Create simulated processes and move them through the operating-system states: admit, dispatch to the CPU, "
        "run, block for I/O, complete the I/O, preempt and terminate. Names such as chrome.exe are sample labels for "
        "SIMULATED processes. LabDesk does not read, start or stop real Windows processes, and a student's lab "
        "session is a database record, not a process.")));

    auto* tabs = new QTabWidget();
    tabs->addTab(buildSimulatorTab(), QStringLiteral("Lifecycle simulator"));
    tabs->addTab(buildTraceTab(), QStringLiteral("Trace from CPU scheduling"));
    root->addWidget(tabs, 1);
    refreshSimulator();
}

QWidget* ProcessStatePage::buildLegend()
{
    auto* legend = new QWidget();
    auto* grid = new QGridLayout(legend);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    int row = 0;
    for (S s : {S::New, S::Ready, S::Running, S::Waiting, S::Terminated}) {
        const QString name = s == S::Waiting ? QStringLiteral("WAITING / BLOCKED") : stateName(s);
        grid->addWidget(makeStatusChip(stateName(s), name), row, 0, Qt::AlignLeft | Qt::AlignTop);
        grid->addWidget(makeLabel(QString::fromStdString(os::stateDescription(s)), QStringLiteral("caption")), row, 1);
        ++row;
    }
    grid->setColumnStretch(1, 1);
    return legend;
}

QWidget* ProcessStatePage::buildSimulatorTab()
{
    QWidget* tab = makeTabPage();
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);
    m_banner = new MessageBanner();
    layout->addWidget(m_banner);

    auto* body = new QHBoxLayout();
    body->setSpacing(20);

    // ---- left: processes and actions
    auto* left = new QVBoxLayout();
    left->setSpacing(10);
    left->addWidget(makeLabel(QStringLiteral("Processes"), QStringLiteral("sectionTitle")));
    auto* addRow = new QHBoxLayout();
    addRow->setSpacing(8);
    m_newName = new QComboBox();
    m_newName->setEditable(true);
    m_newName->addItems({QStringLiteral("chrome.exe"), QStringLiteral("python.exe"), QStringLiteral("Code.exe"),
                         QStringLiteral("notepad.exe")});
    m_newName->setToolTip(QStringLiteral("Sample name for a simulated process; you can type any name"));
    m_newPriority = new QSpinBox();
    m_newPriority->setRange(0, 99);
    m_newPriority->setPrefix(QStringLiteral("Priority "));
    m_newPriority->setToolTip(QStringLiteral("0-99, lower number = higher priority"));
    m_newBurst = new QSpinBox();
    m_newBurst->setRange(1, 1000);
    m_newBurst->setValue(4);
    m_newBurst->setPrefix(QStringLiteral("Burst "));
    m_newBurst->setToolTip(QStringLiteral("CPU time the process needs, in time units"));
    auto* create = makeButton(QStringLiteral("Add simulated process"), QStringLiteral("primary"));
    addRow->addWidget(m_newName, 2);
    addRow->addWidget(m_newPriority, 1);
    addRow->addWidget(m_newBurst, 1);
    addRow->addWidget(create);
    left->addLayout(addRow);
    auto* createRow = new QHBoxLayout();
    createRow->setSpacing(8);
    m_fromSchedule = makeButton(QStringLiteral("Create from last CPU scheduling run"));
    m_fromSchedule->setToolTip(QStringLiteral("Run a simulation on the CPU Scheduling page first"));
    m_fromSchedule->setEnabled(false);
    auto* reset = makeButton(QStringLiteral("Reset simulation"));
    createRow->addWidget(m_fromSchedule);
    createRow->addStretch(1);
    createRow->addWidget(reset);
    left->addLayout(createRow);

    m_processes = new QTableWidget();
    setupTable(m_processes, {QStringLiteral("PID"), QStringLiteral("Process name"), QStringLiteral("State"),
                             QStringLiteral("Priority"), QStringLiteral("Arrival"), QStringLiteral("Burst"),
                             QStringLiteral("Remaining"), QStringLiteral("CPU time")});
    m_processes->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    alignNumericColumns(m_processes, {0, 3, 4, 5, 6, 7});
    m_processes->setMinimumHeight(170);
    left->addWidget(m_processes, 1);
    m_processesState = makeStateMessage();
    left->addWidget(m_processesState);

    left->addWidget(makeLabel(QStringLiteral("Actions for the selected process"), QStringLiteral("sectionTitle")));
    m_cpuStatus = makeLabel(QString(), QStringLiteral("caption"));
    left->addWidget(m_cpuStatus);
    const std::pair<S, S> edges[] = {{S::New, S::Ready},     {S::Ready, S::Running},  {S::Running, S::Ready},
                                     {S::Running, S::Waiting}, {S::Waiting, S::Ready}, {S::Running, S::Terminated}};
    auto* actionGrid = new QGridLayout();
    actionGrid->setHorizontalSpacing(8);
    actionGrid->setVerticalSpacing(8);
    int index = 0;
    for (const auto& edge : edges) {
        auto* button = makeButton(actionLabel(edge.first, edge.second));
        actionGrid->addWidget(button, index / 2, index % 2);
        ++index;
        m_actions.push_back({edge.first, edge.second, button});
        const S from = edge.first;
        const S to = edge.second;
        connect(button, &QPushButton::clicked, this, [this, from, to]() { applyTransition(from, to); });
    }
    m_runTick = makeButton(QStringLiteral("Run 1 time unit"), QStringLiteral("primary"));
    m_runTick->setToolTip(QStringLiteral("The RUNNING process executes for one time unit (CPU time +1, remaining -1)"));
    actionGrid->addWidget(m_runTick, index / 2, 0, 1, 2);
    connect(m_runTick, &QPushButton::clicked, this, &ProcessStatePage::onRunTick);
    actionGrid->setColumnStretch(0, 1);
    actionGrid->setColumnStretch(1, 1);
    left->addLayout(actionGrid);
    left->addWidget(makeLabel(QStringLiteral("Buttons with a dashed border are not allowed for the selected process "
                                             "right now; click one to see why. Only one process can be RUNNING at a "
                                             "time because the simulation has one CPU."),
                              QStringLiteral("caption")));
    body->addLayout(left, 1);

    // ---- right: what is happening
    auto* right = new QVBoxLayout();
    right->setSpacing(10);
    m_selectedSummary = makeLabel(QString(), QStringLiteral("sectionTitle"));
    right->addWidget(m_selectedSummary);
    m_lastAction = makeExplainer(QString());
    right->addWidget(m_lastAction);
    m_diagram = new StateDiagramWidget();
    right->addWidget(m_diagram);
    right->addWidget(makeLabel(QStringLiteral("What the states mean"), QStringLiteral("sectionTitle")));
    right->addWidget(buildLegend());
    body->addLayout(right, 1);
    layout->addLayout(body);

    layout->addWidget(makeLabel(QStringLiteral("Transition history (newest first)"), QStringLiteral("sectionTitle")));
    m_events = new QTableWidget();
    setupTable(m_events, {QStringLiteral("Tick"), QStringLiteral("Process"), QStringLiteral("Action"),
                          QStringLiteral("Previous state"), QStringLiteral("New state")});
    m_events->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    alignNumericColumns(m_events, {0});
    m_events->setMinimumHeight(180);
    layout->addWidget(m_events);
    m_eventsState = makeStateMessage();
    layout->addWidget(m_eventsState);

    connect(create, &QPushButton::clicked, this, &ProcessStatePage::onAddProcess);
    connect(m_fromSchedule, &QPushButton::clicked, this, &ProcessStatePage::createFromSchedule);
    connect(reset, &QPushButton::clicked, this, [this]() {
        if (!m_sim.processes().empty()
            && !confirm(this, QStringLiteral("Reset simulation"),
                        QStringLiteral("Remove all simulated processes and the transition history?"),
                        QStringLiteral("Reset"))) {
            return;
        }
        m_sim.reset();
        m_banner->showInfo(QStringLiteral("Simulation reset. Create a process to start again."));
        refreshSimulator();
    });
    connect(m_processes, &QTableWidget::itemSelectionChanged, this, &ProcessStatePage::onProcessSelected);
    return tab;
}

QWidget* ProcessStatePage::buildTraceTab()
{
    QWidget* tab = makeTabPage();
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    m_traceTitle = makeLabel(QStringLiteral("Run an algorithm on the CPU scheduling page to see the states its "
                                            "processes went through."),
                             QStringLiteral("caption"));
    layout->addWidget(m_traceTitle);

    auto* sliderRow = new QHBoxLayout();
    m_sliderLabel = makeLabel(QStringLiteral("Time 0"), QStringLiteral("sectionTitle"));
    m_sliderLabel->setMinimumWidth(90);
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setEnabled(false);
    sliderRow->addWidget(m_sliderLabel);
    sliderRow->addWidget(m_slider, 1);
    layout->addLayout(sliderRow);

    auto* tables = new QHBoxLayout();
    tables->setSpacing(14);
    m_snapshot = new QTableWidget();
    setupTable(m_snapshot, {QStringLiteral("Process ID"), QStringLiteral("State at this time")});
    m_snapshot->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tables->addWidget(m_snapshot, 2);
    m_traceEvents = new QTableWidget();
    setupTable(m_traceEvents, {QStringLiteral("Time"), QStringLiteral("Process ID"), QStringLiteral("Previous state"),
                               QStringLiteral("New state"), QStringLiteral("Reason")});
    alignNumericColumns(m_traceEvents, {0});
    tables->addWidget(m_traceEvents, 3);
    layout->addLayout(tables, 1);
    tab->setMinimumHeight(420);

    connect(m_slider, &QSlider::valueChanged, this, &ProcessStatePage::updateSnapshot);
    return tab;
}

QString ProcessStatePage::selectedProcess() const
{
    const auto items = m_processes->selectedItems();
    if (items.isEmpty())
        return QString();
    const QTableWidgetItem* item = m_processes->item(items.first()->row(), 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void ProcessStatePage::onProcessSelected()
{
    const QString id = selectedProcess();
    const os::SimulatedProcess* p = id.isEmpty() ? nullptr : m_sim.find(id.toStdString());
    m_diagram->setCurrentState(p ? std::optional<S>(p->state) : std::nullopt);
    if (p) {
        m_selectedSummary->setText(QStringLiteral("PID %1 (%2) is %3")
                                       .arg(p->pid)
                                       .arg(QString::fromStdString(p->name), stateName(p->state)));
    } else {
        m_selectedSummary->setText(m_sim.processes().empty() ? QStringLiteral("No process yet")
                                                             : QStringLiteral("Select a process"));
    }
    for (const ActionButton& a : m_actions) {
        a.button->setEnabled(p != nullptr);
        const bool allowed = p && p->state == a.from;
        a.button->setProperty("allowed", (!p || allowed) ? QStringLiteral("true") : QStringLiteral("false"));
        repolish(a.button);
        if (!p)
            a.button->setToolTip(QStringLiteral("Select a process first"));
        else if (allowed)
            a.button->setToolTip(QStringLiteral("Allowed: %1 to %2").arg(stateName(a.from), stateName(a.to)));
        else
            a.button->setToolTip(QString::fromStdString(os::explainInvalidTransition(id.toStdString(), p->state, a.to)));
    }
    const auto running = m_sim.runningProcess();
    const os::SimulatedProcess* runner = running ? m_sim.find(*running) : nullptr;
    m_cpuStatus->setText(runner ? QStringLiteral("CPU: running PID %1 (%2). Simulation clock: %3")
                                      .arg(runner->pid)
                                      .arg(QString::fromStdString(runner->name))
                                      .arg(m_sim.clock())
                                : QStringLiteral("CPU: idle (no process is RUNNING). Simulation clock: %1")
                                      .arg(m_sim.clock()));
    m_runTick->setEnabled(runner != nullptr);
}

void ProcessStatePage::refreshSimulator()
{
    const QString keep = selectedProcess();
    m_processes->blockSignals(true);
    const auto& processes = m_sim.processes();
    m_processes->setRowCount(static_cast<int>(processes.size()));
    int reselect = -1;
    for (int r = 0; r < static_cast<int>(processes.size()); ++r) {
        const os::SimulatedProcess& p = processes[static_cast<std::size_t>(r)];
        const QString id = QString::fromStdString(p.id);
        auto* pidItem = makeNumberItem(p.pid);
        pidItem->setData(Qt::UserRole, id);  // the simulator's key for this process
        m_processes->setItem(r, 0, pidItem);
        m_processes->setItem(r, 1, makeItem(QString::fromStdString(p.name)));
        m_processes->setItem(r, 2, makeStatusItem(stateName(p.state)));
        m_processes->setItem(r, 3, makeNumberItem(p.priority));
        m_processes->setItem(r, 4, makeNumberItem(p.arrival));
        const bool timed = p.burst > 0;
        auto numberOrDash = [](bool known, int value) {
            QTableWidgetItem* item = known ? makeNumberItem(value) : makeItem(QStringLiteral("-"));
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return item;
        };
        m_processes->setItem(r, 5, numberOrDash(timed, p.burst));
        m_processes->setItem(r, 6, numberOrDash(timed, p.remaining));
        m_processes->setItem(r, 7, makeNumberItem(p.cpuTime));
        if (id == keep)
            reselect = r;
    }
    if (reselect >= 0)
        m_processes->selectRow(reselect);
    m_processes->blockSignals(false);
    if (processes.empty()) {
        m_processes->hide();
        showEmptyState(m_processesState, QStringLiteral("No simulated processes yet. Press \"Create process\" or "
                                                        "\"Create from last CPU scheduling run\"."));
    } else {
        m_processes->show();
        m_processesState->hide();
    }

    const auto& events = m_sim.events();
    m_events->setRowCount(static_cast<int>(events.size()));
    for (int r = 0; r < static_cast<int>(events.size()); ++r) {
        const os::StateEvent& e = events[events.size() - 1 - static_cast<std::size_t>(r)];  // newest first
        m_events->setItem(r, 0, makeNumberItem(e.time));
        const os::SimulatedProcess* who = m_sim.find(e.processId);
        m_events->setItem(r, 1, makeItem(who ? QStringLiteral("%1 %2").arg(who->pid).arg(QString::fromStdString(who->name))
                                             : QString::fromStdString(e.processId)));
        m_events->setItem(r, 2, makeItem(e.reason == "Burst complete" || e.reason.rfind("Terminated before", 0) == 0
                                             ? QString::fromStdString(e.reason)
                                             : actionLabel(e.from, e.to)));
        m_events->setItem(r, 3, makeStatusItem(stateName(e.from)));
        m_events->setItem(r, 4, makeStatusItem(stateName(e.to)));
    }
    if (events.empty()) {
        m_events->hide();
        showEmptyState(m_eventsState, QStringLiteral("No transitions yet. Every action you apply is recorded here."));
        m_lastAction->setText(QStringLiteral("Last action: none yet."));
    } else {
        m_events->show();
        m_eventsState->hide();
        const os::StateEvent& last = events.back();
        m_lastAction->setText(QStringLiteral("Last action: %1 on %2 (tick %3).\nPrevious state: %4    New state: %5")
                                  .arg(actionLabel(last.from, last.to), QString::fromStdString(last.processId))
                                  .arg(last.time)
                                  .arg(stateName(last.from), stateName(last.to)));
    }
    onProcessSelected();  // diagram, summary, button states and CPU indicator
}

void ProcessStatePage::applyTransition(S from, S to)
{
    const QString id = selectedProcess();
    if (id.isEmpty()) {
        m_banner->showWarning(QStringLiteral("Select a process in the table first."));
        return;
    }
    const os::SimulatedProcess* p = m_sim.find(id.toStdString());
    if (p && p->state != from) {
        // The button's edge does not start at this process's state: explain the rule.
        m_banner->showWarning(QStringLiteral("\"%1\" is not possible. %2")
                                  .arg(actionLabel(from, to),
                                       QString::fromStdString(os::explainInvalidTransition(id.toStdString(), p->state, to))));
        return;
    }
    std::string error;
    if (!m_sim.apply(id.toStdString(), to, &error)) {
        m_banner->showWarning(QStringLiteral("\"%1\" is not possible. %2")
                                  .arg(actionLabel(from, to), QString::fromStdString(error)));
        return;
    }
    m_banner->showSuccess(QStringLiteral("%1: %2 moved from %3 to %4.")
                              .arg(actionLabel(from, to), id, stateName(from), stateName(to)));
    refreshSimulator();
}

void ProcessStatePage::selectProcess(const std::string& id)
{
    const QString key = QString::fromStdString(id);
    for (int r = 0; r < m_processes->rowCount(); ++r)
        if (m_processes->item(r, 0) && m_processes->item(r, 0)->data(Qt::UserRole).toString() == key)
            m_processes->selectRow(r);
}

void ProcessStatePage::onAddProcess()
{
    std::string error;
    const int pid = m_sim.addProcess(m_newName->currentText().trimmed().toStdString(), m_newPriority->value(),
                                     m_newBurst->value(), &error);
    if (pid < 0) {
        m_banner->showError(QString::fromStdString(error));
        return;
    }
    refreshSimulator();
    selectProcess(std::to_string(pid));
    m_banner->showInfo(QStringLiteral("Simulated process PID %1 (%2) created in the NEW state. Next step: \"Admit to "
                                      "ready queue\".")
                           .arg(pid)
                           .arg(m_newName->currentText().trimmed()));
}

void ProcessStatePage::onRunTick()
{
    const auto running = m_sim.runningProcess();
    std::string error;
    if (!running || !m_sim.runTick(&error)) {
        m_banner->showWarning(QString::fromStdString(error.empty() ? std::string("No process is running.") : error));
        return;
    }
    const os::SimulatedProcess* p = m_sim.find(*running);
    if (p && p->state == S::Terminated)
        m_banner->showSuccess(QStringLiteral("PID %1 (%2) used its last time unit and moved to TERMINATED.")
                                  .arg(p->pid)
                                  .arg(QString::fromStdString(p->name)));
    else if (p && p->burst > 0)
        m_banner->showInfo(QStringLiteral("PID %1 (%2) ran for 1 time unit: %3 unit(s) left.")
                               .arg(p->pid)
                               .arg(QString::fromStdString(p->name))
                               .arg(p->remaining));
    else if (p)
        m_banner->showInfo(QStringLiteral("PID %1 (%2) ran for 1 time unit (no burst length set, so it runs until you "
                                          "terminate it).")
                               .arg(p->pid)
                               .arg(QString::fromStdString(p->name)));
    refreshSimulator();
}

void ProcessStatePage::createFromSchedule()
{
    if (!m_hasTrace)
        return;
    if (!m_sim.processes().empty()
        && !confirm(this, QStringLiteral("Create from CPU scheduling run"),
                    QStringLiteral("Replace the current simulated processes with the %1 process(es) from the last "
                                   "CPU scheduling run?")
                        .arg(static_cast<int>(m_trace.processes.size())),
                    QStringLiteral("Replace"))) {
        return;
    }
    m_sim.reset();
    QStringList created;
    for (const os::ProcessResult& p : m_trace.processes) {
        std::string error;
        if (m_sim.createProcess(p.process.id, &error))
            created << QString::fromStdString(p.process.id);
        else
            m_banner->showError(QString::fromStdString(error));
    }
    refreshSimulator();
    if (m_processes->rowCount() > 0)
        m_processes->selectRow(0);
    m_banner->showInfo(QStringLiteral("Created %1 in the NEW state from the last %2 run. Arrival and burst times "
                                      "are not used here: you drive each transition yourself. The \"Trace from CPU "
                                      "scheduling\" tab shows the states the scheduler itself produced.")
                           .arg(created.join(QStringLiteral(", ")),
                                QString::fromStdString(os::algorithmName(m_trace.algorithm))));
}

void ProcessStatePage::showSchedule(const os::ScheduleResult& result)
{
    m_trace = result;
    m_hasTrace = true;
    m_fromSchedule->setEnabled(true);
    m_fromSchedule->setToolTip(QStringLiteral("Create the processes of the last %1 run in the NEW state")
                                   .arg(QString::fromStdString(os::algorithmName(result.algorithm))));
    QString title = QString::fromStdString(os::algorithmName(result.algorithm));
    if (result.algorithm == os::Algorithm::RoundRobin)
        title += QStringLiteral(" (quantum %1)").arg(result.quantum);
    m_traceTitle->setText(QStringLiteral("Last run: %1 on %2 processes, finished at time %3. Drag the slider to "
                                         "see each process's state at that instant. The simulated processes are "
                                         "CPU-bound, so WAITING does not occur here; try it in the lifecycle "
                                         "simulator.")
                              .arg(title)
                              .arg(static_cast<int>(result.processes.size()))
                              .arg(result.makespan));

    m_traceEvents->setRowCount(static_cast<int>(result.events.size()));
    for (int r = 0; r < static_cast<int>(result.events.size()); ++r) {
        const os::StateEvent& e = result.events[static_cast<std::size_t>(r)];
        m_traceEvents->setItem(r, 0, makeNumberItem(e.time));
        m_traceEvents->setItem(r, 1, makeItem(QString::fromStdString(e.processId)));
        m_traceEvents->setItem(r, 2, makeStatusItem(stateName(e.from)));
        m_traceEvents->setItem(r, 3, makeStatusItem(stateName(e.to)));
        m_traceEvents->setItem(r, 4, makeItem(QString::fromStdString(e.reason)));
    }
    m_traceEvents->resizeColumnsToContents();

    m_slider->blockSignals(true);
    m_slider->setRange(0, result.makespan);
    m_slider->setValue(0);
    m_slider->setEnabled(true);
    m_slider->blockSignals(false);
    updateSnapshot(0);
}

void ProcessStatePage::updateSnapshot(int time)
{
    if (!m_hasTrace)
        return;
    m_sliderLabel->setText(QStringLiteral("Time %1").arg(time));
    // State at `time` = destination of the last event at or before `time`.
    std::map<std::string, S> state;
    for (const auto& p : m_trace.processes)
        state[p.process.id] = S::New;
    for (const os::StateEvent& e : m_trace.events) {
        if (e.time > time)
            break;  // events are sorted by time
        state[e.processId] = e.to;
    }
    m_snapshot->setRowCount(static_cast<int>(m_trace.processes.size()));
    for (int r = 0; r < static_cast<int>(m_trace.processes.size()); ++r) {
        const std::string& id = m_trace.processes[static_cast<std::size_t>(r)].process.id;
        m_snapshot->setItem(r, 0, makeItem(QString::fromStdString(id)));
        m_snapshot->setItem(r, 1, makeStatusItem(stateName(state[id])));
    }
}

}  // namespace labdesk::ui
