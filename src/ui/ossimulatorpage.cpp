#include "ossimulatorpage.h"

#include "ganttchartwidget.h"
#include "layouthelpers.h"
#include "theme.h"
#include "uihelpers.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace labdesk::ui {

namespace {

enum Column { ColName = 0, ColArrival, ColBurst, ColPriority, ColMemory, ColCount };

// Numeric cells: bounded spin box.
class ProgramDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (index.column() == ColName)
            return QStyledItemDelegate::createEditor(parent, option, index);
        auto* box = new QSpinBox(parent);
        box->setFrame(false);
        switch (index.column()) {
        case ColArrival: box->setRange(0, 100); break;
        case ColBurst: box->setRange(1, 50); break;
        case ColPriority: box->setRange(0, 99); break;
        default: box->setRange(1, 16); break;  // memory blocks
        }
        return box;
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        if (auto* box = qobject_cast<QSpinBox*>(editor))
            box->setValue(index.data(Qt::EditRole).toInt());
        else
            QStyledItemDelegate::setEditorData(editor, index);
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
    {
        if (auto* box = qobject_cast<QSpinBox*>(editor)) {
            box->interpretText();
            model->setData(index, box->value(), Qt::EditRole);
        } else {
            QStyledItemDelegate::setModelData(editor, model, index);
        }
    }
};

const os::Algorithm kAlgorithms[] = {os::Algorithm::FCFS, os::Algorithm::SJF, os::Algorithm::Priority,
                                     os::Algorithm::RoundRobin};

QFrame* panel(const QString& title, QVBoxLayout** contentOut)
{
    QFrame* card = makeCard();
    cardLayout(card)->setSpacing(6);
    cardLayout(card)->addWidget(makeLabel(title, QStringLiteral("statLabel")));
    *contentOut = cardLayout(card);
    return card;
}

}  // namespace

OsSimulatorPage::OsSimulatorPage(QWidget* parent) : Page(parent)
{
    QWidget* body = makeScrollPage(this);
    QVBoxLayout* root = makePageLayout(body, QStringLiteral("OS Simulator"),
                                       QStringLiteral("Simulate processes on one computer"));
    m_banner = new MessageBanner();
    root->addWidget(m_banner);
    root->addWidget(buildSetupCard());
    root->addWidget(buildRunCard());
    root->addWidget(buildMachineCards());
    root->addWidget(buildDetailsCard());
    root->addStretch(1);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &OsSimulatorPage::onStep);
    fillPrograms(os::OsSimulator::example());
    showState();
}

QWidget* OsSimulatorPage::buildSetupCard()
{
    QFrame* card = makeCard();
    QVBoxLayout* layout = cardLayout(card);
    layout->addWidget(makeLabel(QStringLiteral("1. Programs and settings"), QStringLiteral("sectionTitle")));
    m_programs = new QTableWidget();
    setupTable(m_programs, {QStringLiteral("Program"), QStringLiteral("Arrival"), QStringLiteral("CPU time"),
                            QStringLiteral("Priority"), QStringLiteral("Memory blocks")});
    m_programs->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                                | QAbstractItemView::AnyKeyPressed);
    m_programs->setItemDelegate(new ProgramDelegate(m_programs));
    m_programs->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    alignNumericColumns(m_programs, {1, 2, 3, 4});
    m_programs->setMinimumHeight(200);
    layout->addWidget(m_programs);

    m_setupControls = new QWidget();
    auto* controls = new QHBoxLayout(m_setupControls);
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(8);
    auto* add = makeButton(QStringLiteral("Add program"));
    auto* remove = makeButton(QStringLiteral("Remove selected"));
    auto* example = makeButton(QStringLiteral("Load example"));
    controls->addWidget(add);
    controls->addWidget(remove);
    controls->addWidget(example);
    controls->addStretch(1);
    controls->addWidget(makeLabel(QStringLiteral("Scheduler"), QStringLiteral("fieldLabel")));
    m_algorithm = new QComboBox();
    for (os::Algorithm a : kAlgorithms)
        m_algorithm->addItem(QString::fromStdString(os::algorithmName(a)), static_cast<int>(a));
    controls->addWidget(m_algorithm);
    controls->addWidget(makeLabel(QStringLiteral("Quantum"), QStringLiteral("fieldLabel")));
    m_quantum = new QSpinBox();
    m_quantum->setRange(1, 20);
    m_quantum->setValue(2);
    m_quantum->setToolTip(QStringLiteral("Used by Round Robin only"));
    controls->addWidget(m_quantum);
    controls->addWidget(makeLabel(QStringLiteral("Memory"), QStringLiteral("fieldLabel")));
    m_memoryBlocks = new QSpinBox();
    m_memoryBlocks->setRange(4, 16);
    m_memoryBlocks->setValue(8);
    m_memoryBlocks->setSuffix(QStringLiteral(" blocks"));
    controls->addWidget(m_memoryBlocks);
    layout->addWidget(m_setupControls);

    connect(add, &QPushButton::clicked, this, [this]() {
        os::SimProgram p;
        p.name = "program" + std::to_string(m_programs->rowCount() + 1) + ".exe";
        p.burst = 3;
        addProgramRow(p);
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const auto items = m_programs->selectedItems();
        if (!items.isEmpty())
            m_programs->removeRow(items.first()->row());
    });
    connect(example, &QPushButton::clicked, this, [this]() { fillPrograms(os::OsSimulator::example()); });
    connect(m_algorithm, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_quantum->setEnabled(m_algorithm->currentData().toInt() == static_cast<int>(os::Algorithm::RoundRobin));
    });
    m_quantum->setEnabled(false);
    return card;
}

QWidget* OsSimulatorPage::buildRunCard()
{
    QFrame* card = makeCard();
    auto* row = new QHBoxLayout();
    row->setSpacing(8);
    m_startPause = makeButton(QStringLiteral("Start"), QStringLiteral("primary"));
    auto* step = makeButton(QStringLiteral("Step"));
    step->setToolTip(QStringLiteral("Advance the clock by one tick"));
    auto* toEnd = makeButton(QStringLiteral("Run to end"));
    auto* reset = makeButton(QStringLiteral("Reset"));
    m_speed = new QComboBox();
    m_speed->addItem(QStringLiteral("Slow"), 1200);
    m_speed->addItem(QStringLiteral("Normal"), 600);
    m_speed->addItem(QStringLiteral("Fast"), 200);
    m_speed->setCurrentIndex(1);
    m_clock = makeLabel(QStringLiteral("Time 0"), QStringLiteral("statValue"));
    row->addWidget(m_startPause);
    row->addWidget(step);
    row->addWidget(toEnd);
    row->addWidget(reset);
    row->addWidget(makeLabel(QStringLiteral("Speed"), QStringLiteral("fieldLabel")));
    row->addWidget(m_speed);
    row->addStretch(1);
    row->addWidget(m_clock);
    cardLayout(card)->addWidget(makeLabel(QStringLiteral("2. Run"), QStringLiteral("sectionTitle")));
    cardLayout(card)->addLayout(row);
    connect(m_startPause, &QPushButton::clicked, this, &OsSimulatorPage::onStartPause);
    connect(step, &QPushButton::clicked, this, &OsSimulatorPage::onStep);
    connect(toEnd, &QPushButton::clicked, this, &OsSimulatorPage::onRunToEnd);
    connect(reset, &QPushButton::clicked, this, &OsSimulatorPage::onReset);
    connect(m_speed, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]() { m_timer->setInterval(m_speed->currentData().toInt()); });
    return card;
}

QWidget* OsSimulatorPage::buildMachineCards()
{
    auto* grid = new ResponsiveGrid(300, 14);
    QVBoxLayout* content = nullptr;

    QFrame* cpu = panel(QStringLiteral("CPU (scheduling)"), &content);
    m_cpu = makeLabel(QString(), QStringLiteral("statValue"));
    m_cpuDetail = makeLabel(QString(), QStringLiteral("caption"));
    content->addWidget(m_cpu);
    content->addWidget(m_cpuDetail);
    content->addStretch(1);

    QFrame* memory = panel(QStringLiteral("Memory (first fit)"), &content);
    m_memoryGrid = new QWidget();
    auto* blocks = new QGridLayout(m_memoryGrid);
    blocks->setContentsMargins(0, 0, 0, 0);
    blocks->setSpacing(4);
    m_memoryDetail = makeLabel(QString(), QStringLiteral("caption"));
    content->addWidget(m_memoryGrid);
    content->addWidget(m_memoryDetail);
    content->addStretch(1);

    QFrame* ready = panel(QStringLiteral("Ready queue (waiting for the CPU)"), &content);
    m_readyQueue = makeLabel(QString());
    m_readyQueue->setTextFormat(Qt::RichText);
    content->addWidget(m_readyQueue);
    content->addStretch(1);


    grid->setWidgets({cpu, memory, ready});
    return grid;
}

QWidget* OsSimulatorPage::buildDetailsCard()
{
    QFrame* card = makeCard();
    QVBoxLayout* layout = cardLayout(card);
    layout->setSpacing(10);
    layout->addWidget(makeLabel(QStringLiteral("3. Process table (PCB)"), QStringLiteral("sectionTitle")));
    m_pcb = new QTableWidget();
    setupTable(m_pcb, {QStringLiteral("PID"), QStringLiteral("Program"), QStringLiteral("State"),
                       QStringLiteral("Time left"), QStringLiteral("Memory"), QStringLiteral("Ready wait"),
                       QStringLiteral("Memory wait"),
                       QStringLiteral("Finished at")});
    m_pcb->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    alignNumericColumns(m_pcb, {0, 3, 5, 6, 7});
    m_pcb->setMinimumHeight(200);
    layout->addWidget(m_pcb);

    layout->addWidget(makeLabel(QStringLiteral("Gantt chart (who used the CPU at each tick)"), QStringLiteral("fieldLabel")));
    auto* ganttScroll = new QScrollArea();
    ganttScroll->setWidgetResizable(true);
    ganttScroll->setFrameShape(QFrame::NoFrame);
    ganttScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gantt = new GanttChartWidget();
    ganttScroll->setWidget(m_gantt);
    ganttScroll->setFixedHeight(m_gantt->sizeHint().height() + 18);
    layout->addWidget(ganttScroll);

    m_results = makeLabel(QString(), QStringLiteral("explainer"));
    m_results->hide();
    layout->addWidget(m_results);

    layout->addWidget(makeLabel(QStringLiteral("What the OS did (newest first)"), QStringLiteral("fieldLabel")));
    m_log = new QListWidget();
    m_log->setObjectName(QStringLiteral("log"));
    m_log->setMinimumHeight(200);
    layout->addWidget(m_log);
    return card;
}

void OsSimulatorPage::addProgramRow(const os::SimProgram& p)
{
    const int row = m_programs->rowCount();
    m_programs->insertRow(row);
    m_programs->setItem(row, ColName, new QTableWidgetItem(QString::fromStdString(p.name)));
    const int values[] = {p.arrival, p.burst, p.priority, p.memory};
    for (int c = 0; c < 4; ++c) {
        auto* item = new QTableWidgetItem();
        item->setData(Qt::EditRole, values[c]);
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_programs->setItem(row, ColArrival + c, item);
    }
}

void OsSimulatorPage::fillPrograms(const std::vector<os::SimProgram>& programs)
{
    m_programs->setRowCount(0);
    for (const os::SimProgram& p : programs)
        addProgramRow(p);
}

void OsSimulatorPage::setLocked(bool locked)
{
    m_programs->setEnabled(!locked);
    m_setupControls->setEnabled(!locked);
}

bool OsSimulatorPage::startIfNeeded()
{
    if (m_started)
        return true;
    std::vector<os::SimProgram> programs;
    for (int r = 0; r < m_programs->rowCount(); ++r) {
        os::SimProgram p;
        const QTableWidgetItem* name = m_programs->item(r, ColName);
        p.name = name ? name->text().trimmed().toStdString() : std::string();
        int* fields[] = {&p.arrival, &p.burst, &p.priority, &p.memory};  // no disk or printer use
        for (int c = 0; c < 4; ++c) {
            const QTableWidgetItem* item = m_programs->item(r, ColArrival + c);
            *fields[c] = item ? item->data(Qt::EditRole).toInt() : 0;
        }
        programs.push_back(p);
    }
    os::SimConfig cfg;
    cfg.algorithm = static_cast<os::Algorithm>(m_algorithm->currentData().toInt());
    cfg.quantum = m_quantum->value();
    cfg.memoryBlocks = m_memoryBlocks->value();
    const auto problems = m_sim.load(programs, cfg);
    if (!problems.empty()) {
        QStringList lines;
        for (const auto& line : problems)
            lines << QString::fromStdString(line);
        m_banner->showError(lines.join(QLatin1Char('\n')));
        return false;
    }
    // memory blocks for this run
    auto* grid = static_cast<QGridLayout*>(m_memoryGrid->layout());
    while (QLayoutItem* item = grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (int b = 0; b < cfg.memoryBlocks; ++b) {
        auto* block = new QLabel();
        block->setAlignment(Qt::AlignCenter);
        block->setMinimumHeight(30);
        grid->addWidget(block, b / 4, b % 4);
    }
    m_started = true;
    m_banner->clearMessage();
    setLocked(true);
    return true;
}

QString OsSimulatorPage::nameOf(int index) const
{
    return QString::fromStdString(m_sim.processes()[static_cast<std::size_t>(index)].program.name);
}

void OsSimulatorPage::onStep()
{
    if (!startIfNeeded()) {
        m_timer->stop();
        m_startPause->setText(QStringLiteral("Start"));
        return;
    }
    if (!m_sim.step()) {
        m_timer->stop();
        m_startPause->setText(QStringLiteral("Start"));
    }
    showState();
    if (m_sim.finished()) {
        m_timer->stop();
        m_startPause->setText(QStringLiteral("Start"));
    }
}

void OsSimulatorPage::onStartPause()
{
    if (m_timer->isActive()) {
        m_timer->stop();
        m_startPause->setText(QStringLiteral("Start"));
        return;
    }
    if (!startIfNeeded())
        return;
    if (m_sim.finished()) {
        m_banner->showInfo(QStringLiteral("All programs have finished. Press Reset to run again."));
        return;
    }
    m_timer->setInterval(m_speed->currentData().toInt());
    m_timer->start();
    m_startPause->setText(QStringLiteral("Pause"));
}

void OsSimulatorPage::onRunToEnd()
{
    if (!startIfNeeded())
        return;
    m_timer->stop();
    m_startPause->setText(QStringLiteral("Start"));
    m_sim.runToEnd();
    showState();
}

void OsSimulatorPage::onReset()
{
    m_timer->stop();
    m_startPause->setText(QStringLiteral("Start"));
    m_sim = os::OsSimulator();
    m_started = false;
    setLocked(false);
    m_banner->clearMessage();
    showState();
}

void OsSimulatorPage::showState()
{
    const auto& procs = m_sim.processes();
    m_clock->setText(QStringLiteral("Time %1").arg(m_sim.time()));
    if (!m_started) {
        m_cpu->setText(QStringLiteral("Not started"));
        m_cpuDetail->setText(QStringLiteral("Press Step or Start."));
        m_memoryDetail->setText(QString());
        m_readyQueue->setText(QStringLiteral("-"));
        m_pcb->setRowCount(0);
        m_gantt->clear();
        m_log->clear();
        m_results->hide();
        return;
    }

    // CPU
    const int run = m_sim.running();
    if (run >= 0) {
        const os::SimProcess& p = procs[static_cast<std::size_t>(run)];
        m_cpu->setText(nameOf(run));
        m_cpuDetail->setText(QStringLiteral("PID %1, %2 unit(s) left. Scheduler: %3%4")
                                 .arg(p.pid)
                                 .arg(p.remaining)
                                 .arg(QString::fromStdString(os::algorithmName(m_sim.config().algorithm)))
                                 .arg(m_sim.config().algorithm == os::Algorithm::RoundRobin
                                          ? QStringLiteral(", quantum %1").arg(m_sim.config().quantum)
                                          : QString()));
    } else {
        m_cpu->setText(m_sim.finished() ? QStringLiteral("Done") : QStringLiteral("Idle"));
        m_cpuDetail->setText(m_sim.finished() ? QStringLiteral("Every program has terminated.")
                                              : QStringLiteral("No program is ready to run."));
    }

    // Memory
    auto* grid = static_cast<QGridLayout*>(m_memoryGrid->layout());
    int used = 0;
    for (int b = 0; b < static_cast<int>(m_sim.memory().size()); ++b) {
        auto* block = qobject_cast<QLabel*>(grid->itemAt(b) ? grid->itemAt(b)->widget() : nullptr);
        if (!block)
            continue;
        const int owner = m_sim.memory()[static_cast<std::size_t>(b)];
        if (owner >= 0) {
            ++used;
            const QColor c = theme::processColor(owner);
            block->setText(nameOf(owner).section(QLatin1Char('.'), 0, 0).left(7));
            block->setStyleSheet(QStringLiteral("background:%1; color:#FFFFFF; border-radius:4px; font-size:8.5pt;").arg(c.name()));
        } else {
            block->setText(QStringLiteral("free"));
            block->setStyleSheet(QStringLiteral("background:#F8FAFC; color:#94A3B8; border:1px dashed #CBD5E1; "
                                                "border-radius:4px; font-size:8.5pt;"));
        }
    }
    m_memoryDetail->setText(QStringLiteral("%1 of %2 blocks used.").arg(used).arg(static_cast<int>(m_sim.memory().size())));

    // Ready queue
    QStringList ready;
    for (int i : m_sim.readyQueue())
        ready << nameOf(i).toHtmlEscaped();
    m_readyQueue->setText(ready.isEmpty() ? QStringLiteral("empty") : ready.join(QStringLiteral("  &rarr;  ")));

    // PCB table
    m_pcb->setRowCount(static_cast<int>(procs.size()));
    for (int r = 0; r < static_cast<int>(procs.size()); ++r) {
        const os::SimProcess& p = procs[static_cast<std::size_t>(r)];
        m_pcb->setItem(r, 0, makeNumberItem(p.pid));
        m_pcb->setItem(r, 1, makeItem(QString::fromStdString(p.program.name)));
        QTableWidgetItem* state = makeStatusItem(QString::fromLatin1(os::simStateName(p.state)));
        state->setText(QString::fromStdString(os::simStateDetail(p.state)));
        m_pcb->setItem(r, 2, state);
        m_pcb->setItem(r, 3, makeNumberItem(p.remaining));
        m_pcb->setItem(r, 4, makeItem(p.memoryStart >= 0 ? QStringLiteral("blocks %1-%2")
                                                               .arg(p.memoryStart)
                                                               .arg(p.memoryStart + p.program.memory - 1)
                                                         : QStringLiteral("-")));
        m_pcb->setItem(r, 5, makeNumberItem(p.readyWait));
        m_pcb->setItem(r, 6, makeNumberItem(p.memoryWait));
        m_pcb->setItem(r, 7, p.completion >= 0 ? makeNumberItem(p.completion) : makeItem(QStringLiteral("-")));
    }

    // Gantt chart: reuse the scheduler's chart widget
    os::ScheduleResult chart;
    chart.makespan = static_cast<int>(m_sim.gantt().size());
    for (const os::SimProcess& p : procs) {
        os::ProcessResult pr;
        pr.process.id = p.program.name;
        chart.processes.push_back(pr);
    }
    const auto& g = m_sim.gantt();
    for (int t = 0; t < static_cast<int>(g.size());) {
        int end = t;
        while (end < static_cast<int>(g.size()) && g[static_cast<std::size_t>(end)] == g[static_cast<std::size_t>(t)])
            ++end;
        const int who = g[static_cast<std::size_t>(t)];
        chart.timeline.push_back({who < 0 ? std::string() : procs[static_cast<std::size_t>(who)].program.name, t, end, who < 0});
        t = end;
    }
    if (chart.makespan > 0)
        m_gantt->setSchedule(chart);
    else
        m_gantt->clear();

    // Event log, newest first
    m_log->clear();
    const auto& events = m_sim.events();
    for (auto it = events.rbegin(); it != events.rend(); ++it)
        m_log->addItem(QStringLiteral("t=%1   %2").arg(it->time).arg(QString::fromStdString(it->text)));

    // Results when finished
    if (m_sim.finished()) {
        m_results->setText(QStringLiteral("Finished at time %1. Average turnaround %2, average ready wait %3, CPU busy %4 %, context switches %5.")
                               .arg(m_sim.time())
                               .arg(m_sim.averageTurnaround(), 0, 'f', 2)
                               .arg(m_sim.averageReadyWait(), 0, 'f', 2)
                               .arg(m_sim.cpuUtilisation() * 100.0, 0, 'f', 0)
                               .arg(m_sim.contextSwitches()));
        m_results->show();
    } else {
        m_results->hide();
    }
}

}  // namespace labdesk::ui
