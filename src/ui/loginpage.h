#pragma once
// First screen: choose Admin or Student, then sign in against the database.
// When no administrator exists yet, the Admin role shows a one-time form to
// create the first administrator (no default password is built in).

#include <QWidget>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class MessageBanner;

class LoginPage : public QWidget {
    Q_OBJECT
public:
    LoginPage(Services& services, QWidget* parent = nullptr);
    void reset();  // called after logout

signals:
    void adminAuthenticated(const QString& adminId, const QString& name);
    void studentAuthenticated(const QString& studentId, const QString& name);

private:
    QWidget* buildBrandPanel();
    QWidget* buildSignInForm();
    QWidget* buildSetupForm();
    bool adminSelected() const;
    void updateRole();
    void onSignIn();
    void onCreateAdmin();

    Services& m_services;
    QButtonGroup* m_roles = nullptr;
    QPushButton* m_adminRole = nullptr;
    QPushButton* m_studentRole = nullptr;
    MessageBanner* m_banner = nullptr;
    QStackedWidget* m_forms = nullptr;
    QLabel* m_cardTitle = nullptr;
    QLabel* m_idLabel = nullptr;
    QLineEdit* m_id = nullptr;
    QLineEdit* m_password = nullptr;
    QPushButton* m_showPassword = nullptr;
    QLabel* m_hint = nullptr;
    QLineEdit* m_newId = nullptr;
    QLineEdit* m_newName = nullptr;
    QLineEdit* m_newPassword = nullptr;
    QLineEdit* m_confirmPassword = nullptr;
};

}  // namespace labdesk::ui
