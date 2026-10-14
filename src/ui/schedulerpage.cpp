#include "schedulerpage.h"

#include "ganttchartwidget.h"
#include "layouthelpers.h"
#include "services.h"
#include "timeutil.h"
#include "uihelpers.h"

#include <QColor>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPair>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

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

enum InputColumn { ColId = 0, ColArrival, ColBurst, ColPriority, ColSource };

constexpr qlonglong kDemoKey = -1;  // combo data for the unlinked demo workload

const os::Algorithm kAlgorithms[] = {os::Algorithm::FCFS, os::Algorithm::SJF, os::Algorithm::SRTF,
                                     os::Algorithm::Priority, os::Algorithm::RoundRobin};

QLabel* makeStepTitle(const QString& text) { return makeLabel(text, QStringLiteral("sectionTitle")); }

}  // namespace

SchedulerPage::SchedulerPage(Services& services, QWidget* parent) : Page(parent), m_services(services)
{
    QWidget* body = makeScrollPage(this);
    QVBoxLayout* root = makePageLayout(
        body, QStringLiteral("CPU scheduling"),
        QStringLiteral("Simulate how an operating system shares one CPU among several processes."));
    root->addWidget(makeExplainer(QStringLiteral(
        "When several processes are ready, the operating system's scheduler decides which one runs next. Each "
        "process here has an arrival time (when it becomes ready), a burst time (how much CPU time it needs) and a "
        "priority. Pick a workload, choose an algorithm and run it to see the order of execution and how long each "
        "process waited. All numbers are simulation inputs that you enter or saved earlier; they are not "
        "measurements of real Windows processes, and a lab session's duration is not CPU burst time.")));

    m_banner = new MessageBanner();
    root->addWidget(m_banner);

    root->addWidget(buildWorkloadCard());

    auto* middle = new QHBoxLayout();
    middle->setSpacing(16);
    middle->addWidget(buildProcessCard(), 3);
    middle->addWidget(buildAlgorithmCard(), 2);
    root->addLayout(middle);

    root->addWidget(buildResultsCard());
    root->addStretch(1);

    onAlgorithmChanged();
    clearResults();
}

// ------------------------------------------------------------------ building

QWidget* SchedulerPage::buildWorkloadCard()
{
    QFrame* card = makeCard();
    QVBoxLayout* layout = cardLayout(card);
    layout->addWidget(makeStepTitle(QStringLiteral("1. Choose a workload")));
    layout->addWidget(makeLabel(
        QStringLiteral("A saved workload is the set of simulation processes needed to reproduce a run. It can be "
                       "linked to a lab session, or kept as the stand-alone demo workload. Choosing an entry loads "
                       "its saved processes from the database."),
        QStringLiteral("caption")));

    auto* row = new QHBoxLayout();
    row->setSpacing(10);
    row->addWidget(makeLabel(QStringLiteral("Workload"), QStringLiteral("fieldLabel")));
    m_workload = new QComboBox();
    m_workload->setMinimumWidth(360);
    row->addWidget(m_workload, 1);
    m_revert = makeButton(QStringLiteral("Revert to saved"));
    m_revert->setToolTip(QStringLiteral("Discard table edits and reload this workload from the database"));
    m_save = makeButton(QStringLiteral("Save workload"), QStringLiteral("primary"));
    row->addWidget(m_revert);
    row->addWidget(m_save);
    layout->addLayout(row);

    m_sessionDetails = makeExplainer(QString());
    layout->addWidget(m_sessionDetails);
    m_workloadStatus = new MessageBanner();
    m_workloadStatus->setPersistent(true);
    layout->addWidget(m_workloadStatus);

    connect(m_workload, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SchedulerPage::onWorkloadChosen);
    connect(m_save, &QPushButton::clicked, this, &SchedulerPage::onSaveWorkload);
    connect(m_revert, &QPushButton::clicked, this, [this]() {
        if (!m_dirty || confirm(this, QStringLiteral("Revert to saved"),
                                QStringLiteral("Discard your table edits and reload %1 from the database?")
                                    .arg(workloadName(m_loadedKey)),
                                QStringLiteral("Discard edits"))) {
            loadSelectedWorkload();
        }
    });
    return card;
}

QWidget* SchedulerPage::buildProcessCard()
{
    QFrame* card = makeCard();
    QVBoxLayout* layout = cardLayout(card);
    layout->addWidget(makeStepTitle(QStringLiteral("2. Process details")));

    m_input = new QTableWidget();
    m_input->setColumnCount(5);
    m_input->setHorizontalHeaderLabels({QStringLiteral("Process ID"), QStringLiteral("Arrival time"),
                                        QStringLiteral("Burst time"), QStringLiteral("Priority"),
                                        QStringLiteral("Source")});
    const QString tips[] = {
        QStringLiteral("A short name such as P1. Must be unique within the workload."),
        QStringLiteral("Time unit at which the process enters the ready queue (0 or more)."),
        QStringLiteral("CPU time the process needs, in time units (1 or more)."),
        QStringLiteral("0 to 99. A LOWER number means a HIGHER priority. Used only by Priority scheduling."),
        QStringLiteral("Where the row came from: the database, a new row you added, or the textbook example.")};
    for (int c = 0; c < 5; ++c)
        m_input->horizontalHeaderItem(c)->setToolTip(tips[c]);
    m_input->verticalHeader()->setVisible(false);
    m_input->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_input->setSelectionMode(QAbstractItemView::SingleSelection);
    m_input->setAlternatingRowColors(true);
    m_input->setShowGrid(false);
    m_input->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_input->horizontalHeader()->setSectionResizeMode(ColSource, QHeaderView::ResizeToContents);
    m_input->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    m_input->setItemDelegateForColumn(ColArrival, new SpinBoxDelegate(0, os::kMaxArrival, m_input));
    m_input->setItemDelegateForColumn(ColBurst, new SpinBoxDelegate(1, os::kMaxBurst, m_input));
    m_input->setItemDelegateForColumn(ColPriority, new SpinBoxDelegate(os::kMinPriority, os::kMaxPriority, m_input));
    m_input->setMinimumHeight(230);
    layout->addWidget(m_input, 1);
    m_inputState = makeStateMessage();
    layout->addWidget(m_inputState);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    auto* add = makeButton(QStringLiteral("Add process"));
    auto* remove = makeButton(QStringLiteral("Remove selected"));
    auto* sample = makeButton(QStringLiteral("Use textbook example"));
    sample->setToolTip(QStringLiteral("Fill the table with the four-process example from the documentation. "
                                      "These rows are marked as example values until you save them."));
    auto* clear = makeButton(QStringLiteral("Clear table"));
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(sample);
    buttons->addWidget(clear);
    layout->addLayout(buttons);
    layout->addWidget(makeLabel(QStringLiteral("Double-click a cell to edit it. Arrival and burst are in abstract "
                                               "time units. Edits stay local until you save the workload."),
                                QStringLiteral("caption")));

    connect(add, &QPushButton::clicked, this, &SchedulerPage::onAddProcess);
    connect(remove, &QPushButton::clicked, this, &SchedulerPage::onRemoveProcess);
    connect(sample, &QPushButton::clicked, this, &SchedulerPage::loadTextbookSample);
    connect(clear, &QPushButton::clicked, this, &SchedulerPage::onClearProcesses);
    connect(m_input, &QTableWidget::itemChanged, this, &SchedulerPage::onCellChanged);
    return card;
}

QWidget* SchedulerPage::buildAlgorithmCard()
{
    QFrame* card = makeCard();
    card->setMinimumWidth(320);
    QVBoxLayout* layout = cardLayout(card);
    layout->addWidget(makeStepTitle(QStringLiteral("3. Algorithm and simulation")));
    m_algorithm = new QComboBox();
    m_algorithm->addItem(QStringLiteral("First Come First Serve (FCFS)"), static_cast<int>(os::Algorithm::FCFS));
    m_algorithm->addItem(QStringLiteral("Shortest Job First (SJF, non-preemptive)"),
                         static_cast<int>(os::Algorithm::SJF));
    m_algorithm->addItem(QStringLiteral("Shortest Remaining Time First (SRTF, preemptive)"),
                         static_cast<int>(os::Algorithm::SRTF));
    m_algorithm->addItem(QStringLiteral("Priority (non-preemptive)"), static_cast<int>(os::Algorithm::Priority));
    m_algorithm->addItem(QStringLiteral("Round Robin (preemptive)"), static_cast<int>(os::Algorithm::RoundRobin));
    layout->addWidget(m_algorithm);
    m_algorithmHelp = makeExplainer(QString());
    layout->addWidget(m_algorithmHelp);

    auto* quantumRow = new QHBoxLayout();
    m_quantumLabel = makeLabel(QStringLiteral("Time quantum (Round Robin only)"), QStringLiteral("fieldLabel"));
    quantumRow->addWidget(m_quantumLabel, 1);
    m_quantum = new QSpinBox();
    m_quantum->setRange(1, os::kMaxQuantum);
    m_quantum->setValue(2);
    m_quantum->setToolTip(QStringLiteral("Maximum CPU time a process may use before it is moved to the back of "
                                         "the ready queue."));
    quantumRow->addWidget(m_quantum);
    layout->addLayout(quantumRow);

    auto* run = makeButton(QStringLiteral("Run simulation"), QStringLiteral("primary"));
    auto* compare = makeButton(QStringLiteral("Compare all five algorithms"));
    layout->addWidget(run);
    layout->addWidget(compare);
    layout->addWidget(makeLabel(QStringLiteral("Formulas: Turnaround = Completion - Arrival.  Waiting = Turnaround - "
                                               "Burst.  Response = First run - Arrival. Ties go to the earlier "
                                               "arrival, then to the earlier row."),
                                QStringLiteral("caption")));
    layout->addStretch(1);

    connect(m_algorithm, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SchedulerPage::onAlgorithmChanged);
    connect(run, &QPushButton::clicked, this, &SchedulerPage::onRun);
    connect(compare, &QPushButton::clicked, this, &SchedulerPage::onCompareAll);
    return card;
}

QWidget* SchedulerPage::buildResultsCard()
{
    QFrame* card = makeCard();
    QVBoxLayout* layout = cardLayout(card);
    layout->setSpacing(12);
    layout->addWidget(makeStepTitle(QStringLiteral("4. Gantt chart and results")));
    m_resultTitle = makeLabel(QString(), QStringLiteral("caption"));
    layout->addWidget(m_resultTitle);

    // Average metrics as small figures.
    auto* metrics = new ResponsiveGrid(150, 12);
    QList<QWidget*> tiles;
    const QPair<QString, QString> names[] = {{QStringLiteral("wt"), QStringLiteral("Average waiting time")},
                                             {QStringLiteral("tat"), QStringLiteral("Average turnaround time")},
                                             {QStringLiteral("rt"), QStringLiteral("Average response time")},
                                             {QStringLiteral("util"), QStringLiteral("CPU utilisation")},
                                             {QStringLiteral("cs"), QStringLiteral("Context switches")}};
    for (const auto& n : names) {
        auto* tile = new QFrame();
        tile->setObjectName(QStringLiteral("card"));
        auto* v = new QVBoxLayout(tile);
        v->setContentsMargins(12, 10, 12, 10);
        v->setSpacing(2);
        v->addWidget(makeLabel(n.second, QStringLiteral("statLabel")));
        auto* value = makeLabel(QStringLiteral("-"), QStringLiteral("statValue"));
        v->addWidget(value);
        m_metricValues.insert(n.first, value);
        tiles.append(tile);
    }
    metrics->setWidgets(tiles);
    m_metrics = metrics;
    layout->addWidget(metrics);

    auto* ganttScroll = new QScrollArea();
    ganttScroll->setWidgetResizable(true);
    ganttScroll->setFrameShape(QFrame::NoFrame);
    ganttScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gantt = new GanttChartWidget();
    ganttScroll->setWidget(m_gantt);
    ganttScroll->setFixedHeight(m_gantt->sizeHint().height() + 18);
    m_ganttArea = ganttScroll;
    layout->addWidget(ganttScroll);

    m_results = new QTableWidget();
    setupTable(m_results, {QStringLiteral("Process ID"), QStringLiteral("Arrival"), QStringLiteral("Burst"),
                           QStringLiteral("Priority"), QStringLiteral("First run"), QStringLiteral("Completion"),
                           QStringLiteral("Turnaround"), QStringLiteral("Waiting"), QStringLiteral("Response")});
    m_results->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_results->setMinimumHeight(200);
    layout->addWidget(m_results);
    m_resultsState = makeStateMessage();
    layout->addWidget(m_resultsState);

    m_comparisonTitle = makeLabel(QStringLiteral("Comparison on the same workload"), QStringLiteral("sectionTitle"));
    layout->addWidget(m_comparisonTitle);
    m_comparison = new QTableWidget();
    setupTable(m_comparison, {QStringLiteral("Algorithm"), QStringLiteral("Avg waiting"),
                              QStringLiteral("Avg turnaround"), QStringLiteral("Avg response"),
                              QStringLiteral("Context switches"), QStringLiteral("CPU utilisation")});
    m_comparison->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_comparison->setFixedHeight(5 * 32 + 42);
    layout->addWidget(m_comparison);
    m_comparisonTitle->hide();
    m_comparison->hide();
    return card;
}

// ---------------------------------------------------------- workload handling

void SchedulerPage::refresh()
{
    QHash<qint64, int> sizes;
    QString sizeError;
    m_services.processes.workloadSizes(sizes, &sizeError);
    QString sessionError;
    const auto sessions = m_services.sessions.listSessions(false, 100, &sessionError);

    const QVariant previous = m_workload->currentData();
    m_workload->blockSignals(true);
    m_workload->clear();
    auto describe = [&sizes](qint64 key) {
        const int n = sizes.value(key, 0);
        return n > 0 ? QStringLiteral("%1 saved process(es)").arg(n) : QStringLiteral("no saved workload");
    };
    m_workload->addItem(QStringLiteral("Demo workload, not linked to a session (%1)")
                            .arg(describe(ProcessService::kUnlinkedKey)),
                        kDemoKey);
    for (const SessionRecord& s : sessions) {
        m_workload->addItem(QStringLiteral("Session #%1: %2 on %3, %4 (%5)")
                                .arg(s.sessionId)
                                .arg(s.studentId, s.computerId, s.status.toLower(), describe(s.sessionId)),
                            static_cast<qlonglong>(s.sessionId));
    }
    int index = previous.isValid() ? m_workload->findData(previous) : 0;
    m_workload->setCurrentIndex(index >= 0 ? index : 0);
    m_workload->blockSignals(false);

    if (!sessionError.isEmpty())
        m_banner->showError(QStringLiteral("Lab sessions could not be listed: ") + sessionError);
    else if (!sizeError.isEmpty())
        m_banner->showWarning(QStringLiteral("Saved workload sizes could not be read: ") + sizeError);

    if (!m_loaded)
        loadSelectedWorkload();  // first time: show the selected workload straight away
    else
        updateWorkloadStatus();
}

std::optional<qint64> SchedulerPage::selectedWorkloadKey() const
{
    const qlonglong v = m_workload->currentData().toLongLong();
    if (v < 0)
        return std::nullopt;
    return v;
}

QString SchedulerPage::workloadName(std::optional<qint64> key) const
{
    return key ? QStringLiteral("the workload of session #%1").arg(*key) : QStringLiteral("the demo workload");
}

void SchedulerPage::onWorkloadChosen(int)
{
    if (m_dirty) {
        const bool discard = confirm(this, QStringLiteral("Unsaved changes"),
                                     QStringLiteral("The process table has changes that are not saved to %1. "
                                                    "Discard them and load the selected workload?")
                                         .arg(workloadName(m_loadedKey)),
                                     QStringLiteral("Discard and load"));
        if (!discard) {
            // Put the selector back on the workload the table belongs to.
            const int back = m_workload->findData(m_loadedKey ? static_cast<qlonglong>(*m_loadedKey) : kDemoKey);
            m_workload->blockSignals(true);
            m_workload->setCurrentIndex(back >= 0 ? back : 0);
            m_workload->blockSignals(false);
            return;
        }
    }
    loadSelectedWorkload();
}

void SchedulerPage::showSessionDetails(std::optional<qint64> key)
{
    if (!key) {
        m_sessionDetails->setText(QStringLiteral("Demo workload: a stand-alone set of simulation processes that is "
                                                 "not linked to any lab session."));
        return;
    }
    QString error;
    const auto session = m_services.sessions.findSession(*key, &error);
    if (!error.isEmpty()) {
        m_sessionDetails->setText(QStringLiteral("Session #%1 could not be read from the database: %2")
                                      .arg(*key)
                                      .arg(error));
        return;
    }
    if (!session) {
        m_sessionDetails->setText(QStringLiteral("Session #%1 was not found in the database.").arg(*key));
        return;
    }
    const QString ended = session->endTime.isEmpty() ? QStringLiteral("still active")
                                                     : QStringLiteral("ended %1").arg(timeutil::toDisplay(session->endTime));
    m_sessionDetails->setText(
        QStringLiteral("Session #%1 (from the database): %2 (%3) on %4, %5. Started %6, %7, duration %8.\n"
                       "The session duration is how long the student used the computer. It is not CPU burst time, "
                       "so the process values below are separate simulation inputs saved for this session.")
            .arg(session->sessionId)
            .arg(session->studentName, session->studentId, session->computerId, session->location,
                 timeutil::toDisplay(session->startTime), ended,
                 timeutil::durationText(session->startTime, session->endTime)));
}

void SchedulerPage::loadSelectedWorkload()
{
    const std::optional<qint64> key = selectedWorkloadKey();
    showSessionDetails(key);

    QString error;
    const auto rows = m_services.processes.loadWorkload(key, &error);

    m_populating = true;
    m_input->setRowCount(0);
    m_savedRowIds.clear();
    if (error.isEmpty()) {
        for (const StoredProcess& p : rows) {
            addProcessRow(p.label, p.arrival, p.burst, p.priority, RowSource::Saved, p.processId);
            m_savedRowIds.append(p.processId);
        }
    }
    m_populating = false;

    m_loaded = true;
    m_loadedKey = key;
    m_hasSavedWorkload = error.isEmpty() && !rows.empty();
    m_dirty = false;
    clearResults();
    updateTableState();

    if (!error.isEmpty()) {
        m_banner->showError(QStringLiteral("The saved workload could not be loaded: ") + error);
        showErrorState(m_inputState, error);
    }
    updateWorkloadStatus();
}

void SchedulerPage::updateWorkloadStatus()
{
    const QString name = workloadName(m_loadedKey);
    const QString capitalName = name.left(1).toUpper() + name.mid(1);
    if (m_dirty) {
        m_workloadStatus->showWarning(
            QStringLiteral("Unsaved changes. The table no longer matches %1 in the database. Press \"%2\" to store "
                           "it, or \"Revert to saved\" to discard the edits.")
                .arg(name, m_hasSavedWorkload ? QStringLiteral("Update saved workload")
                                              : QStringLiteral("Save workload")));
    } else if (m_hasSavedWorkload) {
        const qint64 low = *std::min_element(m_savedRowIds.begin(), m_savedRowIds.end());
        const qint64 high = *std::max_element(m_savedRowIds.begin(), m_savedRowIds.end());
        m_workloadStatus->showInfo(QStringLiteral("Loaded from the database: %1 has %2 saved process(es) (PROCESS "
                                                  "table rows #%3 to #%4). Process IDs and values are shown exactly "
                                                  "as stored.")
                                       .arg(capitalName)
                                       .arg(static_cast<int>(m_savedRowIds.size()))
                                       .arg(low)
                                       .arg(high));
    } else {
        m_workloadStatus->showInfo(
            QStringLiteral("%1 has no saved processes yet. Nothing has been filled in for you: add processes or use "
                           "the textbook example, then press \"Save workload\" to store them.")
                .arg(capitalName));
    }
    m_save->setText(m_hasSavedWorkload ? QStringLiteral("Update saved workload") : QStringLiteral("Save workload"));
    m_save->setEnabled(m_input->rowCount() > 0 && (m_dirty || !m_hasSavedWorkload));
    m_revert->setEnabled(m_dirty);
}

void SchedulerPage::markDirty()
{
    if (m_populating)
        return;
    m_dirty = true;
    clearResults();
    updateTableState();
    updateWorkloadStatus();
}

void SchedulerPage::onSaveWorkload()
{
    std::vector<os::Process> processes;
    if (!readProcesses(processes))
        return;
    const std::optional<qint64> key = selectedWorkloadKey();
    if (key != m_loadedKey) {
        m_banner->showError(QStringLiteral("The selected workload changed; reload it before saving."));
        return;
    }
    if (m_hasSavedWorkload
        && !confirm(this, QStringLiteral("Update saved workload"),
                    QStringLiteral("Replace the %1 saved process(es) of %2 with the %3 process(es) in the table? "
                                   "This happens in one transaction: either all rows are replaced or none are.")
                        .arg(static_cast<int>(m_savedRowIds.size()))
                        .arg(workloadName(key))
                        .arg(static_cast<int>(processes.size())),
                    QStringLiteral("Update workload"))) {
        return;
    }
    std::vector<StoredProcess> rows;
    for (const os::Process& p : processes) {
        StoredProcess s;
        s.label = QString::fromStdString(p.id);
        s.arrival = p.arrival;
        s.burst = p.burst;
        s.priority = p.priority;
        rows.push_back(s);
    }
    const OpResult r = m_services.processes.saveWorkload(key, rows);
    if (!r.ok) {
        m_banner->showError(r.message);
        return;
    }
    loadSelectedWorkload();  // re-read so the table shows exactly what is stored
    m_banner->showSuccess(QStringLiteral("%1 Stored as %2.").arg(r.message, workloadName(key)));
    emit dataChanged();  // other pages (and this page's selector) show the new size
}

// ---------------------------------------------------------------- the table

void SchedulerPage::addProcessRow(const QString& id, int arrival, int burst, int priority, RowSource source,
                                  qint64 databaseRow)
{
    const bool wasPopulating = m_populating;
    m_populating = true;
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
    QString text;
    switch (source) {
    case RowSource::Saved: text = QStringLiteral("Database row #%1").arg(databaseRow); break;
    case RowSource::New: text = QStringLiteral("New, not saved"); break;
    case RowSource::Example: text = QStringLiteral("Example, not saved"); break;
    }
    auto* sourceItem = new QTableWidgetItem(text);
    sourceItem->setFlags(sourceItem->flags() & ~Qt::ItemIsEditable);
    sourceItem->setData(Qt::UserRole, static_cast<qlonglong>(databaseRow));
    sourceItem->setForeground(source == RowSource::Saved ? QColor("#1E40AF") : QColor("#64748B"));
    m_input->setItem(row, ColSource, sourceItem);
    m_populating = wasPopulating;
}

void SchedulerPage::onCellChanged(QTableWidgetItem* item)
{
    if (m_populating || !item || item->column() == ColSource)
        return;
    QTableWidgetItem* source = m_input->item(item->row(), ColSource);
    if (source && source->data(Qt::UserRole).toLongLong() > 0) {
        m_populating = true;  // the text change below must not re-enter this handler's logic
        source->setText(QStringLiteral("Database row #%1, edited").arg(source->data(Qt::UserRole).toLongLong()));
        m_populating = false;
    }
    markDirty();
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
    addProcessRow(QStringLiteral("P%1").arg(n), 0, 1, 0, RowSource::New);
    m_input->selectRow(m_input->rowCount() - 1);
    markDirty();
    m_banner->showInfo(QStringLiteral("Added P%1 with arrival 0, burst 1, priority 0. Double-click a cell to "
                                      "change these simulation values.")
                           .arg(n));
}

void SchedulerPage::onRemoveProcess()
{
    const auto items = m_input->selectedItems();
    if (items.isEmpty()) {
        m_banner->showWarning(QStringLiteral("Select a process row to remove."));
        return;
    }
    m_input->removeRow(items.first()->row());
    markDirty();
}

void SchedulerPage::onClearProcesses()
{
    if (m_input->rowCount() == 0)
        return;
    m_input->setRowCount(0);
    markDirty();
}

void SchedulerPage::loadTextbookSample()
{
    if (m_input->rowCount() > 0
        && !confirm(this, QStringLiteral("Use textbook example"),
                    QStringLiteral("Replace the processes in the table with the textbook example? Nothing is "
                                   "saved until you press Save."),
                    QStringLiteral("Replace rows"))) {
        return;
    }
    m_populating = true;
    m_input->setRowCount(0);
    addProcessRow(QStringLiteral("P1"), 0, 5, 2, RowSource::Example);
    addProcessRow(QStringLiteral("P2"), 1, 3, 3, RowSource::Example);
    addProcessRow(QStringLiteral("P3"), 2, 8, 1, RowSource::Example);
    addProcessRow(QStringLiteral("P4"), 3, 6, 4, RowSource::Example);
    m_populating = false;
    markDirty();
}

void SchedulerPage::updateTableState()
{
    if (m_input->rowCount() > 0) {
        m_inputState->hide();
        return;
    }
    if (m_hasSavedWorkload || m_dirty)
        showEmptyState(m_inputState, QStringLiteral("The table is empty. Add processes to build a workload."));
    else
        showEmptyState(m_inputState,
                       QStringLiteral("No saved workload for this selection. A lab session does not provide CPU "
                                      "scheduling data by itself, so enter simulation values with \"Add process\" "
                                      "or \"Use textbook example\"."));
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

// ---------------------------------------------------------------- simulation

void SchedulerPage::onAlgorithmChanged()
{
    const auto algorithm = static_cast<os::Algorithm>(m_algorithm->currentData().toInt());
    m_algorithmHelp->setText(QString::fromStdString(os::algorithmDescription(algorithm)));
    const bool rr = algorithm == os::Algorithm::RoundRobin;
    m_quantum->setEnabled(rr);
    m_quantumLabel->setEnabled(rr);
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
    const int count = static_cast<int>(sizeof(kAlgorithms) / sizeof(kAlgorithms[0]));
    m_comparison->setRowCount(count);
    try {
        for (int i = 0; i < count; ++i) {
            const os::ScheduleResult r = os::Scheduler::run(kAlgorithms[i], processes, m_quantum->value());
            QString name = QString::fromStdString(os::algorithmName(kAlgorithms[i]));
            if (kAlgorithms[i] == os::Algorithm::RoundRobin)
                name += QStringLiteral(" (quantum %1)").arg(m_quantum->value());
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
    m_banner->showInfo(QStringLiteral("Compared all five algorithms on the same %1 processes. Lower waiting and "
                                      "turnaround times are better.")
                           .arg(static_cast<int>(processes.size())));
}

void SchedulerPage::clearResults()
{
    m_resultTitle->setText(QString());
    m_resultTitle->hide();
    for (QLabel* value : m_metricValues)
        value->setText(QStringLiteral("-"));
    m_metrics->hide();
    m_gantt->clear();
    m_ganttArea->hide();
    m_results->setRowCount(0);
    m_results->hide();
    m_comparison->setRowCount(0);
    m_comparison->hide();
    m_comparisonTitle->hide();
    showEmptyState(m_resultsState, QStringLiteral("No results yet. Choose an algorithm and press \"Run "
                                                  "simulation\". Results are cleared whenever the processes change."));
}

void SchedulerPage::showResult(const os::ScheduleResult& r)
{
    QString title = QString::fromStdString(os::algorithmName(r.algorithm));
    if (r.algorithm == os::Algorithm::RoundRobin)
        title += QStringLiteral(", time quantum %1").arg(r.quantum);
    m_resultTitle->setText(QStringLiteral("%1 on %2 process(es). All processes finished at time %3; the CPU was "
                                          "idle for %4 time unit(s). Hatched bars in the chart are idle time.")
                               .arg(title)
                               .arg(static_cast<int>(r.processes.size()))
                               .arg(r.makespan)
                               .arg(r.idleTime));
    m_resultTitle->show();
    m_metricValues.value(QStringLiteral("wt"))->setText(QString::number(r.averageWaiting, 'f', 2));
    m_metricValues.value(QStringLiteral("tat"))->setText(QString::number(r.averageTurnaround, 'f', 2));
    m_metricValues.value(QStringLiteral("rt"))->setText(QString::number(r.averageResponse, 'f', 2));
    m_metricValues.value(QStringLiteral("util"))->setText(QStringLiteral("%1 %").arg(r.cpuUtilisation * 100.0, 0, 'f', 1));
    m_metricValues.value(QStringLiteral("cs"))->setText(QString::number(r.contextSwitches));
    m_metrics->show();
    m_gantt->setSchedule(r);
    m_ganttArea->show();

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
    m_results->show();
    m_resultsState->hide();
}

}  // namespace labdesk::ui
