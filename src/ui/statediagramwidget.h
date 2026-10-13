#pragma once
// Draws the five-state process diagram and highlights the current state and
// the transitions that are legal from it.

#include "processstate.h"

#include <QWidget>

#include <optional>

namespace labdesk::ui {

class StateDiagramWidget : public QWidget {
public:
    explicit StateDiagramWidget(QWidget* parent = nullptr);
    void setCurrentState(std::optional<os::ProcessState> state);

    QSize sizeHint() const override { return {560, 250}; }
    QSize minimumSizeHint() const override { return {420, 210}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::optional<os::ProcessState> m_state;
};

}  // namespace labdesk::ui
