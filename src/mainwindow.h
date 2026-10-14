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
class Page;
}

// Hosts the sidebar navigation and every page. When any page reports that it
// changed the database, every page re-reads it, so administrator and student
// views always reflect the same committed state.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Database& db, QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void addSection(const QString& title);
    void addPage(const QString& title, ui::Page* page);
    void refreshAll();
    void onNavigation(int row);
    void showPage(QWidget* page);

    Database& m_db;
    std::unique_ptr<Services> m_services;
    QListWidget* m_nav = nullptr;
    QStackedWidget* m_stack = nullptr;
    std::vector<ui::Page*> m_pages;
};

}  // namespace labdesk
