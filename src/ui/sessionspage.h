#pragma once

#include "page.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace labdesk {
class Database;
struct Services;
}

namespace labdesk::ui {

class MessageBanner;

// Session history for administrators, plus a transaction demonstration that
// runs the real allocation/termination code and prints every SQL step,
// optionally with an injected failure to show ROLLBACK.
class SessionsPage : public Page {
    Q_OBJECT
public:
    SessionsPage(Database& db, Services& services, QWidget* parent = nullptr);
    void refresh() override;

private:
    QWidget* buildHistoryTab();
    QWidget* buildTransactionTab();
    void reloadHistory();
    void reloadDemoChoices();
    void onEndSelected();
    void runAllocation();
    void runEndSession();
    QString describeState(const QString& studentId, const QString& computerId) const;

    Database& m_db;
    Services& m_services;
    MessageBanner* m_banner = nullptr;

    QCheckBox* m_activeOnly = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_empty = nullptr;
    QPushButton* m_endSelected = nullptr;

    QComboBox* m_demoStudent = nullptr;
    QComboBox* m_demoComputer = nullptr;
    QCheckBox* m_injectFailure = nullptr;
    QPlainTextEdit* m_log = nullptr;
};

}  // namespace labdesk::ui
