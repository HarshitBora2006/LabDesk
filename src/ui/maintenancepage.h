#pragma once

#include "page.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class MessageBanner;

class MaintenancePage : public Page {
    Q_OBJECT
public:
    MaintenancePage(Services& services, QWidget* parent = nullptr);
    void refresh() override;

private:
    void reloadComputerChoices();
    void onReport();
    void onEdit();
    void onResolve();
    void updateButtons();

    Services& m_services;
    QComboBox* m_computer = nullptr;
    QLineEdit* m_description = nullptr;
    QCheckBox* m_openOnly = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_empty = nullptr;
    QPushButton* m_edit = nullptr;
    QPushButton* m_resolve = nullptr;
    MessageBanner* m_banner = nullptr;
};

}  // namespace labdesk::ui
