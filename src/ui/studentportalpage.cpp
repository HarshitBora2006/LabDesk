#include "studentportalpage.h"

#include "labqueue.h"
#include "services.h"
#include "timeutil.h"
#include "uihelpers.h"

#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace labdesk::ui {

StudentPortalPage::StudentPortalPage(Services& services, QWidget* parent) : Page(parent), m_services(services)
{
    setObjectName(QStringLiteral("pageRoot"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget();
    m_stack->setObjectName(QStringLiteral("pageStack"));
    m_stack->addWidget(buildLogin());
    m_stack->addWidget(buildDashboard());
    root->addWidget(m_stack);

    m_clock = new QTimer(this);
    m_clock->setInterval(1000);
    connect(m_clock, &QTimer::timeout, this, &StudentPortalPage::updateElapsed);
}

QWidget* StudentPortalPage::buildLogin()
{
    auto* page = new QWidget();
    QVBoxLayout* outer = makePageLayout(page, QStringLiteral("Student portal"),
                                        QStringLiteral("Sign in to request a lab computer or end your session."));
    auto* centre = new QHBoxLayout();
    centre->addStretch();
    QFrame* card = makeCard();
    card->setFixedWidth(380);
    QVBoxLayout* box = cardLayout(card);
    box->setSpacing(12);
    box->addWidget(makeLabel(QStringLiteral("Sign in"), QStringLiteral("sectionTitle")));
    m_loginBanner = new MessageBanner();
    box->addWidget(m_loginBanner);
    auto* form = new QFormLayout();
    form->setVerticalSpacing(10);
    m_loginId = new QLineEdit();
    m_loginId->setPlaceholderText(QStringLiteral("Your student ID"));
    m_loginPassword = new QLineEdit();
    m_loginPassword->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Student ID"), m_loginId);
    form->addRow(QStringLiteral("Password"), m_loginPassword);
    box->addLayout(form);
    auto* signIn = makeButton(QStringLiteral("Sign in"), QStringLiteral("primary"));
    box->addWidget(signIn);
    box->addWidget(makeLabel(QStringLiteral("Demo accounts: DEMO01 to DEMO04, password demo123."),
                             QStringLiteral("muted")));
    centre->addWidget(card);
    centre->addStretch();
    outer->addSpacing(30);
    outer->addLayout(centre);
    outer->addStretch();

    connect(signIn, &QPushButton::clicked, this, &StudentPortalPage::onSignIn);
    connect(m_loginPassword, &QLineEdit::returnPressed, this, &StudentPortalPage::onSignIn);
    connect(m_loginId, &QLineEdit::returnPressed, this, [this]() { m_loginPassword->setFocus(); });
    return page;
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
    auto* signOut = makeButton(QStringLiteral("Sign out"));
    header->addWidget(signOut, 0, Qt::AlignTop);
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
    bodyLayout->addWidget(makeLabel(QStringLiteral("Your purpose sets your priority if the lab is full (exam first, "
                                                   "then assignment, then practice). Your expected length is used by "
                                                   "the shortest-job-first policy. Your real waiting time and session "
                                                   "length are recorded automatically."),
                                    QStringLiteral("caption")));

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

    connect(signOut, &QPushButton::clicked, this, &StudentPortalPage::onSignOut);
    connect(m_endSession, &QPushButton::clicked, this, &StudentPortalPage::onEndSession);
    connect(m_leaveQueue, &QPushButton::clicked, this, &StudentPortalPage::onLeaveQueue);
    connect(m_startNow, &QPushButton::clicked, this, &StudentPortalPage::onStartNow);
    connect(m_joinQueue, &QPushButton::clicked, this, &StudentPortalPage::onJoinQueue);
    connect(m_available, &QTableWidget::itemSelectionChanged, this, &StudentPortalPage::updateRequestControls);
    connect(m_available, &QTableWidget::itemDoubleClicked, this, [this]() { onStartNow(); });
    return page;
}

void StudentPortalPage::onSignIn()
{
    QString error;
    const auto student = m_services.students.authenticate(m_loginId->text(), m_loginPassword->text(), &error);
    m_loginPassword->clear();
    if (!student) {
        m_loginBanner->showError(error);
        return;
    }
    m_loginBanner->clearMessage();
    m_studentId = student->studentId;
    m_banner->clearMessage();
    m_stack->setCurrentIndex(1);
    refresh();
}

void StudentPortalPage::onSignOut()
{
    m_studentId.clear();
    m_timerStart.clear();
    m_clock->stop();
    m_loginId->clear();
    m_stack->setCurrentIndex(0);
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
        onSignOut();
        m_loginBanner->showError(QStringLiteral("Your account is no longer available. Contact the lab admin."));
        return;
    }
    m_welcome->setText(QStringLiteral("Hello, %1").arg(student->name));
    m_identity->setText(QStringLiteral("Student ID %1, %2").arg(student->studentId, student->department));

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
        m_visitDetails->setText(QStringLiteral("%1, %2. Started %3. Your session length is measured when you press "
                                               "End session.")
                                    .arg(session->computerName, session->location,
                                         timeutil::toDisplay(session->startTime)));
    } else if (m_waiting) {
        m_timerStart = entry->requestedAt;
        m_timerPrefix = QStringLiteral("Waiting for");
        m_visitSummary->setText(QStringLiteral("You are number %1 of %2 in the queue").arg(entry->position).arg(m_queueLength));
        m_visitDetails->setText(QStringLiteral("Requested %1 for %2 (priority %3), expected %4 min. Policy: %5. The next "
                                               "free computer is assigned automatically; this page updates when it "
                                               "happens.")
                                    .arg(timeutil::toDisplay(entry->requestedAt), entry->purpose.toLower())
                                    .arg(entry->priority)
                                    .arg(entry->expectedMinutes)
                                    .arg(policyName));
    } else {
        m_timerStart.clear();
        m_visitSummary->setText(QStringLiteral("No active computer session."));
        m_visitDetails->setText(QStringLiteral("Request a computer below. If none is free you can join the queue."));
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
        showEmptyState(m_noneAvailable, QStringLiteral("No computers available for allocation. Join the waiting queue "
                                                       "and the next free computer will be assigned to you under the "
                                                       "%1 policy.")
                                            .arg(policyName));
    } else if (m_queueLength > 0 && !m_waiting) {
        m_available->hide();
        showEmptyState(m_noneAvailable, QStringLiteral("%1 student(s) are already waiting, so free computers are "
                                                       "assigned from the queue. Join the queue to take your turn.")
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
        m_requestHint->setText(QStringLiteral("You already have a computer. End your session before requesting "
                                              "another one."));
    else if (m_waiting)
        m_requestHint->setText(QStringLiteral("You are in the queue. Leave the queue if you no longer need a computer."));
    else
        m_requestHint->setText(QStringLiteral("Tell the lab why you need a computer and for about how long."));

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
        m_banner->showSuccess(QStringLiteral("Session ended. Your actual session length has been recorded. Thank "
                                             "you for releasing the computer."));
    else
        m_banner->showError(QStringLiteral("Could not end the session: ") + r.message);
    emit dataChanged();
}

}  // namespace labdesk::ui
