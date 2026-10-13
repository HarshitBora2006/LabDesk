#pragma once

#include "page.h"

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

class ComputersPage : public Page {
    Q_OBJECT
public:
    ComputersPage(Services& services, QWidget* parent = nullptr);
    void refresh() override;

private:
    void onSelectionChanged();
    void onAdd();
    void onSave();
    void onDelete();
    void clearForm();
    void updateButtons();

    Services& m_services;
    QComboBox* m_filter = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_empty = nullptr;
    QLabel* m_formTitle = nullptr;
    QLineEdit* m_id = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_location = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_add = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_delete = nullptr;
    MessageBanner* m_banner = nullptr;
    QString m_editingId;
};

}  // namespace labdesk::ui
