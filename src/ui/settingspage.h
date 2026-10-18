#pragma once
// Settings (administrators): own password, administrator accounts and
// information about this installation. The queue policy lives on the
// Allocation Management page where its effect is visible.

#include "page.h"

class QLabel;
class QLineEdit;
class QTableWidget;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class MessageBanner;

class SettingsPage : public Page {
    Q_OBJECT
public:
    SettingsPage(Services& services, const QString& adminId, QWidget* parent = nullptr);
    void refresh() override;

private:
    void onChangePassword();
    void onAddAdmin();

    Services& m_services;
    QString m_adminId;
    MessageBanner* m_banner = nullptr;
    QLineEdit* m_current = nullptr;
    QLineEdit* m_new = nullptr;
    QLineEdit* m_confirm = nullptr;
    QTableWidget* m_admins = nullptr;
    QLineEdit* m_addId = nullptr;
    QLineEdit* m_addName = nullptr;
    QLineEdit* m_addPassword = nullptr;
    QLabel* m_about = nullptr;
};

}  // namespace labdesk::ui
