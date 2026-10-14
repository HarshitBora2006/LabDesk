#include "mainwindow.h"

#include "database.h"
#include "services.h"
#include "ui/computerspage.h"
#include "ui/dashboardpage.h"
#include "ui/labqueuepage.h"
#include "ui/maintenancepage.h"
#include "ui/page.h"
#include "ui/processstatepage.h"
#include "ui/schedulerpage.h"
#include "ui/sessionspage.h"
#include "ui/studentportalpage.h"
#include "ui/studentspage.h"
#include "ui/uihelpers.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>

namespace labdesk {

MainWindow::MainWindow(Database& db, QWidget* parent)
    : QMainWindow(parent), m_db(db), m_services(std::make_unique<Services>(db))
{
    setWindowTitle(QStringLiteral("LabDesk: Smart Computer Lab Management and CPU Scheduling"));
    resize(1320, 840);
    setMinimumSize(1080, 680);

    auto* central = new QWidget();
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* sidebar = new QWidget();
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setAttribute(Qt::WA_StyledBackground, true);
    sidebar->setFixedWidth(232);
    auto* side = new QVBoxLayout(sidebar);
    side->setContentsMargins(0, 0, 0, 12);
    side->setSpacing(0);
    auto* brand = new QLabel(QStringLiteral("LabDesk"));
    brand->setObjectName(QStringLiteral("brand"));
    auto* brandSub = new QLabel(QStringLiteral("Lab management and CPU scheduling"));
    brandSub->setObjectName(QStringLiteral("brandSub"));
    brandSub->setWordWrap(true);
    side->addWidget(brand);
    side->addWidget(brandSub);
    m_nav = new QListWidget();
    m_nav->setObjectName(QStringLiteral("nav"));
    m_nav->setFocusPolicy(Qt::NoFocus);
    side->addWidget(m_nav, 1);
    auto* reload = ui::makeButton(QStringLiteral("Reload from database"));
    reload->setToolTip(QStringLiteral("Re-read every screen from the database"));
    side->addWidget(reload, 0, Qt::AlignHCenter);

    m_stack = new QStackedWidget();
    m_stack->setObjectName(QStringLiteral("pageStack"));
    m_stack->setAttribute(Qt::WA_StyledBackground, true);
    layout->addWidget(sidebar);
    layout->addWidget(m_stack, 1);
    setCentralWidget(central);

    Services& s = *m_services;
    auto* scheduler = new ui::SchedulerPage(s);
    auto* processStates = new ui::ProcessStatePage();
    auto* dashboard = new ui::DashboardPage(s);
    auto* computers = new ui::ComputersPage(s);

    addSection(QStringLiteral("Administration"));
    addPage(QStringLiteral("Dashboard"), dashboard);
    addPage(QStringLiteral("Students"), new ui::StudentsPage(s));
    addPage(QStringLiteral("Computers"), computers);
    addPage(QStringLiteral("Maintenance"), new ui::MaintenancePage(s));
    addPage(QStringLiteral("Sessions and transactions"), new ui::SessionsPage(m_db, s));
    addPage(QStringLiteral("Lab queue"), new ui::LabQueuePage(s));
    addSection(QStringLiteral("Students"));
    addPage(QStringLiteral("Student portal"), new ui::StudentPortalPage(s));
    addSection(QStringLiteral("Operating systems"));
    addPage(QStringLiteral("CPU scheduling"), scheduler);
    addPage(QStringLiteral("Process states"), processStates);

    connect(scheduler, &ui::SchedulerPage::scheduleComputed, processStates, &ui::ProcessStatePage::showSchedule);
    connect(dashboard, &ui::DashboardPage::openComputers, this, [this, computers](const QString& location) {
        showPage(computers);
        computers->showLocation(location);
    });
    connect(m_nav, &QListWidget::currentRowChanged, this, &MainWindow::onNavigation);
    connect(reload, &QPushButton::clicked, this, [this]() {
        refreshAll();
        statusBar()->showMessage(QStringLiteral("Reloaded from the database."), 3000);
    });

    auto* dbLabel = new QLabel(QStringLiteral("Database: %1").arg(m_db.filePath()));
    dbLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addPermanentWidget(dbLabel);
    refreshAll();
    m_nav->setCurrentRow(1);  // row 0 is the "Administration" heading
}

MainWindow::~MainWindow() = default;

void MainWindow::addSection(const QString& title)
{
    auto* item = new QListWidgetItem(title, m_nav);
    item->setFlags(Qt::NoItemFlags);
    item->setData(Qt::UserRole, -1);
    QFont f = item->font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    f.setBold(true);
    item->setFont(f);
}

void MainWindow::addPage(const QString& title, ui::Page* page)
{
    const int index = m_stack->addWidget(page);
    m_pages.push_back(page);
    auto* item = new QListWidgetItem(title, m_nav);
    item->setData(Qt::UserRole, index);
    connect(page, &ui::Page::dataChanged, this, &MainWindow::refreshAll);
}

void MainWindow::refreshAll()
{
    for (ui::Page* page : m_pages)
        page->refresh();
}

void MainWindow::showPage(QWidget* page)
{
    const int index = m_stack->indexOf(page);
    for (int row = 0; row < m_nav->count(); ++row) {
        if (m_nav->item(row)->data(Qt::UserRole).toInt() == index) {
            m_nav->setCurrentRow(row);  // triggers onNavigation, which refreshes the page
            return;
        }
    }
}

void MainWindow::onNavigation(int row)
{
    QListWidgetItem* item = m_nav->item(row);
    if (!item)
        return;
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0)
        return;
    m_stack->setCurrentIndex(index);
    if (auto* page = qobject_cast<ui::Page*>(m_stack->currentWidget()))
        page->refresh();
}

}  // namespace labdesk
