#pragma once

#include "page.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QWidget;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class MessageBanner;

// Student-facing portal. Nothing beyond the sign-in form is reachable until
// StudentService::authenticate() succeeds; every action uses the signed-in
// student's ID, never an ID typed into the dashboard.
//
// Every visit starts with a request (purpose + expected length), so the lab
// queue records real arrival times. The student either starts on a free
// computer at once (waiting 0) or joins the queue; the actual session length
// (burst) is measured when the session ends.
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
    void onStartNow();
    void onJoinQueue();
    void onLeaveQueue();
    void onEndSession();
    void updateElapsed();
    void updateRequestControls();
    QString selectedPurpose() const;

    Services& m_services;
    QStackedWidget* m_stack = nullptr;

    QLineEdit* m_loginId = nullptr;
    QLineEdit* m_loginPassword = nullptr;
    MessageBanner* m_loginBanner = nullptr;

    QString m_studentId;     // set only after successful authentication
    QString m_timerStart;    // session start or queue arrival (UTC ISO), empty if neither
    QString m_timerPrefix;   // "Time at the computer" / "Waiting for"
    bool m_inSession = false;
    bool m_waiting = false;
    int m_queueLength = 0;

    QLabel* m_welcome = nullptr;
    QLabel* m_identity = nullptr;
    MessageBanner* m_banner = nullptr;
    QLabel* m_visitSummary = nullptr;
    QLabel* m_visitDetails = nullptr;
    QLabel* m_elapsed = nullptr;
    QPushButton* m_endSession = nullptr;
    QPushButton* m_leaveQueue = nullptr;

    QWidget* m_requestBody = nullptr;
    QLabel* m_requestHint = nullptr;
    QComboBox* m_purpose = nullptr;
    QSpinBox* m_expected = nullptr;
    QTableWidget* m_available = nullptr;
    QLabel* m_noneAvailable = nullptr;
    QPushButton* m_startNow = nullptr;
    QPushButton* m_joinQueue = nullptr;
    QTimer* m_clock = nullptr;
};

}  // namespace labdesk::ui
