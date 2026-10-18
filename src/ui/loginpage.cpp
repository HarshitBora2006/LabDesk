#include "loginpage.h"

#include "services.h"
#include "uihelpers.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace labdesk::ui {

namespace {
QLabel* fieldLabel(const QString& text) { return makeLabel(text, QStringLiteral("fieldLabel")); }

// Password field with a Show / Hide toggle next to it.
QWidget* passwordRow(QLineEdit* field, QPushButton** toggleOut)
{
    auto* row = new QWidget();
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    field->setEchoMode(QLineEdit::Password);
    layout->addWidget(field, 1);
    auto* toggle = makeButton(QStringLiteral("Show"));
    toggle->setCheckable(true);
    toggle->setToolTip(QStringLiteral("Show or hide the password"));
    toggle->setFocusPolicy(Qt::NoFocus);
    QObject::connect(toggle, &QPushButton::toggled, field, [field, toggle](bool shown) {
        field->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
        toggle->setText(shown ? QStringLiteral("Hide") : QStringLiteral("Show"));
    });
    layout->addWidget(toggle);
    if (toggleOut)
        *toggleOut = toggle;
    return row;
}
}  // namespace

LoginPage::LoginPage(Services& services, QWidget* parent) : QWidget(parent), m_services(services)
{
    setObjectName(QStringLiteral("loginRoot"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildBrandPanel(), 2);

    auto* right = new QWidget();
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(32, 32, 32, 32);
    rightLayout->addStretch(1);
    auto* centre = new QHBoxLayout();
    centre->addStretch(1);

    auto* card = new QFrame();
    card->setObjectName(QStringLiteral("loginCard"));
    card->setFixedWidth(430);
    auto* box = new QVBoxLayout(card);
    box->setContentsMargins(32, 30, 32, 30);
    box->setSpacing(12);
    m_cardTitle = makeLabel(QStringLiteral("Sign in"), QStringLiteral("pageTitle"));
    box->addWidget(m_cardTitle);
    box->addWidget(makeLabel(QStringLiteral("Choose your role, then enter your credentials."), QStringLiteral("caption")));

    auto* roles = new QHBoxLayout();
    roles->setSpacing(0);
    m_adminRole = makeButton(QStringLiteral("Admin Login"));
    m_studentRole = makeButton(QStringLiteral("Student Login"));
    m_roles = new QButtonGroup(this);
    m_roles->setExclusive(true);
    for (QPushButton* b : {m_adminRole, m_studentRole}) {
        b->setCheckable(true);
        b->setProperty("segment", true);
        m_roles->addButton(b);
        roles->addWidget(b, 1);
    }
    m_studentRole->setChecked(true);
    box->addLayout(roles);

    m_banner = new MessageBanner();
    box->addWidget(m_banner);
    m_forms = new QStackedWidget();
    m_forms->addWidget(buildSignInForm());
    m_forms->addWidget(buildSetupForm());
    box->addWidget(m_forms);

    centre->addWidget(card);
    centre->addStretch(1);
    rightLayout->addLayout(centre);
    rightLayout->addStretch(1);
    root->addWidget(right, 3);

    connect(m_adminRole, &QPushButton::toggled, this, [this]() { updateRole(); });
    updateRole();
}

QWidget* LoginPage::buildBrandPanel()
{
    auto* panel = new QFrame();
    panel->setObjectName(QStringLiteral("loginBrand"));
    panel->setMinimumWidth(360);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(48, 48, 48, 40);
    layout->setSpacing(10);
    layout->addStretch(1);
    layout->addWidget(makeLabel(QStringLiteral("LabDesk"), QStringLiteral("loginTitle")));
    layout->addWidget(makeLabel(QStringLiteral("Computer Lab Management System"), QStringLiteral("loginSubtitle")));
    layout->addSpacing(18);
    for (const QString& point :
         {QStringLiteral("Administrators manage students, computers, allocations, usage logs and reports."),
          QStringLiteral("Students request a computer, follow the waiting queue and end their session."),
          QStringLiteral("The OS and DBMS modules show scheduling, process states, deadlock avoidance, "
                         "transactions and the real database schema.")})
        layout->addWidget(makeLabel(point, QStringLiteral("loginPoint")));
    layout->addStretch(2);
    return panel;
}

QWidget* LoginPage::buildSignInForm()
{
    auto* form = new QWidget();
    auto* layout = new QVBoxLayout(form);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(8);
    m_idLabel = fieldLabel(QString());
    layout->addWidget(m_idLabel);
    m_id = new QLineEdit();
    layout->addWidget(m_id);
    layout->addWidget(fieldLabel(QStringLiteral("Password")));
    m_password = new QLineEdit();
    layout->addWidget(passwordRow(m_password, &m_showPassword));
    layout->addSpacing(6);
    auto* signIn = makeButton(QStringLiteral("Sign in"), QStringLiteral("primary"));
    layout->addWidget(signIn);
    m_hint = makeLabel(QString(), QStringLiteral("caption"));
    layout->addWidget(m_hint);
    connect(signIn, &QPushButton::clicked, this, &LoginPage::onSignIn);
    connect(m_password, &QLineEdit::returnPressed, this, &LoginPage::onSignIn);
    connect(m_id, &QLineEdit::returnPressed, this, [this]() { m_password->setFocus(); });
    return form;
}

QWidget* LoginPage::buildSetupForm()
{
    auto* form = new QWidget();
    auto* layout = new QVBoxLayout(form);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(makeLabel(QStringLiteral("No administrator account exists yet. Create the first one now; its "
                                               "password is stored only as a salted hash."),
                                QStringLiteral("caption")));
    m_newId = new QLineEdit();
    m_newId->setPlaceholderText(QStringLiteral("e.g. labadmin"));
    m_newName = new QLineEdit();
    m_newPassword = new QLineEdit();
    m_confirmPassword = new QLineEdit();
    m_confirmPassword->setEchoMode(QLineEdit::Password);
    layout->addWidget(fieldLabel(QStringLiteral("Admin ID")));
    layout->addWidget(m_newId);
    layout->addWidget(fieldLabel(QStringLiteral("Full name")));
    layout->addWidget(m_newName);
    layout->addWidget(fieldLabel(QStringLiteral("Password (at least 8 characters)")));
    layout->addWidget(passwordRow(m_newPassword, nullptr));
    layout->addWidget(fieldLabel(QStringLiteral("Confirm password")));
    layout->addWidget(m_confirmPassword);
    layout->addSpacing(6);
    auto* create = makeButton(QStringLiteral("Create administrator and sign in"), QStringLiteral("primary"));
    layout->addWidget(create);
    connect(create, &QPushButton::clicked, this, &LoginPage::onCreateAdmin);
    connect(m_confirmPassword, &QLineEdit::returnPressed, this, &LoginPage::onCreateAdmin);
    return form;
}

bool LoginPage::adminSelected() const
{
    return m_adminRole->isChecked();
}

void LoginPage::updateRole()
{
    m_banner->clearMessage();
    m_password->clear();
    if (adminSelected()) {
        QString error;
        const int admins = m_services.admins.adminCount(&error);
        if (admins < 0)
            m_banner->showError(QStringLiteral("Administrator accounts could not be read: ") + error);
        const bool setup = admins == 0;
        m_forms->setCurrentIndex(setup ? 1 : 0);
        m_cardTitle->setText(setup ? QStringLiteral("Create administrator") : QStringLiteral("Admin sign in"));
        m_idLabel->setText(QStringLiteral("Admin ID"));
        m_id->setPlaceholderText(QStringLiteral("Your admin ID"));
        m_hint->setText(QString());
        m_hint->hide();
    } else {
        m_forms->setCurrentIndex(0);
        m_cardTitle->setText(QStringLiteral("Student sign in"));
        m_idLabel->setText(QStringLiteral("Student ID"));
        m_id->setPlaceholderText(QStringLiteral("Your student ID"));
        // Mention demo accounts only when they really exist in this database.
        const bool demo = m_services.students.findStudent(QStringLiteral("DEMO01")).has_value();
        m_hint->setText(QStringLiteral("Demo accounts in this database: DEMO01 to DEMO04, password demo123."));
        m_hint->setVisible(demo);
    }
}

void LoginPage::reset()
{
    m_id->clear();
    m_password->clear();
    if (m_showPassword)
        m_showPassword->setChecked(false);
    updateRole();
}

void LoginPage::onSignIn()
{
    QString error;
    const QString id = m_id->text().trimmed();
    if (adminSelected()) {
        const auto admin = m_services.admins.authenticate(id, m_password->text(), &error);
        m_password->clear();
        if (!admin) {
            m_banner->showError(error);
            return;
        }
        m_banner->clearMessage();
        emit adminAuthenticated(admin->adminId, admin->name);
    } else {
        const auto student = m_services.students.authenticate(id, m_password->text(), &error);
        m_password->clear();
        if (!student) {
            m_banner->showError(error);
            return;
        }
        m_banner->clearMessage();
        emit studentAuthenticated(student->studentId, student->name);
    }
}

void LoginPage::onCreateAdmin()
{
    if (m_newPassword->text() != m_confirmPassword->text()) {
        m_banner->showError(QStringLiteral("The two passwords do not match."));
        return;
    }
    const OpResult r =
        m_services.admins.createFirstAdmin(m_newId->text(), m_newName->text(), m_newPassword->text());
    if (!r.ok) {
        m_banner->showError(r.message);
        return;
    }
    const auto admin = m_services.admins.authenticate(m_newId->text(), m_newPassword->text());
    m_newPassword->clear();
    m_confirmPassword->clear();
    if (!admin) {
        m_banner->showError(QStringLiteral("The account was created but signing in failed. Try signing in."));
        updateRole();
        return;
    }
    emit adminAuthenticated(admin->adminId, admin->name);
}

}  // namespace labdesk::ui
