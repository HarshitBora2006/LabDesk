#pragma once
// Student dashboard: welcome, the student's current session (or queue
// position) and the status of every lab computer. Read-only; actions are on
// "My Session".

#include "page.h"

class QLabel;
class QPushButton;

namespace labdesk {
struct Services;
}

namespace labdesk::ui {

class MessageBanner;
class ResponsiveGrid;

class StudentHomePage : public Page {
    Q_OBJECT
public:
    StudentHomePage(Services& services, const QString& studentId, QWidget* parent = nullptr);
    void refresh() override;

signals:
    void openMySession();

private:
    Services& m_services;
    QString m_studentId;
    QLabel* m_welcome = nullptr;
    QLabel* m_subtitle = nullptr;
    QLabel* m_visitTitle = nullptr;
    QLabel* m_visitDetails = nullptr;
    QPushButton* m_visitAction = nullptr;
    QLabel* m_counts = nullptr;
    QWidget* m_tiles = nullptr;
    QLabel* m_tilesState = nullptr;
};

}  // namespace labdesk::ui
