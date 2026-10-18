#include "mainwindow.h"

#include "database.h"
#include "services.h"
#include "ui/bankerspage.h"
#include "ui/computerspage.h"
#include "ui/conceptguidepage.h"
#include "ui/dashboardpage.h"
#include "ui/dboverviewpage.h"
#include "ui/labqueuepage.h"
#include "ui/loginpage.h"
#include "ui/maintenancepage.h"
#include "ui/modulepage.h"
#include "ui/page.h"
#include "ui/processstatepage.h"
#include "ui/reportspage.h"
#include "ui/resourceallocationpage.h"
#include "ui/schedulerpage.h"
#include "ui/schemapage.h"
#include "ui/sessionspage.h"
#include "ui/settingspage.h"
#include "ui/sqloperationspage.h"
#include "ui/studenthistorypage.h"
#include "ui/studenthomepage.h"
#include "ui/studentportalpage.h"
#include "ui/studentspage.h"
#include "ui/uihelpers.h"

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace labdesk {

MainWindow::MainWindow(Database& db, QWidget* parent)
    : QMainWindow(parent), m_db(db), m_services(std::make_unique<Services>(db))
{
    setWindowTitle(QStringLiteral("LabDesk - Computer Lab Management System"));
    resize(1360, 860);
    setMinimumSize(1100, 700);

    m_root = new QStackedWidget();
    m_root->setObjectName(QStringLiteral("pageStack"));
    m_root->setAttribute(Qt::WA_StyledBackground, true);
    m_login = new ui::LoginPage(*m_services);
    m_root->addWidget(m_login);
    setCentralWidget(m_root);

    connect(m_login, &ui::LoginPage::adminAuthenticated, this,
            [this](const QString& id, const QString& name) { startSession(Role::Admin, id, name); });
    connect(m_login, &ui::LoginPage::studentAuthenticated, this,
            [this](const QString& id, const QString& name) { startSession(Role::Student, id, name); });
}

MainWindow::~MainWindow() = default;

void MainWindow::startSession(Role role, const QString& userId, const QString& name)
{
    m_shell = new QWidget();
    auto* layout = new QHBoxLayout(m_shell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* sidebar = new QWidget();
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setAttribute(Qt::WA_StyledBackground, true);
    sidebar->setFixedWidth(250);
    auto* side = new QVBoxLayout(sidebar);
    side->setContentsMargins(0, 0, 0, 14);
    side->setSpacing(0);
    auto* brand = new QLabel(QStringLiteral("LabDesk"));
    brand->setObjectName(QStringLiteral("brand"));
    auto* brandSub = new QLabel(QStringLiteral("Computer Lab Management System"));
    brandSub->setObjectName(QStringLiteral("brandSub"));
    brandSub->setWordWrap(true);
    side->addWidget(brand);
    side->addWidget(brandSub);
    m_nav = new QListWidget();
    m_nav->setObjectName(QStringLiteral("nav"));
    m_nav->setFocusPolicy(Qt::NoFocus);
    side->addWidget(m_nav, 1);

    auto* userBox = new QFrame();
    userBox->setObjectName(QStringLiteral("userBox"));
    auto* userLayout = new QVBoxLayout(userBox);
    userLayout->setContentsMargins(12, 10, 12, 10);
    userLayout->setSpacing(2);
    auto* userName = new QLabel(QStringLiteral("%1 (%2)").arg(name, userId));
    userName->setObjectName(QStringLiteral("userName"));
    userName->setWordWrap(true);
    auto* userRole = new QLabel(role == Role::Admin ? QStringLiteral("Administrator") : QStringLiteral("Student"));
    userRole->setObjectName(QStringLiteral("userRole"));
    userLayout->addWidget(userName);
    userLayout->addWidget(userRole);
    auto* userWrap = new QHBoxLayout();
    userWrap->setContentsMargins(12, 8, 12, 8);
    userWrap->addWidget(userBox);
    side->addLayout(userWrap);
    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(12, 0, 12, 0);
    buttons->setSpacing(8);
    auto* reload = ui::makeButton(QStringLiteral("Reload"));
    reload->setObjectName(QStringLiteral("sidebarButton"));
    reload->setToolTip(QStringLiteral("Re-read every page from the database"));
    auto* logoutButton = ui::makeButton(QStringLiteral("Log out"));
    logoutButton->setObjectName(QStringLiteral("sidebarButton"));
    buttons->addWidget(reload);
    buttons->addWidget(logoutButton, 1);
    side->addLayout(buttons);

    m_stack = new QStackedWidget();
    m_stack->setObjectName(QStringLiteral("pageStack"));
    m_stack->setAttribute(Qt::WA_StyledBackground, true);
    layout->addWidget(sidebar);
    layout->addWidget(m_stack, 1);

    m_pages.clear();
    if (role == Role::Admin)
        buildAdminPages(userId);
    else
        buildStudentPages(userId);

    connect(m_nav, &QListWidget::currentRowChanged, this, &MainWindow::onNavigation);
    connect(reload, &QPushButton::clicked, this, &MainWindow::refreshAll);
    connect(logoutButton, &QPushButton::clicked, this, &MainWindow::logout);

    m_root->addWidget(m_shell);
    m_root->setCurrentWidget(m_shell);
    refreshAll();
    m_nav->setCurrentRow(0);
}

void MainWindow::buildAdminPages(const QString& adminId)
{
    Services& s = *m_services;
    auto* dashboard = new ui::DashboardPage(s);
    auto* computers = new ui::ComputersPage(s);

    auto* computerModule = new ui::ModulePage();
    computerModule->addSubPage(computers, QStringLiteral("Computers"));
    computerModule->addSubPage(new ui::MaintenancePage(s), QStringLiteral("Maintenance"));

    auto* scheduler = new ui::SchedulerPage(s);
    auto* processes = new ui::ProcessStatePage();
    auto* os = new ui::ModulePage();
    os->addSubPage(processes, QStringLiteral("Process Management"));
    os->addSubPage(scheduler, QStringLiteral("CPU Scheduling"));
    os->addSubPage(new ui::ResourceAllocationPage(s), QStringLiteral("Resource Allocation"));
    os->addSubPage(new ui::BankersPage(), QStringLiteral("Deadlock Avoidance"));
    os->addSubPage(new ui::ConceptGuidePage(ui::ConceptGuidePage::Topic::OperatingSystems), QStringLiteral("OS Concept Guide"));
    connect(scheduler, &ui::SchedulerPage::scheduleComputed, processes, &ui::ProcessStatePage::showSchedule);

    auto* dbms = new ui::ModulePage();
    dbms->addSubPage(new ui::DbOverviewPage(s), QStringLiteral("Database Overview"));
    dbms->addSubPage(new ui::SchemaPage(s), QStringLiteral("Schema and Relationships"));
    dbms->addSubPage(new ui::SqlOperationsPage(s), QStringLiteral("SQL Operations"));
    dbms->addSubPage(new ui::SessionsPage(m_db, s, ui::SessionsPage::Mode::TransactionDemo),
                     QStringLiteral("Transactions and Integrity"));
    dbms->addSubPage(new ui::ConceptGuidePage(ui::ConceptGuidePage::Topic::Databases), QStringLiteral("DBMS Concept Guide"));

    addPage(QStringLiteral("Dashboard"), dashboard);
    addPage(QStringLiteral("Student Management"), new ui::StudentsPage(s));
    addPage(QStringLiteral("Computer Management"), computerModule);
    addPage(QStringLiteral("Allocation Management"), new ui::LabQueuePage(s));
    addSection(QStringLiteral("Concepts in LabDesk"));
    addPage(QStringLiteral("OS Management"), os);
    addPage(QStringLiteral("DBMS Management"), dbms);
    addSection(QStringLiteral("Records"));
    addPage(QStringLiteral("Usage Logs"), new ui::SessionsPage(m_db, s, ui::SessionsPage::Mode::History));
    addPage(QStringLiteral("Reports"), new ui::ReportsPage(s));
    addPage(QStringLiteral("Settings"), new ui::SettingsPage(s, adminId));

    connect(dashboard, &ui::DashboardPage::openComputers, this, [this, computerModule, computers](const QString& lab) {
        showPage(computerModule);
        computers->showLocation(lab);
    });
}

void MainWindow::buildStudentPages(const QString& studentId)
{
    Services& s = *m_services;
    auto* home = new ui::StudentHomePage(s, studentId);
    auto* session = new ui::StudentPortalPage(s, studentId);

    auto* scheduler = new ui::SchedulerPage(s);
    scheduler->setStudentScope(studentId);
    auto* processes = new ui::ProcessStatePage();
    auto* os = new ui::ModulePage();
    os->addSubPage(processes, QStringLiteral("Process Management"));
    os->addSubPage(scheduler, QStringLiteral("CPU Scheduling"));
    os->addSubPage(new ui::BankersPage(), QStringLiteral("Deadlock Avoidance"));
    os->addSubPage(new ui::ConceptGuidePage(ui::ConceptGuidePage::Topic::OperatingSystems), QStringLiteral("OS Concept Guide"));
    connect(scheduler, &ui::SchedulerPage::scheduleComputed, processes, &ui::ProcessStatePage::showSchedule);

    addPage(QStringLiteral("Dashboard"), home);
    addPage(QStringLiteral("My Session"), session);
    addPage(QStringLiteral("My Allocation History"), new ui::StudentHistoryPage(s, studentId));
    addSection(QStringLiteral("Learning"));
    addPage(QStringLiteral("OS Management"), os);

    connect(home, &ui::StudentHomePage::openMySession, this, [this, session]() { showPage(session); });
}

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
    if (!m_stack || !m_nav)
        return;
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
    QListWidgetItem* item = m_nav ? m_nav->item(row) : nullptr;
    if (!item)
        return;
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0)
        return;
    m_stack->setCurrentIndex(index);
    if (auto* page = qobject_cast<ui::Page*>(m_stack->currentWidget()))
        page->refresh();
}

void MainWindow::logout()
{
    if (!ui::confirm(this, QStringLiteral("Log out"), QStringLiteral("Log out of LabDesk?"), QStringLiteral("Log out")))
        return;
    QWidget* shell = m_shell;
    m_shell = nullptr;
    m_nav = nullptr;
    m_stack = nullptr;
    m_pages.clear();
    m_root->removeWidget(shell);
    shell->deleteLater();  // safe: we may be inside a click handler of a widget in the shell
    m_login->reset();
    m_root->setCurrentWidget(m_login);
}

}  // namespace labdesk
