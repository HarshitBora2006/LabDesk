#include "studenthomepage.h"

#include "labqueue.h"
#include "layouthelpers.h"
#include "services.h"
#include "theme.h"
#include "timeutil.h"
#include "uihelpers.h"

#include <QDate>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

namespace labdesk::ui {

StudentHomePage::StudentHomePage(Services& services, const QString& studentId, QWidget* parent)
    : Page(parent), m_services(services), m_studentId(studentId)
{
    QWidget* body = makeScrollPage(this);
    body->setObjectName(QStringLiteral("pageRoot"));
    auto* root = new QVBoxLayout(body);
    root->setContentsMargins(32, 26, 32, 28);
    root->setSpacing(16);
    m_welcome = makeLabel(QString(), QStringLiteral("pageTitle"));
    m_subtitle = makeLabel(QString(), QStringLiteral("pageSubtitle"));
    root->addWidget(m_welcome);
    root->addWidget(m_subtitle);

    QFrame* visit = makeCard();
    cardLayout(visit)->addWidget(makeLabel(QStringLiteral("My current session"), QStringLiteral("sectionTitle")));
    auto* row = new QHBoxLayout();
    auto* text = new QVBoxLayout();
    m_visitTitle = makeLabel(QString(), QStringLiteral("statValue"));
    m_visitDetails = makeLabel(QString(), QStringLiteral("caption"));
    text->addWidget(m_visitTitle);
    text->addWidget(m_visitDetails);
    row->addLayout(text, 1);
    m_visitAction = makeButton(QString(), QStringLiteral("primary"));
    row->addWidget(m_visitAction, 0, Qt::AlignVCenter);
    cardLayout(visit)->addLayout(row);
    root->addWidget(visit);

    QFrame* computers = makeCard();
    cardLayout(computers)->addWidget(makeLabel(QStringLiteral("Computers in the lab"), QStringLiteral("sectionTitle")));
    m_counts = makeLabel(QString(), QStringLiteral("caption"));
    cardLayout(computers)->addWidget(m_counts);
    m_tiles = new QWidget();
    new FlowLayout(m_tiles, 0, 12, 12);
    cardLayout(computers)->addWidget(m_tiles);
    m_tilesState = makeStateMessage();
    cardLayout(computers)->addWidget(m_tilesState);
    root->addWidget(computers);
    root->addStretch(1);

    connect(m_visitAction, &QPushButton::clicked, this, &StudentHomePage::openMySession);
}

void StudentHomePage::refresh()
{
    const auto student = m_services.students.findStudent(m_studentId);
    m_welcome->setText(QStringLiteral("Welcome, %1").arg(student ? student->name : m_studentId));
    m_subtitle->setText(QStringLiteral("Student ID %1, %2. %3")
                            .arg(m_studentId, student ? student->department : QString(),
                                 QLocale().toString(QDate::currentDate(), QLocale::LongFormat)));

    QString error;
    const auto session = m_services.sessions.activeSessionForStudent(m_studentId, &error);
    int queueLength = 0;
    const auto waiting = m_services.queue.waitingEntryFor(m_studentId, &queueLength);
    if (!error.isEmpty()) {
        m_visitTitle->setText(QStringLiteral("Unavailable"));
        m_visitDetails->setText(QStringLiteral("Your session could not be read: ") + error);
        m_visitAction->setText(QStringLiteral("Open My Session"));
    } else if (session) {
        m_visitTitle->setText(session->computerId);
        m_visitDetails->setText(QStringLiteral("%1, %2. Started %3 (%4 ago).")
                                    .arg(session->computerName, session->location, timeutil::toDisplay(session->startTime),
                                         timeutil::durationText(session->startTime)));
        m_visitAction->setText(QStringLiteral("End or view my session"));
    } else if (waiting) {
        m_visitTitle->setText(QStringLiteral("Waiting: number %1 of %2").arg(waiting->position).arg(queueLength));
        m_visitDetails->setText(QStringLiteral("You requested a computer at %1. The next free computer is assigned "
                                               "automatically.")
                                    .arg(timeutil::toDisplay(waiting->requestedAt)));
        m_visitAction->setText(QStringLiteral("View queue position"));
    } else {
        m_visitTitle->setText(QStringLiteral("No active session"));
        m_visitDetails->setText(QStringLiteral("Request a computer to start a session."));
        m_visitAction->setText(QStringLiteral("Request a computer"));
    }

    // Rebuild the computer tiles.
    auto* flow = static_cast<FlowLayout*>(m_tiles->layout());
    while (QLayoutItem* item = flow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    QString listError;
    const auto computers = m_services.computers.listComputers(QString(), &listError);
    int available = 0, inUse = 0, maintenance = 0;
    for (const Computer& c : computers) {
        if (c.status == ComputerStatus::Available)
            ++available;
        else if (c.status == ComputerStatus::Occupied)
            ++inUse;
        else
            ++maintenance;
        const auto colors = theme::statusColors(c.status);
        auto* tile = new QFrame();
        tile->setObjectName(QStringLiteral("card"));
        tile->setFixedSize(168, 92);
        auto* v = new QVBoxLayout(tile);
        v->setContentsMargins(12, 10, 12, 10);
        v->setSpacing(4);
        v->addWidget(makeLabel(c.computerId, QStringLiteral("sectionTitle")));
        v->addWidget(makeLabel(c.location, QStringLiteral("caption")));
        auto* badge = makeStatusChip(c.status, theme::displayStatus(c.status));
        v->addWidget(badge, 0, Qt::AlignLeft);
        tile->setStyleSheet(QStringLiteral("QFrame#card { border-left: 4px solid %1; }").arg(colors.foreground.name()));
        tile->setToolTip(c.name);
        flow->addWidget(tile);
    }
    m_counts->setText(QStringLiteral("%1 available, %2 in use, %3 under maintenance.").arg(available).arg(inUse).arg(maintenance));
    if (!listError.isEmpty()) {
        m_tiles->hide();
        showErrorState(m_tilesState, listError);
    } else if (computers.empty()) {
        m_tiles->hide();
        showEmptyState(m_tilesState, QStringLiteral("No computers have been registered yet."));
    } else {
        m_tiles->show();
        m_tilesState->hide();
    }
}

}  // namespace labdesk::ui
