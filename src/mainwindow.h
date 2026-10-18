#pragma once

#include <QMainWindow>

#include <memory>
#include <vector>

class QListWidget;
class QStackedWidget;

namespace labdesk {

class Database;
struct Services;

namespace ui {
class LoginPage;
class Page;
}

// Starts on the login screen. After authentication it builds a role-specific
// shell (navy sidebar + pages): administrators and students see different
// pages, and logging out destroys the shell so nothing from one user's session
// remains for the next. When any page changes the database, every page of the
// shell re-reads it, so all views show the same committed state.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Database& db, QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    enum class Role { Admin, Student };

    void startSession(Role role, const QString& userId, const QString& name);
    void logout();
    void buildAdminPages(const QString& adminId);
    void buildStudentPages(const QString& studentId);
    void addSection(const QString& title);
    void addPage(const QString& title, ui::Page* page);
    void refreshAll();
    void onNavigation(int row);
    void showPage(QWidget* page);

    Database& m_db;
    std::unique_ptr<Services> m_services;
    QStackedWidget* m_root = nullptr;
    ui::LoginPage* m_login = nullptr;
    QWidget* m_shell = nullptr;
    QListWidget* m_nav = nullptr;
    QStackedWidget* m_stack = nullptr;
    std::vector<ui::Page*> m_pages;
};

}  // namespace labdesk
