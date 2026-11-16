#include "settingspage.h"

#include "services.h"
#include "uihelpers.h"

#include <QCoreApplication>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtGlobal>

namespace labdesk::ui {

namespace {
QLineEdit* passwordField()
{
    auto* e = new QLineEdit();
    e->setEchoMode(QLineEdit::Password);
    return e;
}
}  // namespace

SettingsPage::SettingsPage(Services& services, const QString& adminId, QWidget* parent)
    : Page(parent), m_services(services), m_adminId(adminId)
{
    QWidget* body = makeScrollPage(this);
    QVBoxLayout* root = makePageLayout(body, QStringLiteral("Settings"),
                                       QStringLiteral("Account and system settings"));
    m_banner = new MessageBanner();
    root->addWidget(m_banner);

    auto* row = new QHBoxLayout();
    row->setSpacing(16);
    QFrame* account = makeCard();
    cardLayout(account)->addWidget(makeLabel(QStringLiteral("Change your password"), QStringLiteral("sectionTitle")));
    auto* form = new QFormLayout();
    form->setVerticalSpacing(10);
    m_current = passwordField();
    m_new = passwordField();
    m_confirm = passwordField();
    form->addRow(QStringLiteral("Current password"), m_current);
    form->addRow(QStringLiteral("New password"), m_new);
    form->addRow(QStringLiteral("Confirm new password"), m_confirm);
    cardLayout(account)->addLayout(form);
    auto* change = makeButton(QStringLiteral("Change password"), QStringLiteral("primary"));
    cardLayout(account)->addWidget(change, 0, Qt::AlignLeft);
    cardLayout(account)->addStretch(1);
    row->addWidget(account, 1);

    QFrame* admins = makeCard();
    cardLayout(admins)->addWidget(makeLabel(QStringLiteral("Administrator accounts"), QStringLiteral("sectionTitle")));
    m_admins = new QTableWidget();
    setupTable(m_admins, {QStringLiteral("Admin ID"), QStringLiteral("Name")});
    m_admins->setMinimumHeight(130);
    cardLayout(admins)->addWidget(m_admins);
    auto* addForm = new QFormLayout();
    addForm->setVerticalSpacing(8);
    m_addId = new QLineEdit();
    m_addName = new QLineEdit();
    m_addPassword = passwordField();
    addForm->addRow(QStringLiteral("Admin ID"), m_addId);
    addForm->addRow(QStringLiteral("Name"), m_addName);
    addForm->addRow(QStringLiteral("Initial password"), m_addPassword);
    cardLayout(admins)->addLayout(addForm);
    auto* add = makeButton(QStringLiteral("Add administrator"));
    cardLayout(admins)->addWidget(add, 0, Qt::AlignLeft);
    row->addWidget(admins, 1);
    root->addLayout(row);

    QFrame* about = makeCard();
    cardLayout(about)->addWidget(makeLabel(QStringLiteral("About this installation"), QStringLiteral("sectionTitle")));
    m_about = makeLabel(QString());
    cardLayout(about)->addWidget(m_about);
    root->addWidget(about);
    root->addStretch(1);

    connect(change, &QPushButton::clicked, this, &SettingsPage::onChangePassword);
    connect(add, &QPushButton::clicked, this, &SettingsPage::onAddAdmin);
}

void SettingsPage::refresh()
{
    QString error;
    const auto admins = m_services.admins.listAdmins(&error);
    m_admins->setRowCount(static_cast<int>(admins.size()));
    for (int r = 0; r < static_cast<int>(admins.size()); ++r) {
        m_admins->setItem(r, 0, makeItem(admins[static_cast<std::size_t>(r)].adminId));
        m_admins->setItem(r, 1, makeItem(admins[static_cast<std::size_t>(r)].name));
    }
    if (!error.isEmpty())
        m_banner->showError(QStringLiteral("Administrator accounts could not be read: ") + error);

    DatabaseOverview o;
    QString dbError;
    const bool ok = m_services.dbms.overview(o, &dbError);
    m_about->setText(QStringLiteral("LabDesk %1 (Qt %2, running on Qt %3)\nDatabase: %4, schema version %5, foreign keys %6.")
                         .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(QT_VERSION_STR),
                              QString::fromLatin1(qVersion()), ok ? o.engine : QStringLiteral("unavailable"))
                         .arg(o.schemaVersion)
                         .arg(o.foreignKeysOn ? QStringLiteral("enforced") : QStringLiteral("OFF")));
}

void SettingsPage::onChangePassword()
{
    if (m_new->text() != m_confirm->text()) {
        m_banner->showError(QStringLiteral("The new passwords do not match."));
        return;
    }
    const OpResult r = m_services.admins.changePassword(m_adminId, m_current->text(), m_new->text());
    m_current->clear();
    m_new->clear();
    m_confirm->clear();
    if (r.ok)
        m_banner->showSuccess(r.message);
    else
        m_banner->showError(r.message);
}

void SettingsPage::onAddAdmin()
{
    const OpResult r = m_services.admins.addAdmin(m_addId->text(), m_addName->text(), m_addPassword->text());
    m_addPassword->clear();
    if (!r.ok) {
        m_banner->showError(r.message);
        return;
    }
    m_addId->clear();
    m_addName->clear();
    m_banner->showSuccess(r.message);
    refresh();
}

}  // namespace labdesk::ui
