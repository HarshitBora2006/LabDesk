#include "sessionspage.h"

#include "database.h"
#include "services.h"
#include "timeutil.h"
#include "uihelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <memory>

namespace labdesk::ui {

SessionsPage::SessionsPage(Database& db, Services& services, QWidget* parent)
    : Page(parent), m_db(db), m_services(services)
{
    QVBoxLayout* root = makePageLayout(
        this, QStringLiteral("Sessions and transactions"),
        QStringLiteral("Every allocation and every session end is a single database transaction."));
    m_banner = new MessageBanner();
    root->addWidget(m_banner);

    auto* tabs = new QTabWidget();
    tabs->addTab(buildHistoryTab(), QStringLiteral("Session history"));
    tabs->addTab(buildTransactionTab(), QStringLiteral("Transaction demonstration"));
    root->addWidget(tabs, 1);
}

QWidget* SessionsPage::buildHistoryTab()
{
    auto* tab = new QWidget();
    tab->setObjectName(QStringLiteral("tabPage"));
    tab->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);
    auto* header = new QHBoxLayout();
    m_activeOnly = new QCheckBox(QStringLiteral("Active sessions only"));
    header->addWidget(m_activeOnly);
    header->addStretch();
    m_endSelected = makeButton(QStringLiteral("End selected session"), QStringLiteral("danger"));
    header->addWidget(m_endSelected);
    layout->addLayout(header);

    m_table = new QTableWidget();
    setupTable(m_table, {QStringLiteral("Session"), QStringLiteral("Student"), QStringLiteral("Computer"),
                         QStringLiteral("Location"), QStringLiteral("Started"), QStringLiteral("Ended"),
                         QStringLiteral("Duration"), QStringLiteral("Status")});
    layout->addWidget(m_table, 1);
    m_empty = makeStateMessage();
    layout->addWidget(m_empty);
    layout->addWidget(makeLabel(QStringLiteral("Ended sessions are kept permanently; the database rejects any "
                                               "attempt to modify or delete them."),
                                QStringLiteral("muted")));

    connect(m_activeOnly, &QCheckBox::toggled, this, [this]() { reloadHistory(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        bool active = false;
        const auto items = m_table->selectedItems();
        if (!items.isEmpty()) {
            const QTableWidgetItem* status = m_table->item(items.first()->row(), 7);
            active = status && status->text() == SessionStatus::Active;
        }
        m_endSelected->setEnabled(active);
    });
    connect(m_endSelected, &QPushButton::clicked, this, &SessionsPage::onEndSelected);
    m_endSelected->setEnabled(false);
    return tab;
}

QWidget* SessionsPage::buildTransactionTab()
{
    auto* tab = new QWidget();
    tab->setObjectName(QStringLiteral("tabPage"));
    tab->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);
    layout->addWidget(makeLabel(
        QStringLiteral("Runs the same SessionService code the student portal uses and prints each step. "
                       "Bound values are shown inline for readability; the real statements use prepared "
                       "placeholders (?). Turn on the injected failure to make the session INSERT fail after "
                       "the computer UPDATE has already run, then check that ROLLBACK left the computer "
                       "AVAILABLE."),
        QStringLiteral("muted")));

    auto* controls = new QHBoxLayout();
    m_demoStudent = new QComboBox();
    m_demoStudent->setMinimumWidth(220);
    m_demoComputer = new QComboBox();
    m_demoComputer->setMinimumWidth(220);
    controls->addWidget(makeLabel(QStringLiteral("Student")));
    controls->addWidget(m_demoStudent);
    controls->addSpacing(12);
    controls->addWidget(makeLabel(QStringLiteral("Computer")));
    controls->addWidget(m_demoComputer);
    controls->addStretch();
    layout->addLayout(controls);

    m_injectFailure = new QCheckBox(
        QStringLiteral("Inject a failure after the computer UPDATE (the session INSERT will fail)"));
    layout->addWidget(m_injectFailure);

    auto* buttons = new QHBoxLayout();
    auto* allocate = makeButton(QStringLiteral("Run allocation transaction"), QStringLiteral("primary"));
    auto* endSession = makeButton(QStringLiteral("Run end-session transaction"));
    auto* clearLog = makeButton(QStringLiteral("Clear log"));
    buttons->addWidget(allocate);
    buttons->addWidget(endSession);
    buttons->addStretch();
    buttons->addWidget(clearLog);
    layout->addLayout(buttons);

    m_log = new QPlainTextEdit();
    m_log->setReadOnly(true);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setPlaceholderText(QStringLiteral("Transaction steps appear here."));
    layout->addWidget(m_log, 1);

    connect(allocate, &QPushButton::clicked, this, &SessionsPage::runAllocation);
    connect(endSession, &QPushButton::clicked, this, &SessionsPage::runEndSession);
    connect(clearLog, &QPushButton::clicked, m_log, &QPlainTextEdit::clear);
    return tab;
}

void SessionsPage::refresh()
{
    reloadHistory();
    reloadDemoChoices();
}

void SessionsPage::reloadHistory()
{
    QString error;
    const auto sessions = m_services.sessions.listSessions(m_activeOnly->isChecked(), 500, &error);
    m_table->setRowCount(static_cast<int>(sessions.size()));
    for (int row = 0; row < static_cast<int>(sessions.size()); ++row) {
        const SessionRecord& s = sessions[static_cast<std::size_t>(row)];
        m_table->setItem(row, 0, makeItem(QStringLiteral("#%1").arg(s.sessionId), s.sessionId));
        m_table->setItem(row, 1, makeItem(QStringLiteral("%1 (%2)").arg(s.studentName, s.studentId)));
        m_table->setItem(row, 2, makeItem(s.computerId));
        m_table->setItem(row, 3, makeItem(s.location));
        m_table->setItem(row, 4, makeItem(timeutil::toDisplay(s.startTime)));
        m_table->setItem(row, 5, makeItem(timeutil::toDisplay(s.endTime)));
        m_table->setItem(row, 6, makeItem(timeutil::durationText(s.startTime, s.endTime)));
        m_table->setItem(row, 7, makeStatusItem(s.status));
    }
    m_table->resizeColumnsToContents();
    if (!error.isEmpty()) {
        m_table->hide();
        showErrorState(m_empty, error);
    } else if (sessions.empty()) {
        m_table->hide();
        showEmptyState(m_empty, m_activeOnly->isChecked()
                                    ? QStringLiteral("No active computer sessions right now.")
                                    : QStringLiteral("No sessions recorded yet. Students start one from the "
                                                     "Student portal."));
    } else {
        m_table->show();
        m_empty->hide();
    }
    m_endSelected->setEnabled(false);
}

void SessionsPage::reloadDemoChoices()
{
    const QString student = m_demoStudent->currentData().toString();
    const QString computer = m_demoComputer->currentData().toString();
    m_demoStudent->clear();
    for (const Student& s : m_services.students.listStudents())
        m_demoStudent->addItem(QStringLiteral("%1 (%2)").arg(s.studentId, s.name), s.studentId);
    m_demoComputer->clear();
    // All computers are offered on purpose, so a failing pre-check can be demonstrated too.
    for (const Computer& c : m_services.computers.listComputers())
        m_demoComputer->addItem(QStringLiteral("%1 (%2)").arg(c.computerId, c.status), c.computerId);
    const int si = m_demoStudent->findData(student);
    if (si >= 0)
        m_demoStudent->setCurrentIndex(si);
    const int ci = m_demoComputer->findData(computer);
    if (ci >= 0)
        m_demoComputer->setCurrentIndex(ci);
}

QString SessionsPage::describeState(const QString& studentId, const QString& computerId) const
{
    QString computerState = QStringLiteral("not found");
    if (const auto c = m_services.computers.findComputer(computerId))
        computerState = c->status;
    QString sessionState = QStringLiteral("none");
    if (const auto s = m_services.sessions.activeSessionForStudent(studentId))
        sessionState = QStringLiteral("#%1 on %2").arg(s->sessionId).arg(s->computerId);
    return QStringLiteral("computer %1 = %2 | %3 active session = %4")
        .arg(computerId, computerState, studentId, sessionState);
}

void SessionsPage::runAllocation()
{
    const QString studentId = m_demoStudent->currentData().toString();
    const QString computerId = m_demoComputer->currentData().toString();
    if (studentId.isEmpty() || computerId.isEmpty()) {
        m_banner->showError(QStringLiteral("Add at least one student and one computer first."));
        return;
    }
    m_log->appendPlainText(QStringLiteral("=== Allocate %1 to %2%3 ===")
                               .arg(computerId, studentId,
                                    m_injectFailure->isChecked() ? QStringLiteral(" (failure injected)")
                                                                 : QString()));
    m_log->appendPlainText(QStringLiteral("before: ") + describeState(studentId, computerId));

    std::unique_ptr<FaultInjector> fault;
    if (m_injectFailure->isChecked()) {
        fault = std::make_unique<FaultInjector>(m_db.connection());
        if (!fault->isInstalled()) {
            m_banner->showError(QStringLiteral("Could not install the fault trigger: ") + fault->lastError());
            return;
        }
    }
    const OpResult r = m_services.sessions.allocateComputer(
        studentId, computerId, [this](const QString& line) { m_log->appendPlainText(line); });
    fault.reset();  // drop the TEMP trigger before anything else touches the database

    m_log->appendPlainText(QStringLiteral("after:  ") + describeState(studentId, computerId));
    m_log->appendPlainText(QString());
    if (r.ok)
        m_banner->showSuccess(r.message);
    else
        m_banner->showError(QStringLiteral("Transaction rolled back: ") + r.message);
    emit dataChanged();
}

void SessionsPage::runEndSession()
{
    const QString studentId = m_demoStudent->currentData().toString();
    if (studentId.isEmpty()) {
        m_banner->showError(QStringLiteral("Choose a student."));
        return;
    }
    m_log->appendPlainText(QStringLiteral("=== End active session of %1 ===").arg(studentId));
    const OpResult r = m_services.sessions.endActiveSession(
        studentId, [this](const QString& line) { m_log->appendPlainText(line); });
    m_log->appendPlainText(QString());
    if (r.ok)
        m_banner->showSuccess(r.message);
    else
        m_banner->showError(QStringLiteral("Transaction rolled back: ") + r.message);
    emit dataChanged();
}

void SessionsPage::onEndSelected()
{
    const QVariant id = selectedRowData(m_table);
    if (!id.isValid())
        return;
    if (!confirm(this, QStringLiteral("End session"),
                 QStringLiteral("End session #%1 on behalf of the student? The computer becomes AVAILABLE.")
                     .arg(id.toLongLong()),
                 QStringLiteral("End session"))) {
        return;
    }
    const OpResult r = m_services.sessions.endSessionById(id.toLongLong());
    if (!r.ok) {
        m_banner->showError(r.message);
        return;
    }
    m_banner->showSuccess(r.message);
    emit dataChanged();
}

}  // namespace labdesk::ui
