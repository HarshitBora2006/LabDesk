#include "processstatepage.h"

#include "statediagramwidget.h"
#include "uihelpers.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <map>

namespace labdesk::ui {

using S = os::ProcessState;

ProcessStatePage::ProcessStatePage(QWidget* parent) : Page(parent)
{
    QVBoxLayout* root = makePageLayout(
        this, QStringLiteral("Process states"),
        QStringLiteral("An educational simulation of the process lifecycle. It does not control real operating-"
                       "system processes and is unrelated to computer session status."));
    auto* tabs = new QTabWidget();
    tabs->addTab(buildSimulatorTab(), QStringLiteral("Lifecycle simulator"));
    tabs->addTab(buildTraceTab(), QStringLiteral("Trace from the scheduler"));
    root->addWidget(tabs, 1);
    refreshSimulator();
}

QWidget* ProcessStatePage::buildSimulatorTab()
{
    auto* tab = new QWidget();
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);
    m_banner = new MessageBanner();
    layout->addWidget(m_banner);

    auto* body = new QHBoxLayout();
    body->setSpacing(14);

    auto* left = new QVBoxLayout();
    auto* topButtons = new QHBoxLayout();
    auto* create = makeButton(QStringLiteral("Create process"), QStringLiteral("primary"));
    auto* reset = makeButton(QStringLiteral("Reset"));
    topButtons->addWidget(create);
    topButtons->addStretch();
    topButtons->addWidget(reset);
    left->addLayout(topButtons);
    m_processes = new QTableWidget();
    setupTable(m_processes, {QStringLiteral("Process"), QStringLiteral("State"), QStringLiteral("Transitions")});
    m_processes->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    left->addWidget(m_processes, 1);

    // One button per edge of the state diagram, enabled only when the selected
    // process is in that edge's source state.
    struct Action {
        S from;
        S to;
        const char* text;
    };
    const Action actions[] = {{S::New, S::Ready, "Admit"},
                              {S::Ready, S::Running, "Dispatch"},
                              {S::Running, S::Ready, "Preempt"},
                              {S::Running, S::Waiting, "Block for I/O"},
                              {S::Waiting, S::Ready, "I/O complete"},
                              {S::Running, S::Terminated, "Exit"}};
    auto* actionBox = new QGridLayout();
    actionBox->setSpacing(6);
    int index = 0;
    for (const Action& a : actions) {
        auto* button = makeButton(QString::fromLatin1(a.text));
        button->setToolTip(QStringLiteral("%1 to %2").arg(QString::fromLatin1(os::toString(a.from)),
                                                         QString::fromLatin1(os::toString(a.to))));
        actionBox->addWidget(button, index / 2, index % 2);
        ++index;
        m_actions.push_back({a.from, a.to, button});
        const S to = a.to;
        connect(button, &QPushButton::clicked, this, [this, to]() { applyTransition(to); });
    }
    left->addLayout(actionBox);
    left->addWidget(makeLabel(QStringLiteral("Select a process, then apply a transition. Only one process may "
                                             "be RUNNING at a time (single CPU); illegal moves are refused."),
                              QStringLiteral("muted")));
    body->addLayout(left, 2);

    auto* right = new QVBoxLayout();
    m_diagram = new StateDiagramWidget();
    right->addWidget(m_diagram, 3);
    right->addWidget(makeLabel(QStringLiteral("Transition log"), QStringLiteral("sectionTitle")));
    m_events = new QTableWidget();
    setupTable(m_events, {QStringLiteral("Tick"), QStringLiteral("Process"), QStringLiteral("From"),
                          QStringLiteral("To"), QStringLiteral("Transition")});
    right->addWidget(m_events, 2);
    body->addLayout(right, 3);
    layout->addLayout(body, 1);

    connect(create, &QPushButton::clicked, this, [this]() {
        const std::string id = m_sim.createProcess();
        refreshSimulator();
        for (int r = 0; r < m_processes->rowCount(); ++r)
            if (m_processes->item(r, 0)->text() == QString::fromStdString(id))
                m_processes->selectRow(r);
    });
    connect(reset, &QPushButton::clicked, this, [this]() {
        m_sim.reset();
        m_banner->clearMessage();
        refreshSimulator();
    });
    connect(m_processes, &QTableWidget::itemSelectionChanged, this, &ProcessStatePage::onProcessSelected);
    return tab;
}

QWidget* ProcessStatePage::buildTraceTab()
{
    auto* tab = new QWidget();
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);
    m_traceTitle = makeLabel(QStringLiteral("Run an algorithm on the CPU scheduling page to see its state trace here."),
                             QStringLiteral("muted"));
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
    m_snapshot = new QTableWidget();
    setupTable(m_snapshot, {QStringLiteral("Process"), QStringLiteral("State at this time")});
    m_snapshot->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tables->addWidget(m_snapshot, 2);
    m_traceEvents = new QTableWidget();
    setupTable(m_traceEvents, {QStringLiteral("Time"), QStringLiteral("Process"), QStringLiteral("From"),
                               QStringLiteral("To"), QStringLiteral("Reason")});
    tables->addWidget(m_traceEvents, 3);
    layout->addLayout(tables, 1);

    connect(m_slider, &QSlider::valueChanged, this, &ProcessStatePage::updateSnapshot);
    return tab;
}

void ProcessStatePage::onProcessSelected()
{
    const QString id = selectedProcess();
    const os::SimulatedProcess* p = id.isEmpty() ? nullptr : m_sim.find(id.toStdString());
    m_diagram->setCurrentState(p ? std::optional<S>(p->state) : std::nullopt);
    for (const ActionButton& a : m_actions)
        a.button->setEnabled(p && p->state == a.from);
}

QString ProcessStatePage::selectedProcess() const
{
    const auto items = m_processes->selectedItems();
    if (items.isEmpty())
        return QString();
    const QTableWidgetItem* item = m_processes->item(items.first()->row(), 0);
    return item ? item->text() : QString();
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
        m_processes->setItem(r, 0, makeItem(id));
        m_processes->setItem(r, 1, makeStatusItem(QString::fromLatin1(os::toString(p.state))));
        m_processes->setItem(r, 2, makeNumberItem(p.transitions));
        if (id == keep)
            reselect = r;
    }
    if (reselect >= 0)
        m_processes->selectRow(reselect);
    m_processes->blockSignals(false);
    onProcessSelected();  // refresh diagram and button states

    const auto& events = m_sim.events();
    m_events->setRowCount(static_cast<int>(events.size()));
    for (int r = 0; r < static_cast<int>(events.size()); ++r) {
        const os::StateEvent& e = events[events.size() - 1 - static_cast<std::size_t>(r)];  // newest first
        m_events->setItem(r, 0, makeNumberItem(e.time));
        m_events->setItem(r, 1, makeItem(QString::fromStdString(e.processId)));
        m_events->setItem(r, 2, makeStatusItem(QString::fromLatin1(os::toString(e.from))));
        m_events->setItem(r, 3, makeStatusItem(QString::fromLatin1(os::toString(e.to))));
        m_events->setItem(r, 4, makeItem(QString::fromStdString(e.reason)));
    }
}

void ProcessStatePage::applyTransition(S to)
{
    const QString id = selectedProcess();
    if (id.isEmpty()) {
        m_banner->showError(QStringLiteral("Select a process first."));
        return;
    }
    std::string error;
    if (!m_sim.apply(id.toStdString(), to, &error)) {
        m_banner->showError(QString::fromStdString(error));
        return;
    }
    m_banner->clearMessage();
    refreshSimulator();
}

void ProcessStatePage::showSchedule(const os::ScheduleResult& result)
{
    m_trace = result;
    m_hasTrace = true;
    QString title = QString::fromStdString(os::algorithmName(result.algorithm));
    if (result.algorithm == os::Algorithm::RoundRobin)
        title += QStringLiteral(" (quantum %1)").arg(result.quantum);
    m_traceTitle->setText(QStringLiteral("Last run: %1 on %2 processes, finished at t = %3. Drag the slider to "
                                         "see each process's state at that instant. CPU-bound simulation, so "
                                         "WAITING does not occur here; try it in the lifecycle simulator.")
                              .arg(title)
                              .arg(static_cast<int>(result.processes.size()))
                              .arg(result.makespan));

    m_traceEvents->setRowCount(static_cast<int>(result.events.size()));
    for (int r = 0; r < static_cast<int>(result.events.size()); ++r) {
        const os::StateEvent& e = result.events[static_cast<std::size_t>(r)];
        m_traceEvents->setItem(r, 0, makeNumberItem(e.time));
        m_traceEvents->setItem(r, 1, makeItem(QString::fromStdString(e.processId)));
        m_traceEvents->setItem(r, 2, makeStatusItem(QString::fromLatin1(os::toString(e.from))));
        m_traceEvents->setItem(r, 3, makeStatusItem(QString::fromLatin1(os::toString(e.to))));
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
        m_snapshot->setItem(r, 1, makeStatusItem(QString::fromLatin1(os::toString(state[id]))));
    }
}

}  // namespace labdesk::ui
