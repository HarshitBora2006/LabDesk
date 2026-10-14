#include "dashboardpage.h"

#include "layouthelpers.h"
#include "services.h"
#include "theme.h"
#include "timeutil.h"
#include "uihelpers.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace labdesk::ui {

namespace {
QString percent(int part, int whole)
{
    if (whole <= 0)
        return QStringLiteral("0 %");
    return QStringLiteral("%1 %").arg(qRound(100.0 * part / whole));
}
}  // namespace

DashboardPage::DashboardPage(Services& services, QWidget* parent) : Page(parent), m_services(services)
{
    QWidget* body = makeScrollPage(this);
    QVBoxLayout* root = makePageLayout(
        body, QStringLiteral("Dashboard"),
        QStringLiteral("Live figures read from the database each time this page is shown or data changes."));

    m_banner = new MessageBanner();
    root->addWidget(m_banner);

    // Headline figures: equal-width cards that reflow with the window width.
    m_stats = new ResponsiveGrid(165, 14);
    m_stats->setWidgets({makeStat(QStringLiteral("labs"), QStringLiteral("Laboratories")),
                         makeStat(QStringLiteral("total"), QStringLiteral("Total computers")),
                         makeStat(QStringLiteral("available"), QStringLiteral("Available")),
                         makeStat(QStringLiteral("occupied"), QStringLiteral("Occupied")),
                         makeStat(QStringLiteral("maintenance"), QStringLiteral("Under maintenance")),
                         makeStat(QStringLiteral("sessions"), QStringLiteral("Active sessions")),
                         makeStat(QStringLiteral("occupancy"), QStringLiteral("Lab occupancy"))});
    root->addWidget(m_stats);

    m_consistency = new MessageBanner();
    m_consistency->setPersistent(true);
    root->addWidget(m_consistency);

    // Laboratories section
    auto* labHeader = new QHBoxLayout();
    labHeader->setSpacing(10);
    auto* labTitles = new QVBoxLayout();
    labTitles->setSpacing(2);
    labTitles->addWidget(makeLabel(QStringLiteral("Laboratories"), QStringLiteral("sectionTitle")));
    labTitles->addWidget(makeLabel(QStringLiteral("Each laboratory is a distinct computer location. Occupancy = "
                                                  "occupied computers / all computers in that laboratory."),
                                   QStringLiteral("caption")));
    labHeader->addLayout(labTitles, 1);
    labHeader->addWidget(makeStatusChip(ComputerStatus::Available, QStringLiteral("Available")), 0, Qt::AlignBottom);
    labHeader->addWidget(makeStatusChip(ComputerStatus::Occupied, QStringLiteral("Occupied")), 0, Qt::AlignBottom);
    labHeader->addWidget(makeStatusChip(ComputerStatus::Maintenance, QStringLiteral("Maintenance")), 0,
                         Qt::AlignBottom);
    root->addLayout(labHeader);

    m_labs = new ResponsiveGrid(340, 16);
    root->addWidget(m_labs);
    m_labsState = makeStateMessage();
    root->addWidget(m_labsState);

    // Active sessions
    QFrame* activeCard = makeCard();
    QVBoxLayout* active = cardLayout(activeCard);
    active->addWidget(makeLabel(QStringLiteral("Active sessions"), QStringLiteral("sectionTitle")));
    m_activeTable = new QTableWidget();
    setupTable(m_activeTable, {QStringLiteral("Student"), QStringLiteral("Computer"), QStringLiteral("Laboratory"),
                               QStringLiteral("Started"), QStringLiteral("Elapsed")});
    m_activeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_activeTable->setMinimumHeight(180);
    active->addWidget(m_activeTable);
    m_activeState = makeStateMessage();
    active->addWidget(m_activeState);
    root->addWidget(activeCard);
    root->addStretch(1);
}

QWidget* DashboardPage::makeStat(const QString& key, const QString& label)
{
    QFrame* card = makeCard();
    QVBoxLayout* layout = cardLayout(card);
    layout->setSpacing(2);
    layout->addWidget(makeLabel(label, QStringLiteral("statLabel")));
    auto* value = makeLabel(QStringLiteral("-"), QStringLiteral("statValue"));
    layout->addWidget(value);
    auto* caption = makeLabel(QString(), QStringLiteral("caption"));
    layout->addWidget(caption);
    layout->addStretch(1);
    m_statValues.insert(key, value);
    m_statCaptions.insert(key, caption);
    return card;
}

void DashboardPage::setStat(const QString& key, const QString& value, const QString& caption)
{
    if (QLabel* v = m_statValues.value(key))
        v->setText(value);
    if (QLabel* c = m_statCaptions.value(key))
        c->setText(caption);
}

void DashboardPage::refresh()
{
    m_banner->clearMessage();
    QString error;
    const DashboardStats s = m_services.computers.dashboardStats(&error);
    if (!error.isEmpty()) {
        m_banner->showError(QStringLiteral("Dashboard figures could not be read: ") + error);
        for (const QString& key : m_statValues.keys())
            setStat(key, QStringLiteral("-"), QStringLiteral("unavailable"));
    } else {
        setStat(QStringLiteral("labs"), QString::number(s.laboratories), QStringLiteral("distinct locations"));
        setStat(QStringLiteral("total"), QString::number(s.totalComputers), QStringLiteral("in the inventory"));
        setStat(QStringLiteral("available"), QString::number(s.available), QStringLiteral("ready for students"));
        setStat(QStringLiteral("occupied"), QString::number(s.occupied), QStringLiteral("in use right now"));
        setStat(QStringLiteral("maintenance"), QString::number(s.maintenance),
                QStringLiteral("%1 open issue(s)").arg(s.openMaintenance));
        setStat(QStringLiteral("sessions"), QString::number(s.activeSessions),
                QStringLiteral("%1 student(s) registered").arg(s.students));
        setStat(QStringLiteral("occupancy"), percent(s.occupied, s.totalComputers),
                QStringLiteral("%1 of %2 computers in use").arg(s.occupied).arg(s.totalComputers));
    }

    QString consistencyError;
    const QStringList problems = m_services.computers.consistencyProblems(&consistencyError);
    if (!consistencyError.isEmpty())
        m_consistency->showError(QStringLiteral("Consistency check could not run: ") + consistencyError);
    else if (problems.isEmpty())
        m_consistency->showSuccess(QStringLiteral("Consistency check passed. Every occupied computer has exactly one "
                                                  "active session, and every computer under maintenance has an open "
                                                  "issue."));
    else
        m_consistency->showWarning(QStringLiteral("Consistency problems found:\n") + problems.join(QLatin1Char('\n')));

    // Who is using which computer (tooltips on the lab tiles).
    QHash<QString, QString> usedBy;
    QString sessionError;
    for (const SessionRecord& r : m_services.sessions.listSessions(true, 1000, &sessionError))
        usedBy.insert(r.computerId, QStringLiteral("%1 (%2)").arg(r.studentName, r.studentId));
    refreshLabs(usedBy);
    refreshActiveSessions();
}

QWidget* DashboardPage::makeLabCard(const LabSummary& lab, const QList<Computer>& computers,
                                    const QHash<QString, QString>& usedBy)
{
    QFrame* card = makeCard();
    QVBoxLayout* layout = cardLayout(card);
    layout->setSpacing(12);

    auto* header = new QHBoxLayout();
    header->setSpacing(10);
    auto* titles = new QVBoxLayout();
    titles->setSpacing(2);
    titles->addWidget(makeLabel(lab.location, QStringLiteral("sectionTitle")));
    titles->addWidget(makeLabel(QStringLiteral("%1 computer(s)").arg(lab.total), QStringLiteral("caption")));
    header->addLayout(titles, 1);
    auto* view = makeButton(QStringLiteral("View computers"));
    view->setToolTip(QStringLiteral("Open the Computers page filtered to this laboratory"));
    const QString location = lab.location;
    connect(view, &QPushButton::clicked, this, [this, location]() { emit openComputers(location); });
    header->addWidget(view, 0, Qt::AlignTop);
    layout->addLayout(header);

    auto* chips = new QHBoxLayout();
    chips->setSpacing(8);
    chips->addWidget(makeStatusChip(ComputerStatus::Available, QStringLiteral("Available %1").arg(lab.available)));
    chips->addWidget(makeStatusChip(ComputerStatus::Occupied, QStringLiteral("Occupied %1").arg(lab.occupied)));
    chips->addWidget(
        makeStatusChip(ComputerStatus::Maintenance, QStringLiteral("Maintenance %1").arg(lab.maintenance)));
    chips->addStretch(1);
    layout->addLayout(chips);

    auto* occupancyRow = new QHBoxLayout();
    occupancyRow->setSpacing(10);
    auto* bar = new QProgressBar();
    bar->setObjectName(QStringLiteral("occupancy"));
    bar->setTextVisible(false);
    bar->setRange(0, qMax(1, lab.total));
    bar->setValue(lab.occupied);
    occupancyRow->addWidget(bar, 1);
    occupancyRow->addWidget(
        makeLabel(QStringLiteral("%1 occupied").arg(percent(lab.occupied, lab.total)), QStringLiteral("caption")));
    layout->addLayout(occupancyRow);

    // Computer tiles wrap to the card width with equal spacing.
    auto* tiles = new QWidget();
    auto* flow = new FlowLayout(tiles, 0, 8, 8);
    for (const Computer& c : computers) {
        const auto colors = theme::statusColors(c.status);
        auto* tile = new QLabel(QStringLiteral("<b>%1</b><br>%2")
                                    .arg(c.computerId.toHtmlEscaped(), c.status.left(1) + c.status.mid(1).toLower()));
        tile->setTextFormat(Qt::RichText);
        tile->setAlignment(Qt::AlignCenter);
        tile->setFixedSize(96, 46);
        tile->setStyleSheet(QStringLiteral("background:%1; color:%2; border-radius:6px; font-size:8.5pt;")
                                .arg(colors.background.name(), colors.foreground.name()));
        QString tip = QStringLiteral("%1\n%2\n%3").arg(c.name, c.location, c.status);
        if (usedBy.contains(c.computerId))
            tip += QStringLiteral("\nIn use by ") + usedBy.value(c.computerId);
        tile->setToolTip(tip);
        flow->addWidget(tile);
    }
    layout->addWidget(tiles);
    layout->addStretch(1);
    return card;
}

void DashboardPage::refreshLabs(const QHash<QString, QString>& usedBy)
{
    std::vector<LabSummary> labs;
    QString error;
    if (!m_services.computers.labSummaries(labs, &error)) {
        m_labs->setWidgets({});
        m_labs->hide();
        showErrorState(m_labsState, error);
        return;
    }
    if (labs.empty()) {
        m_labs->setWidgets({});
        m_labs->hide();
        showEmptyState(m_labsState, QStringLiteral("No laboratories yet. Add computers on the Computers page; each "
                                                   "distinct location becomes a laboratory here."));
        return;
    }
    QString listError;
    QMap<QString, QList<Computer>> byLocation;
    for (const Computer& c : m_services.computers.listComputers(QString(), &listError))
        byLocation[c.location].append(c);
    QList<QWidget*> cards;
    for (const LabSummary& lab : labs)
        cards.append(makeLabCard(lab, byLocation.value(lab.location), usedBy));
    m_labs->setWidgets(cards);
    m_labs->show();
    if (!listError.isEmpty())
        showErrorState(m_labsState, QStringLiteral("Computer tiles could not be loaded: ") + listError);
    else
        m_labsState->hide();
}

void DashboardPage::refreshActiveSessions()
{
    QString error;
    const auto active = m_services.sessions.listSessions(true, 500, &error);
    if (!error.isEmpty()) {
        m_activeTable->setRowCount(0);
        m_activeTable->hide();
        showErrorState(m_activeState, error);
        return;
    }
    m_activeTable->setRowCount(static_cast<int>(active.size()));
    for (int row = 0; row < static_cast<int>(active.size()); ++row) {
        const SessionRecord& r = active[static_cast<std::size_t>(row)];
        m_activeTable->setItem(row, 0, makeItem(QStringLiteral("%1 (%2)").arg(r.studentName, r.studentId)));
        m_activeTable->setItem(row, 1, makeItem(r.computerId));
        m_activeTable->setItem(row, 2, makeItem(r.location));
        m_activeTable->setItem(row, 3, makeItem(timeutil::toDisplay(r.startTime)));
        m_activeTable->setItem(row, 4, makeItem(timeutil::durationText(r.startTime)));
    }
    if (active.empty()) {
        m_activeTable->hide();
        showEmptyState(m_activeState, QStringLiteral("No active computer sessions. Students start one from the "
                                                     "Student portal."));
    } else {
        m_activeTable->show();
        m_activeState->hide();
    }
}

}  // namespace labdesk::ui
