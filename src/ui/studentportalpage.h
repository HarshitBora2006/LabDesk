#pragma once

#include "page.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTimer;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class MessageBanner;

// Student-facing portal. Nothing beyond the sign-in form is reachable until
// StudentService::authenticate() succeeds; every action uses the signed-in
// student's ID, never an ID typed into the dashboard.
class StudentPortalPage : public Page {
    Q_OBJECT
public:
    StudentPortalPage(Services& services, QWidget* parent = nullptr);
    void refresh() override;

private:
    QWidget* buildLogin();
    QWidget* buildDashboard();
    void onSignIn();
    void onSignOut();
    void onUseComputer();
    void onEndSession();
    void updateElapsed();
    void updateUseButton();

    Services& m_services;
    QStackedWidget* m_stack = nullptr;

    QLineEdit* m_loginId = nullptr;
    QLineEdit* m_loginPassword = nullptr;
    MessageBanner* m_loginBanner = nullptr;

    QString m_studentId;  // set only after successful authentication
    QString m_sessionStart;
    QLabel* m_welcome = nullptr;
    QLabel* m_identity = nullptr;
    MessageBanner* m_banner = nullptr;
    QLabel* m_sessionSummary = nullptr;
    QLabel* m_sessionDetails = nullptr;
    QLabel* m_elapsed = nullptr;
    QPushButton* m_endSession = nullptr;
    QTableWidget* m_available = nullptr;
    QLabel* m_noneAvailable = nullptr;
    QPushButton* m_useComputer = nullptr;
    QTimer* m_clock = nullptr;
};

}  // namespace labdesk::ui
