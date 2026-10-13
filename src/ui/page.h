#pragma once
// Base class for every screen. refresh() re-reads the database; dataChanged()
// tells MainWindow that this page modified the database so every other page
// (admin and student views alike) refreshes from the same source of truth.

#include <QWidget>

namespace labdesk::ui {

class Page : public QWidget {
    Q_OBJECT
public:
    explicit Page(QWidget* parent = nullptr) : QWidget(parent) {}
    ~Page() override = default;

    virtual void refresh() {}

signals:
    void dataChanged();
};

}  // namespace labdesk::ui
