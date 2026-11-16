#include "studentportalpage.h"

#include "labqueue.h"
#include "services.h"
#include "timeutil.h"
#include "uihelpers.h"

#include <QComboBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace labdesk::ui {

StudentPortalPage::StudentPortalPage(Services& services, const QString& studentId, QWidget* parent)
    : Page(parent), m_services(services), m_studentId(studentId)
{
    setObjectName(QStringLiteral("pageRoot"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(buildDashboard());

    m_clock = new QTimer(this);
    m_clock->setInterval(1000);
    connect(m_clock, &QTimer::timeout, this, &StudentPortalPage::updateElapsed);
}

QWidget* StudentPortalPage::buildDashboard()
{
    auto* page = new QWidget();
    QWidget* body = makeScrollPage(page);
    auto* root = new QVBoxLayout(body);
    root->setContentsMargins(32, 26, 32, 28);
    root->setSpacing(16);

    auto* header = new QHBoxLayout();
    auto* titles = new QVBoxLayout();
    titles->setSpacing(2);
    m_welcome = makeLabel(QString(), QStringLiteral("pageTitle"));
    m_identity = makeLabel(QString(), QStringLiteral("pageSubtitle"));
    titles->addWidget(m_welcome);
    titles->addWidget(m_identity);
    header->addLayout(titles, 1);
    root->addLayout(header);

    m_banner = new MessageBanner();
    root->addWidget(m_banner);

    // ---- current visit
    QFrame* visitCard = makeCard();
    QVBoxLayout* visit = cardLayout(visitCard);
    visit->addWidget(makeLabel(QStringLiteral("Your visit"), QStringLiteral("sectionTitle")));
    auto* visitRow = new QHBoxLayout();
    auto* visitText = new QVBoxLayout();
    m_visitSummary = makeLabel(QString());
    QFont big = m_visitSummary->font();
    big.setPointSizeF(big.pointSizeF() * 1.25);
    big.setBold(true);
    m_visitSummary->setFont(big);
    m_visitDetails = makeLabel(QString(), QStringLiteral("muted"));
    m_elapsed = makeLabel(QString(), QStringLiteral("muted"));
    visitText->addWidget(m_visitSummary);
    visitText->addWidget(m_visitDetails);
    visitText->addWidget(m_elapsed);
    visitRow->addLayout(visitText, 1);
    m_leaveQueue = makeButton(QStringLiteral("Leave queue"), QStringLiteral("danger"));
    m_endSession = makeButton(QStringLiteral("End session"), QStringLiteral("primary"));
    visitRow->addWidget(m_leaveQueue, 0, Qt::AlignVCenter);
    visitRow->addWidget(m_endSession, 0, Qt::AlignVCenter);
    visit->addLayout(visitRow);
    root->addWidget(visitCard);

    // ---- request a computer
    QFrame* requestCard = makeCard();
    QVBoxLayout* request = cardLayout(requestCard);
    request->addWidget(makeLabel(QStringLiteral("Request a computer"), QStringLiteral("sectionTitle")));
    m_requestHint = makeLabel(QString(), QStringLiteral("caption"));
    request->addWidget(m_requestHint);

    m_requestBody = new QWidget();
    auto* bodyLayout = new QVBoxLayout(m_requestBody);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(10);
    auto* fields = new QHBoxLayout();
    fields->setSpacing(10);
    fields->addWidget(makeLabel(QStringLiteral("Purpose"), QStringLiteral("fieldLabel")));
    m_purpose = new QComboBox();
    for (lab::Purpose p : {lab::Purpose::Exam, lab::Purpose::Assignment, lab::Purpose::Practice}) {
        m_purpose->addItem(QStringLiteral("%1 (priority %2)")
                               .arg(QString::fromStdString(lab::purposeName(p)))
                               .arg(lab::priorityFor(p)),
                           QString::fromLatin1(lab::purposeCode(p)));
    }
    m_purpose->setCurrentIndex(1);
    fields->addWidget(m_purpose);
    fields->addSpacing(12);
    fields->addWidget(makeLabel(QStringLiteral("Expected length"), QStringLiteral("fieldLabel")));
    m_expected = new QSpinBox();
    m_expected->setRange(lab::kMinExpectedMinutes, lab::kMaxExpectedMinutes);
    m_expected->setSingleStep(5);
    m_expected->setValue(60);
    m_expected->setSuffix(QStringLiteral(" min"));
    fields->addWidget(m_expected);
    fields->addStretch(1);
    bodyLayout->addLayout(fields);

    m_available = new QTableWidget();
    setupTable(m_available, {QStringLiteral("Computer ID"), QStringLiteral("Name"), QStringLiteral("Location"),
                             QStringLiteral("Status")});
    m_available->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_available->setMinimumHeight(200);
    bodyLayout->addWidget(m_available);
    m_noneAvailable = makeStateMessage();
    bodyLayout->addWidget(m_noneAvailable);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    m_joinQueue = makeButton(QStringLiteral("Join the waiting queue"), QStringLiteral("primary"));
    m_startNow = makeButton(QStringLiteral("Start session on selected computer"), QStringLiteral("primary"));
    buttons->addWidget(m_joinQueue);
    buttons->addWidget(m_startNow);
    bodyLayout->addLayout(buttons);
    request->addWidget(m_requestBody);
    root->addWidget(requestCard);
    root->addStretch(1);

    connect(m_endSession, &QPushButton::clicked, this, &StudentPortalPage::onEndSession);
    connect(m_leaveQueue, &QPushButton::clicked, this, &StudentPortalPage::onLeaveQueue);
    connect(m_startNow, &QPushButton::clicked, this, &StudentPortalPage::onStartNow);
    connect(m_joinQueue, &QPushButton::clicked, this, &StudentPortalPage::onJoinQueue);
    connect(m_available, &QTableWidget::itemSelectionChanged, this, &StudentPortalPage::updateRequestControls);
    connect(m_available, &QTableWidget::itemDoubleClicked, this, [this]() { onStartNow(); });
    return page;
}

QString StudentPortalPage::selectedPurpose() const
{
    return m_purpose->currentData().toString();
}

void StudentPortalPage::refresh()
{
    if (m_studentId.isEmpty())
        return;  // not signed in: nothing to show and nothing to allow

    const auto student = m_services.students.findStudent(m_studentId);
    if (!student) {
        m_banner->showError(QStringLiteral("Your account is no longer available. Contact the lab administrator."));
        m_requestBody->setEnabled(false);
        return;
    }
    m_welcome->setText(QStringLiteral("My session"));
    m_identity->setText(QStringLiteral("%1, student ID %2, %3").arg(student->name, student->studentId, student->department));

    // ---- current visit: in a session, waiting in the queue, or neither
    QString sessionError;
    const auto session = m_services.sessions.activeSessionForStudent(m_studentId, &sessionError);
    QString queueError;
    m_queueLength = 0;
    const auto entry = m_services.queue.waitingEntryFor(m_studentId, &m_queueLength, &queueError);
    const QString policyName = QString::fromStdString(lab::policyName(m_services.queue.policy()));
    m_inSession = sessionError.isEmpty() && session.has_value();
    m_waiting = queueError.isEmpty() && entry.has_value();

    if (!sessionError.isEmpty() || !queueError.isEmpty()) {
        m_timerStart.clear();
        m_visitSummary->setText(QStringLiteral("Visit status unavailable"));
        m_visitDetails->setText(QStringLiteral("Your session or queue entry could not be read from the database: ")
                                + (sessionError.isEmpty() ? queueError : sessionError));
    } else if (m_inSession) {
        m_timerStart = session->startTime;
        m_timerPrefix = QStringLiteral("Time at the computer");
        m_visitSummary->setText(QStringLiteral("You are using %1").arg(session->computerId));
        m_visitDetails->setText(QStringLiteral("%1, %2. Started %3.")
                                    .arg(session->computerName, session->location,
                                         timeutil::toDisplay(session->startTime)));
    } else if (m_waiting) {
        m_timerStart = entry->requestedAt;
        m_timerPrefix = QStringLiteral("Waiting for");
        m_visitSummary->setText(QStringLiteral("You are number %1 of %2 in the queue").arg(entry->position).arg(m_queueLength));
        m_visitDetails->setText(QStringLiteral("Requested %1 for %2, expected %4 min. Priority %3, policy %5.")
                                    .arg(timeutil::toDisplay(entry->requestedAt), entry->purpose.toLower())
                                    .arg(entry->priority)
                                    .arg(entry->expectedMinutes)
                                    .arg(policyName));
    } else {
        m_timerStart.clear();
        m_visitSummary->setText(QStringLiteral("No active computer session."));
        m_visitDetails->setText(QStringLiteral("Request a computer below."));
    }
    m_endSession->setVisible(m_inSession);
    m_leaveQueue->setVisible(m_waiting);
    if (m_timerStart.isEmpty())
        m_clock->stop();
    else
        m_clock->start();
    updateElapsed();

    // ---- free computers
    QString listError;
    const auto computers = m_services.computers.listComputers(ComputerStatus::Available, &listError);
    const QString keep = selectedRowData(m_available).toString();
    m_available->blockSignals(true);
    m_available->setRowCount(static_cast<int>(computers.size()));
    for (int row = 0; row < static_cast<int>(computers.size()); ++row) {
        const Computer& c = computers[static_cast<std::size_t>(row)];
        m_available->setItem(row, 0, makeItem(c.computerId, c.computerId));
        m_available->setItem(row, 1, makeItem(c.name));
        m_available->setItem(row, 2, makeItem(c.location));
        m_available->setItem(row, 3, makeStatusItem(c.status));
        if (c.computerId == keep)
            m_available->selectRow(row);
    }
    m_available->resizeColumnsToContents();
    m_available->blockSignals(false);
    if (!listError.isEmpty()) {
        m_available->hide();
        showErrorState(m_noneAvailable, listError);
    } else if (computers.empty()) {
        m_available->hide();
        showEmptyState(m_noneAvailable, QStringLiteral("No computers available. Join the queue (%1 policy).")
                                            .arg(policyName));
    } else if (m_queueLength > 0 && !m_waiting) {
        m_available->hide();
        showEmptyState(m_noneAvailable, QStringLiteral("%1 student(s) are waiting. Join the queue.")
                                            .arg(m_queueLength));
    } else {
        m_available->show();
        m_noneAvailable->hide();
    }
    updateRequestControls();
}

void StudentPortalPage::updateRequestControls()
{
    const bool busy = m_inSession || m_waiting;
    m_requestBody->setEnabled(!busy);
    if (m_inSession)
        m_requestHint->setText(QStringLiteral("You already have a computer."));
    else if (m_waiting)
        m_requestHint->setText(QStringLiteral("You are in the queue."));
    else
        m_requestHint->setText(QStringLiteral("Choose a purpose and expected length."));

    // isHidden(): explicit hide() only, so this also works while the page is off-screen.
    const bool tableUsable = !m_available->isHidden() && m_available->rowCount() > 0;
    m_startNow->setVisible(tableUsable);
    m_startNow->setEnabled(!busy && selectedRowData(m_available).isValid());
    m_joinQueue->setVisible(!tableUsable && !m_waiting);
    m_joinQueue->setEnabled(!busy);
}

void StudentPortalPage::updateElapsed()
{
    m_elapsed->setVisible(!m_timerStart.isEmpty());
    if (!m_timerStart.isEmpty())
        m_elapsed->setText(QStringLiteral("%1: %2").arg(m_timerPrefix, timeutil::durationText(m_timerStart)));
}

void StudentPortalPage::onStartNow()
{
    if (m_studentId.isEmpty() || m_inSession || m_waiting)
        return;
    const QString computerId = selectedRowData(m_available).toString();
    if (computerId.isEmpty()) {
        m_banner->showWarning(QStringLiteral("Select a computer first."));
        return;
    }
    // The service re-checks availability, the queue and the one-session rule
    // inside its transaction; the table may already be out of date.
    const OpResult r = m_services.queue.startNow(m_studentId, computerId, selectedPurpose(), m_expected->value());
    if (r.ok)
        m_banner->showSuccess(r.message);
    else
        m_banner->showError(QStringLiteral("Could not start the session: ") + r.message);
    emit dataChanged();  // refresh this page and every admin view
}

void StudentPortalPage::onJoinQueue()
{
    if (m_studentId.isEmpty() || m_inSession || m_waiting)
        return;
    const OpResult r = m_services.queue.joinQueue(m_studentId, selectedPurpose(), m_expected->value());
    if (r.ok)
        m_banner->showSuccess(r.message);
    else
        m_banner->showError(QStringLiteral("Could not join the queue: ") + r.message);
    emit dataChanged();
}

void StudentPortalPage::onLeaveQueue()
{
    if (m_studentId.isEmpty())
        return;
    if (!confirm(this, QStringLiteral("Leave queue"),
                 QStringLiteral("Leave the queue? If you need a computer later you will join at the back."),
                 QStringLiteral("Leave queue"))) {
        return;
    }
    const OpResult r = m_services.queue.cancelWaiting(m_studentId);
    if (r.ok)
        m_banner->showInfo(r.message);
    else
        m_banner->showError(r.message);
    emit dataChanged();
}

void StudentPortalPage::onEndSession()
{
    if (m_studentId.isEmpty())
        return;
    if (!confirm(this, QStringLiteral("End session"),
                 QStringLiteral("End your session now? The computer will be released for the next student."),
                 QStringLiteral("End session"))) {
        return;
    }
    const OpResult r = m_services.sessions.endActiveSession(m_studentId);
    if (r.ok)
        m_banner->showSuccess(QStringLiteral("Session ended."));
    else
        m_banner->showError(QStringLiteral("Could not end the session: ") + r.message);
    emit dataChanged();
}

}  // namespace labdesk::ui
