#include "studentportalpage.h"

#include "services.h"
#include "timeutil.h"
#include "uihelpers.h"

#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
                                        QStringLiteral("Sign in to pick a lab computer or end your session."));
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
    page->setObjectName(QStringLiteral("pageRoot"));
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(28, 22, 28, 22);
    root->setSpacing(14);

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

    QFrame* sessionCard = makeCard();
    QVBoxLayout* session = cardLayout(sessionCard);
    session->addWidget(makeLabel(QStringLiteral("Your session"), QStringLiteral("sectionTitle")));
    auto* sessionRow = new QHBoxLayout();
    auto* sessionText = new QVBoxLayout();
    m_sessionSummary = makeLabel(QString());
    QFont big = m_sessionSummary->font();
    big.setPointSizeF(big.pointSizeF() * 1.25);
    big.setBold(true);
    m_sessionSummary->setFont(big);
    m_sessionDetails = makeLabel(QString(), QStringLiteral("muted"));
    m_elapsed = makeLabel(QString(), QStringLiteral("muted"));
    sessionText->addWidget(m_sessionSummary);
    sessionText->addWidget(m_sessionDetails);
    sessionText->addWidget(m_elapsed);
    sessionRow->addLayout(sessionText, 1);
    m_endSession = makeButton(QStringLiteral("End session"), QStringLiteral("primary"));
    sessionRow->addWidget(m_endSession, 0, Qt::AlignVCenter);
    session->addLayout(sessionRow);
    root->addWidget(sessionCard);

    QFrame* availableCard = makeCard();
    QVBoxLayout* available = cardLayout(availableCard);
    auto* availableHeader = new QHBoxLayout();
    availableHeader->addWidget(makeLabel(QStringLiteral("Available computers"), QStringLiteral("sectionTitle")), 1);
    m_useComputer = makeButton(QStringLiteral("Use selected computer"), QStringLiteral("primary"));
    availableHeader->addWidget(m_useComputer);
    available->addLayout(availableHeader);
    m_available = new QTableWidget();
    setupTable(m_available, {QStringLiteral("Computer ID"), QStringLiteral("Name"), QStringLiteral("Location"),
                             QStringLiteral("Status")});
    available->addWidget(m_available, 1);
    m_noneAvailable = makeStateMessage();
    available->addWidget(m_noneAvailable);
    root->addWidget(availableCard, 1);

    connect(signOut, &QPushButton::clicked, this, &StudentPortalPage::onSignOut);
    connect(m_endSession, &QPushButton::clicked, this, &StudentPortalPage::onEndSession);
    connect(m_useComputer, &QPushButton::clicked, this, &StudentPortalPage::onUseComputer);
    connect(m_available, &QTableWidget::itemSelectionChanged, this, &StudentPortalPage::updateUseButton);
    connect(m_available, &QTableWidget::itemDoubleClicked, this, [this]() { onUseComputer(); });
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
    m_sessionStart.clear();
    m_clock->stop();
    m_loginId->clear();
    m_stack->setCurrentIndex(0);
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

    QString error;
    const auto session = m_services.sessions.activeSessionForStudent(m_studentId, &error);
    if (!error.isEmpty()) {
        m_sessionStart.clear();
        m_sessionSummary->setText(QStringLiteral("Session status unavailable"));
        m_sessionDetails->setText(QStringLiteral("Your session could not be read from the database: ") + error);
        m_endSession->setVisible(false);
        m_clock->stop();
    } else if (session) {
        m_sessionStart = session->startTime;
        m_sessionSummary->setText(QStringLiteral("You are using %1").arg(session->computerId));
        m_sessionDetails->setText(QStringLiteral("%1, %2. Started %3.")
                                      .arg(session->computerName, session->location,
                                           timeutil::toDisplay(session->startTime)));
        m_endSession->setVisible(true);
        m_clock->start();
    } else {
        m_sessionStart.clear();
        m_sessionSummary->setText(QStringLiteral("No active computer session."));
        m_sessionDetails->setText(QStringLiteral("Choose an available computer below to start a session."));
        m_endSession->setVisible(false);
        m_clock->stop();
    }
    updateElapsed();

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
        showEmptyState(m_noneAvailable, QStringLiteral("No computers available for allocation. Every computer is "
                                                       "in use or under maintenance; check back in a few minutes."));
    } else {
        m_available->show();
        m_noneAvailable->hide();
    }
    updateUseButton();
}

void StudentPortalPage::updateElapsed()
{
    m_elapsed->setVisible(!m_sessionStart.isEmpty());
    if (!m_sessionStart.isEmpty())
        m_elapsed->setText(QStringLiteral("Elapsed: ") + timeutil::durationText(m_sessionStart));
}

void StudentPortalPage::updateUseButton()
{
    const bool hasSession = !m_sessionStart.isEmpty();
    m_useComputer->setEnabled(!hasSession && selectedRowData(m_available).isValid());
    m_useComputer->setToolTip(hasSession ? QStringLiteral("End your current session before choosing another computer.")
                                         : QString());
}

void StudentPortalPage::onUseComputer()
{
    if (m_studentId.isEmpty())
        return;
    const QString computerId = selectedRowData(m_available).toString();
    if (computerId.isEmpty()) {
        m_banner->showError(QStringLiteral("Select a computer first."));
        return;
    }
    // The service re-checks availability and the one-session rule inside its
    // transaction; the table above may already be out of date.
    const OpResult r = m_services.sessions.allocateComputer(m_studentId, computerId);
    if (r.ok)
        m_banner->showSuccess(QStringLiteral("You are now using %1. Remember to end your session when you leave.")
                                  .arg(computerId));
    else
        m_banner->showError(QStringLiteral("Could not start the session: ") + r.message);
    emit dataChanged();  // refresh this page and every admin view
}

void StudentPortalPage::onEndSession()
{
    if (m_studentId.isEmpty())
        return;
    if (!confirm(this, QStringLiteral("End session"),
                 QStringLiteral("End your session now? The computer will be released for other students."),
                 QStringLiteral("End session"))) {
        return;
    }
    const OpResult r = m_services.sessions.endActiveSession(m_studentId);
    if (r.ok)
        m_banner->showSuccess(QStringLiteral("Session ended. Thank you for releasing the computer."));
    else
        m_banner->showError(QStringLiteral("Could not end the session: ") + r.message);
    emit dataChanged();
}

}  // namespace labdesk::ui
