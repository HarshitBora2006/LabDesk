#include "labqueuepage.h"

#include "labqueue.h"
#include "layouthelpers.h"
#include "services.h"
#include "timeutil.h"
#include "uihelpers.h"

#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPair>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <stdexcept>

namespace labdesk::ui {

namespace {

qint64 toMs(const QString& iso)
{
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(iso, Qt::ISODate);
    return dt.isValid() ? dt.toMSecsSinceEpoch() : 0;
}

// Exact, readable durations: m:ss below one hour, h:mm:ss above (e.g. 0:04, 2:27, 1:05:12).
QString minutes(double value)
{
    const bool negative = value < 0;
    long long secs = static_cast<long long>(std::llround((negative ? -value : value) * 60.0));
    const long long h = secs / 3600;
    const long long m = (secs % 3600) / 60;
    secs %= 60;
    QString text = h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0'))
                         : QStringLiteral("%1:%2").arg(m).arg(secs, 2, 10, QLatin1Char('0'));
    return negative ? QStringLiteral("-") + text : text;
}

QTableWidgetItem* durationItem(double minutesValue)
{
    QTableWidgetItem* item = makeItem(minutes(minutesValue));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

lab::MeasuredVisit toVisit(const RequestRecord& r)
{
    lab::MeasuredVisit v;
    v.arrivalMs = toMs(r.requestedAt);
    if (!r.startTime.isEmpty())
        v.startMs = toMs(r.startTime);
    if (!r.endTime.isEmpty())
        v.endMs = toMs(r.endTime);
    v.expectedMinutes = r.expectedMinutes;
    return v;
}

}  // namespace

LabQueuePage::LabQueuePage(Services& services, QWidget* parent) : Page(parent), m_services(services)
{
    QWidget* body = makeScrollPage(this);
    QVBoxLayout* root = makePageLayout(body, QStringLiteral("Lab queue"),
                                       QStringLiteral("Who gets the next free computer, and the measured waiting "
                                                      "and usage times of real visits."));
    root->addWidget(makeExplainer(QStringLiteral(
        "This applies CPU-scheduling ideas to the lab itself: each student request is a job and each computer is a "
        "processor. Arrival is the moment a student requests a computer, waiting is the time until one is assigned, "
        "and burst is the actual session length, measured when the session ends. Nothing here is typed in or "
        "simulated except the what-if replay at the bottom. Sessions are never interrupted, so every policy is "
        "non-preemptive.")));
    m_banner = new MessageBanner();
    root->addWidget(m_banner);
    root->addWidget(buildPolicyCard());
    root->addWidget(buildQueueCard());
    root->addWidget(buildMeasuredCard());
    root->addWidget(buildReplayCard());
    root->addStretch(1);

    m_tick = new QTimer(this);
    m_tick->setInterval(30000);
    connect(m_tick, &QTimer::timeout, this, [this]() {
        if (isVisible())
            refreshQueue();
    });
    m_tick->start();
    onPolicyChosen();
}

QWidget* LabQueuePage::buildPolicyCard()
{
    QFrame* card = makeCard();
    QVBoxLayout* layout = cardLayout(card);
    layout->addWidget(makeLabel(QStringLiteral("Queue policy"), QStringLiteral("sectionTitle")));
    m_currentPolicy = makeLabel(QString(), QStringLiteral("caption"));
    layout->addWidget(m_currentPolicy);
    auto* row = new QHBoxLayout();
    row->setSpacing(10);
    m_policy = new QComboBox();
    for (lab::Policy p : {lab::Policy::FCFS, lab::Policy::SJF, lab::Policy::Priority})
        m_policy->addItem(QString::fromStdString(lab::policyName(p)), QString::fromLatin1(lab::policyCode(p)));
    row->addWidget(m_policy, 1);
    auto* apply = makeButton(QStringLiteral("Apply policy"), QStringLiteral("primary"));
    row->addWidget(apply);
    layout->addLayout(row);
    m_policyHelp = makeExplainer(QString());
    layout->addWidget(m_policyHelp);
    layout->addWidget(makeLabel(QStringLiteral("Priority comes from the purpose a student chooses: exam 1, assignment "
                                               "2, practice 3 (lower number = served first). Ties always go to the "
                                               "earlier request."),
                                QStringLiteral("caption")));
    connect(m_policy, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LabQueuePage::onPolicyChosen);
    connect(apply, &QPushButton::clicked, this, &LabQueuePage::onApplyPolicy);
    return card;
}

QWidget* LabQueuePage::buildQueueCard()
{
    QFrame* card = makeCard();
    QVBoxLayout* layout = cardLayout(card);
    auto* header = new QHBoxLayout();
    header->addWidget(makeLabel(QStringLiteral("Waiting students, in the order they will be served"),
                                QStringLiteral("sectionTitle")),
                      1);
    m_remove = makeButton(QStringLiteral("Remove selected request"), QStringLiteral("danger"));
    header->addWidget(m_remove);
    layout->addLayout(header);
    m_queue = new QTableWidget();
    setupTable(m_queue, {QStringLiteral("Position"), QStringLiteral("Request"), QStringLiteral("Student"),
                         QStringLiteral("Purpose"), QStringLiteral("Priority"), QStringLiteral("Expected (min)"),
                         QStringLiteral("Requested at"), QStringLiteral("Waiting so far (m:ss)")});
    m_queue->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    alignNumericColumns(m_queue, {0, 4, 5, 7});
    m_queue->setMinimumHeight(170);
    layout->addWidget(m_queue);
    m_queueState = makeStateMessage();
    layout->addWidget(m_queueState);
    connect(m_remove, &QPushButton::clicked, this, &LabQueuePage::onRemoveRequest);
    connect(m_queue, &QTableWidget::itemSelectionChanged, this,
            [this]() { m_remove->setEnabled(selectedRowData(m_queue, 1).isValid()); });
    m_remove->setEnabled(false);
    return card;
}

QWidget* LabQueuePage::buildMeasuredCard()
{
    QFrame* card = makeCard();
    QVBoxLayout* layout = cardLayout(card);
    layout->setSpacing(12);
    layout->addWidget(makeLabel(QStringLiteral("Measured results"), QStringLiteral("sectionTitle")));
    layout->addWidget(makeLabel(QStringLiteral("Averages use finished visits only, because the burst (actual session "
                                               "length) is unknown until a session ends. Estimate error = actual "
                                               "length minus the student's expected length: negative means the "
                                               "visit was shorter than expected. The average uses the size of each "
                                               "error, so over- and under-estimates do not cancel out."),
                                QStringLiteral("caption")));
    auto* grid = new ResponsiveGrid(150, 12);
    QList<QWidget*> tiles;
    const QPair<QString, QString> names[] = {{QStringLiteral("visits"), QStringLiteral("Finished visits")},
                                             {QStringLiteral("wt"), QStringLiteral("Avg waiting (m:ss)")},
                                             {QStringLiteral("bt"), QStringLiteral("Avg actual burst (m:ss)")},
                                             {QStringLiteral("tat"), QStringLiteral("Avg turnaround (m:ss)")},
                                             {QStringLiteral("max"), QStringLiteral("Longest wait (m:ss)")},
                                             {QStringLiteral("err"), QStringLiteral("Avg estimate error, absolute (m:ss)")}};
    for (const auto& n : names) {
        auto* tile = new QFrame();
        tile->setObjectName(QStringLiteral("card"));
        auto* v = new QVBoxLayout(tile);
        v->setContentsMargins(12, 10, 12, 10);
        v->setSpacing(2);
        v->addWidget(makeLabel(n.second, QStringLiteral("statLabel")));
        auto* value = makeLabel(QStringLiteral("-"), QStringLiteral("statValue"));
        v->addWidget(value);
        m_figures.insert(n.first, value);
        tiles.append(tile);
    }
    grid->setWidgets(tiles);
    layout->addWidget(grid);

    m_history = new QTableWidget();
    setupTable(m_history, {QStringLiteral("Request"), QStringLiteral("Student"), QStringLiteral("Purpose"),
                           QStringLiteral("Expected (min)"), QStringLiteral("Arrival (requested)"), QStringLiteral("Start"),
                           QStringLiteral("End"), QStringLiteral("Computer"), QStringLiteral("Waiting (m:ss)"),
                           QStringLiteral("Actual burst (m:ss)"), QStringLiteral("Turnaround (m:ss)"),
                           QStringLiteral("Estimate error (m:ss)"), QStringLiteral("Granted by"), QStringLiteral("Status")});
    alignNumericColumns(m_history, {3, 8, 9, 10, 11});
    m_history->setMinimumHeight(240);
    layout->addWidget(m_history);
    m_historyState = makeStateMessage();
    layout->addWidget(m_historyState);
    return card;
}

QWidget* LabQueuePage::buildReplayCard()
{
    QFrame* card = makeCard();
    QVBoxLayout* layout = cardLayout(card);
    layout->addWidget(makeLabel(QStringLiteral("What-if replay"), QStringLiteral("sectionTitle")));
    layout->addWidget(makeLabel(
        QStringLiteral("Replays every finished visit with its recorded arrival time and actual length under each "
                       "policy, on a chosen number of identical computers that never break down. SJF decides with "
                       "the expected length, exactly like the live queue. This is a simulation for comparison; the "
                       "table above is what really happened."),
        QStringLiteral("caption")));
    auto* row = new QHBoxLayout();
    row->setSpacing(10);
    row->addWidget(makeLabel(QStringLiteral("Computers"), QStringLiteral("fieldLabel")));
    m_computers = new QSpinBox();
    m_computers->setRange(1, 500);
    row->addWidget(m_computers);
    auto* run = makeButton(QStringLiteral("Replay with all three policies"), QStringLiteral("primary"));
    row->addWidget(run);
    row->addStretch(1);
    layout->addLayout(row);
    m_replay = new QTableWidget();
    setupTable(m_replay, {QStringLiteral("Policy"), QStringLiteral("Avg waiting (m:ss)"),
                          QStringLiteral("Avg turnaround (m:ss)"), QStringLiteral("Longest wait (m:ss)"),
                          QStringLiteral("All finished after (m:ss)")});
    m_replay->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    alignNumericColumns(m_replay, {1, 2, 3, 4});
    m_replay->setFixedHeight(3 * 32 + 42);
    m_replay->hide();
    layout->addWidget(m_replay);
    m_replayState = makeStateMessage();
    layout->addWidget(m_replayState);
    showEmptyState(m_replayState, QStringLiteral("Choose a number of computers and press Replay."));
    connect(run, &QPushButton::clicked, this, &LabQueuePage::onReplay);
    return card;
}

void LabQueuePage::refresh()
{
    QString error;
    const lab::Policy current = m_services.queue.policy(&error);
    if (!error.isEmpty())
        m_banner->showError(QStringLiteral("The queue policy could not be read: ") + error);
    m_currentPolicy->setText(QStringLiteral("Active policy: %1").arg(QString::fromStdString(lab::policyName(current))));
    const int index = m_policy->findData(QString::fromLatin1(lab::policyCode(current)));
    if (index >= 0) {
        m_policy->blockSignals(true);
        m_policy->setCurrentIndex(index);
        m_policy->blockSignals(false);
        onPolicyChosen();
    }
    // Default replay size: computers currently in service.
    const DashboardStats stats = m_services.computers.dashboardStats();
    if (m_computers->property("userSet").toBool() == false)
        m_computers->setValue(qMax(1, stats.totalComputers - stats.maintenance));
    refreshQueue();
    refreshMeasured();
}

void LabQueuePage::onPolicyChosen()
{
    const auto p = lab::policyFromCode(m_policy->currentData().toString().toStdString());
    m_policyHelp->setText(p ? QString::fromStdString(lab::policyDescription(*p)) : QString());
}

void LabQueuePage::onApplyPolicy()
{
    const auto p = lab::policyFromCode(m_policy->currentData().toString().toStdString());
    if (!p)
        return;
    const OpResult r = m_services.queue.setPolicy(*p);
    if (r.ok)
        m_banner->showSuccess(r.message + QStringLiteral(" The waiting list below is re-ordered accordingly."));
    else
        m_banner->showError(r.message);
    emit dataChanged();
}

void LabQueuePage::refreshQueue()
{
    std::vector<QueueEntry> queue;
    QString error;
    const bool ok = m_services.queue.waitingQueue(queue, &error);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_queue->setRowCount(static_cast<int>(queue.size()));
    for (int row = 0; row < static_cast<int>(queue.size()); ++row) {
        const QueueEntry& e = queue[static_cast<std::size_t>(row)];
        m_queue->setItem(row, 0, makeNumberItem(e.position));
        m_queue->setItem(row, 1, makeItem(QStringLiteral("#%1").arg(e.requestId), e.requestId));
        m_queue->setItem(row, 2, makeItem(QStringLiteral("%1 (%2)").arg(e.studentName, e.studentId)));
        m_queue->setItem(row, 3, makeItem(e.purpose.left(1) + e.purpose.mid(1).toLower()));
        m_queue->setItem(row, 4, makeNumberItem(e.priority));
        m_queue->setItem(row, 5, makeNumberItem(e.expectedMinutes));
        m_queue->setItem(row, 6, makeItem(timeutil::toDisplay(e.requestedAt)));
        m_queue->setItem(row, 7, durationItem((now - toMs(e.requestedAt)) / 60000.0));
    }
    if (!ok) {
        m_queue->hide();
        showErrorState(m_queueState, error);
    } else if (queue.empty()) {
        m_queue->hide();
        showEmptyState(m_queueState, QStringLiteral("No students are waiting. Students join the queue from the "
                                                    "Student portal when every computer is busy."));
    } else {
        m_queue->show();
        m_queueState->hide();
    }
    m_remove->setEnabled(false);
}

void LabQueuePage::refreshMeasured()
{
    std::vector<RequestRecord> records;
    QString error;
    const bool ok = m_services.queue.history(records, &error);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    std::vector<lab::MeasuredVisit> visits;
    m_history->setRowCount(static_cast<int>(records.size()));
    for (int row = 0; row < static_cast<int>(records.size()); ++row) {
        const RequestRecord& r = records[static_cast<std::size_t>(row)];
        const lab::MeasuredVisit v = toVisit(r);
        visits.push_back(v);
        const lab::VisitMetrics m = lab::measure(v, now);
        const bool cancelled = r.status == QStringLiteral("CANCELLED");
        m_history->setItem(row, 0, makeItem(QStringLiteral("#%1").arg(r.requestId)));
        m_history->setItem(row, 1, makeItem(QStringLiteral("%1 (%2)").arg(r.studentName, r.studentId)));
        m_history->setItem(row, 2, makeItem(r.purpose.left(1) + r.purpose.mid(1).toLower()));
        m_history->setItem(row, 3, makeItem(QString::number(r.expectedMinutes)));
        m_history->setItem(row, 4, makeItem(timeutil::toDisplay(r.requestedAt)));
        m_history->setItem(row, 5, makeItem(timeutil::toDisplay(r.startTime)));
        m_history->setItem(row, 6, makeItem(r.endTime.isEmpty() && m.started ? QStringLiteral("in use")
                                                                             : timeutil::toDisplay(r.endTime)));
        m_history->setItem(row, 7, makeItem(r.computerId.isEmpty() ? QStringLiteral("-") : r.computerId));
        m_history->setItem(row, 8, makeItem(cancelled ? QStringLiteral("-")
                                                      : minutes(m.waitingMinutes)
                                                            + (m.started ? QString() : QStringLiteral(" so far"))));
        m_history->setItem(row, 9, makeItem(m.finished ? minutes(m.burstMinutes) : QStringLiteral("-")));
        m_history->setItem(row, 10, makeItem(m.finished ? minutes(m.turnaroundMinutes) : QStringLiteral("-")));
        m_history->setItem(row, 11, makeItem(m.finished ? QStringLiteral("%1%2")
                                                              .arg(m.estimateErrorMinutes >= 0 ? QStringLiteral("+")
                                                                                               : QString())
                                                              .arg(minutes(m.estimateErrorMinutes))
                                                        : QStringLiteral("-")));
        for (int c : {3, 8, 9, 10, 11})
            m_history->item(row, c)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_history->setItem(row, 12, makeItem(r.grantedBy.isEmpty() ? QStringLiteral("-") : r.grantedBy));
        m_history->setItem(row, 13, makeStatusItem(r.status == QStringLiteral("ALLOCATED") && m.finished
                                                       ? QStringLiteral("ENDED")
                                                       : (r.status == QStringLiteral("ALLOCATED") ? QStringLiteral("ACTIVE")
                                                                                                   : r.status)));
    }
    m_history->resizeColumnsToContents();

    const lab::MeasuredSummary s = lab::summarise(visits);
    const bool any = ok && s.completed > 0;
    m_figures.value(QStringLiteral("visits"))->setText(ok ? QString::number(s.completed) : QStringLiteral("-"));
    m_figures.value(QStringLiteral("wt"))->setText(any ? minutes(s.averageWaiting) : QStringLiteral("-"));
    m_figures.value(QStringLiteral("bt"))->setText(any ? minutes(s.averageBurst) : QStringLiteral("-"));
    m_figures.value(QStringLiteral("tat"))->setText(any ? minutes(s.averageTurnaround) : QStringLiteral("-"));
    m_figures.value(QStringLiteral("max"))->setText(any ? minutes(s.maximumWaiting) : QStringLiteral("-"));
    m_figures.value(QStringLiteral("err"))->setText(any ? minutes(s.averageAbsoluteEstimateError) : QStringLiteral("-"));

    if (!ok) {
        m_history->hide();
        showErrorState(m_historyState, error);
    } else if (records.empty()) {
        m_history->hide();
        showEmptyState(m_historyState, QStringLiteral("No requests recorded yet. Data appears when students request "
                                                      "computers in the Student portal. Sessions started before "
                                                      "version 0.7 or from the transaction demonstration have no "
                                                      "request, so they have no arrival time and are not shown."));
    } else {
        m_history->show();
        m_historyState->hide();
    }
}

void LabQueuePage::onRemoveRequest()
{
    const QVariant id = selectedRowData(m_queue, 1);
    if (!id.isValid())
        return;
    if (!confirm(this, QStringLiteral("Remove request"),
                 QStringLiteral("Remove request #%1 from the queue? It is kept as a cancelled request.")
                     .arg(id.toLongLong()),
                 QStringLiteral("Remove request"))) {
        return;
    }
    const OpResult r = m_services.queue.cancelRequest(id.toLongLong());
    if (r.ok)
        m_banner->showSuccess(r.message);
    else
        m_banner->showError(r.message);
    emit dataChanged();
}

void LabQueuePage::onReplay()
{
    m_computers->setProperty("userSet", true);
    std::vector<RequestRecord> records;
    QString error;
    if (!m_services.queue.history(records, &error)) {
        m_replay->hide();
        showErrorState(m_replayState, error);
        return;
    }
    // Finished visits only: they are the ones with a measured burst.
    std::vector<lab::ReplayJob> jobs;
    qint64 origin = -1;
    for (const RequestRecord& r : records) {
        if (r.startTime.isEmpty() || r.endTime.isEmpty())
            continue;
        const qint64 arrival = toMs(r.requestedAt);
        if (origin < 0 || arrival < origin)
            origin = arrival;
    }
    for (const RequestRecord& r : records) {
        if (r.startTime.isEmpty() || r.endTime.isEmpty())
            continue;
        lab::ReplayJob job;
        job.id = r.requestId;
        job.arrivalSeconds = (toMs(r.requestedAt) - origin) / 1000;
        job.burstSeconds = qMax<qint64>(1, (toMs(r.endTime) - toMs(r.startTime)) / 1000);
        job.expectedMinutes = r.expectedMinutes;
        job.priority = r.priority;
        jobs.push_back(job);
    }
    if (jobs.empty()) {
        m_replay->hide();
        showEmptyState(m_replayState, QStringLiteral("There are no finished visits to replay yet."));
        return;
    }
    const lab::Policy policies[] = {lab::Policy::FCFS, lab::Policy::SJF, lab::Policy::Priority};
    m_replay->setRowCount(3);
    try {
        for (int i = 0; i < 3; ++i) {
            const lab::ReplayResult r = lab::replay(policies[i], jobs, m_computers->value());
            m_replay->setItem(i, 0, makeItem(QString::fromStdString(lab::policyName(policies[i]))));
            m_replay->setItem(i, 1, durationItem(r.averageWaitingMinutes));
            m_replay->setItem(i, 2, durationItem(r.averageTurnaroundMinutes));
            m_replay->setItem(i, 3, durationItem(r.maximumWaitingMinutes));
            m_replay->setItem(i, 4, durationItem(r.finishSeconds / 60.0));
        }
    } catch (const std::exception& e) {
        m_replay->hide();
        showErrorState(m_replayState, QString::fromStdString(e.what()));
        return;
    }
    m_replay->show();
    m_replayState->hide();
    m_banner->showInfo(QStringLiteral("Replayed %1 finished visit(s) on %2 computer(s). Lower waiting is better; "
                                      "compare the longest wait to see which policy risks starvation.")
                           .arg(static_cast<int>(jobs.size()))
                           .arg(m_computers->value()));
}

}  // namespace labdesk::ui
