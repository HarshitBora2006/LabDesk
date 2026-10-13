#include "schedulerpage.h"

#include "ganttchartwidget.h"
#include "services.h"
#include "uihelpers.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

namespace labdesk::ui {

namespace {

// Numeric cells are edited with a bounded spin box, so invalid numbers
// cannot be typed in the first place (the engine validates again anyway).
class SpinBoxDelegate : public QStyledItemDelegate {
public:
    SpinBoxDelegate(int minimum, int maximum, QObject* parent)
        : QStyledItemDelegate(parent), m_min(minimum), m_max(maximum)
    {
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        auto* box = new QSpinBox(parent);
        box->setRange(m_min, m_max);
        box->setFrame(false);
        return box;
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        static_cast<QSpinBox*>(editor)->setValue(index.data(Qt::EditRole).toInt());
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
    {
        auto* box = static_cast<QSpinBox*>(editor);
        box->interpretText();
        model->setData(index, box->value(), Qt::EditRole);
    }

private:
    int m_min;
    int m_max;
};

enum InputColumn { ColId = 0, ColArrival, ColBurst, ColPriority };

}  // namespace

SchedulerPage::SchedulerPage(Services& services, QWidget* parent) : Page(parent), m_services(services)
{
    setObjectName(QStringLiteral("pageRoot"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea();
    scroll->setObjectName(QStringLiteral("pageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget();
    body->setObjectName(QStringLiteral("pageScrollBody"));
    scroll->setWidget(body);
    outer->addWidget(scroll);

    QVBoxLayout* root = makePageLayout(
        body, QStringLiteral("CPU scheduling"),
        QStringLiteral("Simulates FCFS, SJF, Priority and Round Robin on a process workload. This is separate "
                       "from computer allocation: here the resource being shared is CPU time."));
    body->setObjectName(QStringLiteral("pageScrollBody"));
    m_banner = new MessageBanner();
    root->addWidget(m_banner);

    auto* top = new QHBoxLayout();
    top->setSpacing(12);

    // --- workload input
    QFrame* inputCard = makeCard();
    QVBoxLayout* input = cardLayout(inputCard);
    input->addWidget(makeLabel(QStringLiteral("Processes"), QStringLiteral("sectionTitle")));
    m_input = new QTableWidget();
    m_input->setColumnCount(4);
    m_input->setHorizontalHeaderLabels(
        {QStringLiteral("Process ID"), QStringLiteral("Arrival"), QStringLiteral("Burst"), QStringLiteral("Priority")});
    m_input->verticalHeader()->setVisible(false);
    m_input->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_input->setAlternatingRowColors(true);
    m_input->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_input->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    m_input->setItemDelegateForColumn(ColArrival, new SpinBoxDelegate(0, os::kMaxArrival, m_input));
    m_input->setItemDelegateForColumn(ColBurst, new SpinBoxDelegate(1, os::kMaxBurst, m_input));
    m_input->setItemDelegateForColumn(ColPriority, new SpinBoxDelegate(os::kMinPriority, os::kMaxPriority, m_input));
    m_input->setMinimumHeight(210);
    input->addWidget(m_input, 1);

    auto* rowButtons = new QHBoxLayout();
    auto* add = makeButton(QStringLiteral("Add process"));
    auto* remove = makeButton(QStringLiteral("Remove selected"));
    auto* sample = makeButton(QStringLiteral("Load textbook example"));
    auto* clear = makeButton(QStringLiteral("Clear"));
    rowButtons->addWidget(add);
    rowButtons->addWidget(remove);
    rowButtons->addStretch();
    rowButtons->addWidget(sample);
    rowButtons->addWidget(clear);
    input->addLayout(rowButtons);

    auto* storage = new QHBoxLayout();
    storage->addWidget(makeLabel(QStringLiteral("Saved workload"), QStringLiteral("muted")));
    m_workload = new QComboBox();
    m_workload->setMinimumWidth(240);
    storage->addWidget(m_workload, 1);
    auto* load = makeButton(QStringLiteral("Load"));
    auto* save = makeButton(QStringLiteral("Save"));
    storage->addWidget(load);
    storage->addWidget(save);
    input->addLayout(storage);
    top->addWidget(inputCard, 3);

    // --- algorithm controls
    QFrame* controlCard = makeCard();
    controlCard->setMaximumWidth(380);
    QVBoxLayout* controls = cardLayout(controlCard);
    controls->addWidget(makeLabel(QStringLiteral("Algorithm"), QStringLiteral("sectionTitle")));
    m_algorithm = new QComboBox();
    m_algorithm->addItem(QStringLiteral("First Come First Serve (FCFS)"), static_cast<int>(os::Algorithm::FCFS));
    m_algorithm->addItem(QStringLiteral("Shortest Job First (non-preemptive)"), static_cast<int>(os::Algorithm::SJF));
    m_algorithm->addItem(QStringLiteral("Priority (non-preemptive)"), static_cast<int>(os::Algorithm::Priority));
    m_algorithm->addItem(QStringLiteral("Round Robin (preemptive)"), static_cast<int>(os::Algorithm::RoundRobin));
    controls->addWidget(m_algorithm);
    auto* quantumRow = new QHBoxLayout();
    quantumRow->addWidget(makeLabel(QStringLiteral("Time quantum")));
    m_quantum = new QSpinBox();
    m_quantum->setRange(1, os::kMaxQuantum);
    m_quantum->setValue(2);
    quantumRow->addWidget(m_quantum);
    quantumRow->addStretch();
    controls->addLayout(quantumRow);
    auto* run = makeButton(QStringLiteral("Run simulation"), QStringLiteral("primary"));
    auto* compare = makeButton(QStringLiteral("Compare all four"));
    controls->addWidget(run);
    controls->addWidget(compare);
    controls->addWidget(makeLabel(
        QStringLiteral("Priority: 0 to 99, a lower number means a higher priority.\n"
                       "Ties go to the earlier arrival, then to the earlier row.\n"
                       "Round Robin: a process arriving exactly when a quantum ends joins the queue before "
                       "the preempted process.\n"
                       "TAT = CT - AT,  WT = TAT - BT,  RT = first run - AT."),
        QStringLiteral("muted")));
    controls->addStretch();
    top->addWidget(controlCard, 2);
    root->addLayout(top);

    // --- results
    QFrame* resultCard = makeCard();
    QVBoxLayout* result = cardLayout(resultCard);
    result->addWidget(makeLabel(QStringLiteral("Result"), QStringLiteral("sectionTitle")));
    m_summary = makeLabel(QStringLiteral("No simulation run yet."), QStringLiteral("muted"));
    result->addWidget(m_summary);
    auto* ganttScroll = new QScrollArea();
    ganttScroll->setWidgetResizable(true);
    ganttScroll->setFrameShape(QFrame::NoFrame);
    ganttScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ganttScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gantt = new GanttChartWidget();
    ganttScroll->setWidget(m_gantt);
    ganttScroll->setFixedHeight(m_gantt->sizeHint().height() + 18);
    result->addWidget(ganttScroll);
    m_results = new QTableWidget();
    setupTable(m_results, {QStringLiteral("Process"), QStringLiteral("Arrival"), QStringLiteral("Burst"),
                           QStringLiteral("Priority"), QStringLiteral("First run"), QStringLiteral("Completion"),
                           QStringLiteral("Turnaround"), QStringLiteral("Waiting"), QStringLiteral("Response")});
    m_results->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_results->setMinimumHeight(200);
    result->addWidget(m_results);
    m_comparisonTitle = makeLabel(QStringLiteral("Comparison on the same workload"), QStringLiteral("sectionTitle"));
    result->addWidget(m_comparisonTitle);
    m_comparison = new QTableWidget();
    setupTable(m_comparison, {QStringLiteral("Algorithm"), QStringLiteral("Avg waiting"),
                              QStringLiteral("Avg turnaround"), QStringLiteral("Avg response"),
                              QStringLiteral("Context switches"), QStringLiteral("CPU utilisation")});
    m_comparison->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_comparison->setFixedHeight(4 * 32 + 40);
    result->addWidget(m_comparison);
    m_comparisonTitle->hide();
    m_comparison->hide();
    root->addWidget(resultCard);

    connect(add, &QPushButton::clicked, this, &SchedulerPage::onAddProcess);
    connect(remove, &QPushButton::clicked, this, &SchedulerPage::onRemoveProcess);
    connect(sample, &QPushButton::clicked, this, &SchedulerPage::loadTextbookSample);
    connect(clear, &QPushButton::clicked, this, [this]() { m_input->setRowCount(0); });
    connect(load, &QPushButton::clicked, this, &SchedulerPage::onLoadWorkload);
    connect(save, &QPushButton::clicked, this, &SchedulerPage::onSaveWorkload);
    connect(run, &QPushButton::clicked, this, &SchedulerPage::onRun);
    connect(compare, &QPushButton::clicked, this, &SchedulerPage::onCompareAll);
    connect(m_algorithm, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_quantum->setEnabled(m_algorithm->currentData().toInt() == static_cast<int>(os::Algorithm::RoundRobin));
    });
    m_quantum->setEnabled(false);
    loadTextbookSample();
}

void SchedulerPage::refresh()
{
    const QVariant previous = m_workload->currentData();
    m_workload->clear();
    m_workload->addItem(QStringLiteral("Demo workload (not linked to a session)"), QVariant());
    for (const SessionRecord& s : m_services.sessions.listSessions(false, 50)) {
        m_workload->addItem(QStringLiteral("Session #%1, %2 on %3 (%4)")
                                .arg(s.sessionId)
                                .arg(s.studentId, s.computerId, s.status.toLower()),
                            s.sessionId);
    }
    const int index = previous.isValid() ? m_workload->findData(previous) : 0;
    m_workload->setCurrentIndex(index >= 0 ? index : 0);
}

std::optional<qint64> SchedulerPage::selectedWorkloadKey() const
{
    const QVariant v = m_workload->currentData();
    if (!v.isValid() || v.isNull())
        return std::nullopt;
    return v.toLongLong();
}

void SchedulerPage::addProcessRow(const QString& id, int arrival, int burst, int priority)
{
    const int row = m_input->rowCount();
    m_input->insertRow(row);
    m_input->setItem(row, ColId, new QTableWidgetItem(id));
    const int values[] = {arrival, burst, priority};
    for (int c = 0; c < 3; ++c) {
        auto* item = new QTableWidgetItem();
        item->setData(Qt::EditRole, values[c]);
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_input->setItem(row, ColArrival + c, item);
    }
}

void SchedulerPage::onAddProcess()
{
    // Pick the first unused "P<n>" label.
    int n = m_input->rowCount() + 1;
    auto taken = [this](const QString& id) {
        for (int r = 0; r < m_input->rowCount(); ++r)
            if (m_input->item(r, ColId) && m_input->item(r, ColId)->text().trimmed() == id)
                return true;
        return false;
    };
    while (taken(QStringLiteral("P%1").arg(n)))
        ++n;
    addProcessRow(QStringLiteral("P%1").arg(n), 0, 1, 0);
    m_input->selectRow(m_input->rowCount() - 1);
}

void SchedulerPage::onRemoveProcess()
{
    const auto items = m_input->selectedItems();
    if (!items.isEmpty())
        m_input->removeRow(items.first()->row());
}

void SchedulerPage::loadTextbookSample()
{
    m_input->setRowCount(0);
    addProcessRow(QStringLiteral("P1"), 0, 5, 2);
    addProcessRow(QStringLiteral("P2"), 1, 3, 3);
    addProcessRow(QStringLiteral("P3"), 2, 8, 1);
    addProcessRow(QStringLiteral("P4"), 3, 6, 4);
}

bool SchedulerPage::readProcesses(std::vector<os::Process>& out)
{
    out.clear();
    for (int r = 0; r < m_input->rowCount(); ++r) {
        os::Process p;
        const QTableWidgetItem* id = m_input->item(r, ColId);
        p.id = id ? id->text().trimmed().toStdString() : std::string();
        bool ok[3] = {false, false, false};
        int* fields[] = {&p.arrival, &p.burst, &p.priority};
        for (int c = 0; c < 3; ++c) {
            const QTableWidgetItem* item = m_input->item(r, ColArrival + c);
            *fields[c] = item ? item->data(Qt::EditRole).toInt(&ok[c]) : 0;
        }
        if (!ok[0] || !ok[1] || !ok[2]) {
            m_banner->showError(QStringLiteral("Row %1 has a missing or non-numeric value.").arg(r + 1));
            return false;
        }
        out.push_back(p);
    }
    const auto problems = os::validateProcesses(out);
    if (!problems.empty()) {
        QStringList lines;
        for (const auto& line : problems)
            lines << QString::fromStdString(line);
        m_banner->showError(lines.join(QLatin1Char('\n')));
        return false;
    }
    return true;
}

void SchedulerPage::onRun()
{
    std::vector<os::Process> processes;
    if (!readProcesses(processes))
        return;
    const auto algorithm = static_cast<os::Algorithm>(m_algorithm->currentData().toInt());
    try {
        const os::ScheduleResult result = os::Scheduler::run(algorithm, processes, m_quantum->value());
        m_banner->clearMessage();
        showResult(result);
        emit scheduleComputed(result);
    } catch (const std::exception& e) {
        m_banner->showError(QString::fromStdString(e.what()));
    }
}

void SchedulerPage::onCompareAll()
{
    std::vector<os::Process> processes;
    if (!readProcesses(processes))
        return;
    const os::Algorithm all[] = {os::Algorithm::FCFS, os::Algorithm::SJF, os::Algorithm::Priority,
                                 os::Algorithm::RoundRobin};
    m_comparison->setRowCount(4);
    try {
        for (int i = 0; i < 4; ++i) {
            const os::ScheduleResult r = os::Scheduler::run(all[i], processes, m_quantum->value());
            QString name = QString::fromStdString(os::algorithmName(all[i]));
            if (all[i] == os::Algorithm::RoundRobin)
                name += QStringLiteral(" (q = %1)").arg(m_quantum->value());
            m_comparison->setItem(i, 0, makeItem(name));
            m_comparison->setItem(i, 1, makeNumberItem(r.averageWaiting, 2));
            m_comparison->setItem(i, 2, makeNumberItem(r.averageTurnaround, 2));
            m_comparison->setItem(i, 3, makeNumberItem(r.averageResponse, 2));
            m_comparison->setItem(i, 4, makeNumberItem(r.contextSwitches));
            m_comparison->setItem(i, 5, makeItem(QStringLiteral("%1 %").arg(r.cpuUtilisation * 100.0, 0, 'f', 1)));
        }
    } catch (const std::exception& e) {
        m_banner->showError(QString::fromStdString(e.what()));
        return;
    }
    m_comparisonTitle->show();
    m_comparison->show();
    m_banner->showInfo(QStringLiteral("Compared all four algorithms on the same %1 processes.")
                           .arg(static_cast<int>(processes.size())));
}

void SchedulerPage::showResult(const os::ScheduleResult& r)
{
    QString title = QString::fromStdString(os::algorithmName(r.algorithm));
    if (r.algorithm == os::Algorithm::RoundRobin)
        title += QStringLiteral(", quantum %1").arg(r.quantum);
    m_summary->setText(QStringLiteral("%1.  Average waiting %2,  average turnaround %3,  average response %4.  "
                                      "Finished at t = %5, CPU idle for %6, utilisation %7 %, %8 context switch(es).")
                           .arg(title)
                           .arg(r.averageWaiting, 0, 'f', 2)
                           .arg(r.averageTurnaround, 0, 'f', 2)
                           .arg(r.averageResponse, 0, 'f', 2)
                           .arg(r.makespan)
                           .arg(r.idleTime)
                           .arg(r.cpuUtilisation * 100.0, 0, 'f', 1)
                           .arg(r.contextSwitches));
    m_gantt->setSchedule(r);

    m_results->setRowCount(static_cast<int>(r.processes.size()));
    for (int row = 0; row < static_cast<int>(r.processes.size()); ++row) {
        const os::ProcessResult& p = r.processes[static_cast<std::size_t>(row)];
        m_results->setItem(row, 0, makeItem(QString::fromStdString(p.process.id)));
        m_results->setItem(row, 1, makeNumberItem(p.process.arrival));
        m_results->setItem(row, 2, makeNumberItem(p.process.burst));
        m_results->setItem(row, 3, makeNumberItem(p.process.priority));
        m_results->setItem(row, 4, makeNumberItem(p.firstStart));
        m_results->setItem(row, 5, makeNumberItem(p.completion));
        m_results->setItem(row, 6, makeNumberItem(p.turnaround));
        m_results->setItem(row, 7, makeNumberItem(p.waiting));
        m_results->setItem(row, 8, makeNumberItem(p.response));
    }
}

void SchedulerPage::onSaveWorkload()
{
    std::vector<os::Process> processes;
    if (!readProcesses(processes))
        return;
    std::vector<StoredProcess> rows;
    for (const os::Process& p : processes) {
        StoredProcess s;
        s.label = QString::fromStdString(p.id);
        s.arrival = p.arrival;
        s.burst = p.burst;
        s.priority = p.priority;
        rows.push_back(s);
    }
    const OpResult r = m_services.processes.saveWorkload(selectedWorkloadKey(), rows);
    if (r.ok)
        m_banner->showSuccess(r.message + QStringLiteral(" Linked to: ") + m_workload->currentText() + QLatin1Char('.'));
    else
        m_banner->showError(r.message);
}

void SchedulerPage::onLoadWorkload()
{
    QString error;
    const auto rows = m_services.processes.loadWorkload(selectedWorkloadKey(), &error);
    if (!error.isEmpty()) {
        m_banner->showError(QStringLiteral("Could not load the workload: ") + error);
        return;
    }
    if (rows.empty()) {
        m_banner->showInfo(QStringLiteral("Nothing is saved for this workload yet. Enter processes and press Save."));
        return;
    }
    m_input->setRowCount(0);
    for (const StoredProcess& p : rows)
        addProcessRow(p.label, p.arrival, p.burst, p.priority);
    m_banner->showSuccess(QStringLiteral("Loaded %1 process(es).").arg(static_cast<int>(rows.size())));
}

}  // namespace labdesk::ui
