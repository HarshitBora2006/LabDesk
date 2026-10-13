#pragma once

#include "page.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class MessageBanner;

class StudentsPage : public Page {
    Q_OBJECT
public:
    StudentsPage(Services& services, QWidget* parent = nullptr);
    void refresh() override;

private:
    void onSelectionChanged();
    void onAdd();
    void onSave();
    void onDelete();
    void clearForm();
    void applyFilter();
    void updateButtons();

    Services& m_services;
    QLineEdit* m_search = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_empty = nullptr;
    QLabel* m_formTitle = nullptr;
    QLineEdit* m_id = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_email = nullptr;
    QLineEdit* m_department = nullptr;
    QLineEdit* m_password = nullptr;
    QPushButton* m_add = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_delete = nullptr;
    MessageBanner* m_banner = nullptr;
    QString m_editingId;  // empty = creating a new student
};

}  // namespace labdesk::ui
