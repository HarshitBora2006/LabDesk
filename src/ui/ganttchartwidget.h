#pragma once
// Paints a ScheduleResult timeline as a Gantt chart. Idle CPU time is hatched.

#include "scheduler.h"

#include <QHash>
#include <QWidget>

namespace labdesk::ui {

class GanttChartWidget : public QWidget {
public:
    explicit GanttChartWidget(QWidget* parent = nullptr);

    void setSchedule(const os::ScheduleResult& result);
    void clear();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<os::TimelineSlice> m_slices;
    QHash<QString, int> m_colorIndex;
    int m_makespan = 0;
};

}  // namespace labdesk::ui
