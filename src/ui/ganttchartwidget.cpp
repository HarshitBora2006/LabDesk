#include "ganttchartwidget.h"

#include "theme.h"

#include <QFontMetrics>
#include <QPainter>

#include <algorithm>

namespace labdesk::ui {

namespace {
constexpr int kMarginX = 14;
constexpr int kTop = 10;
constexpr int kBarHeight = 46;
constexpr int kMinUnitWidth = 9;  // px per time unit before we start scrolling
}  // namespace

GanttChartWidget::GanttChartWidget(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void GanttChartWidget::setSchedule(const os::ScheduleResult& result)
{
    m_slices = result.timeline;
    m_makespan = result.makespan;
    m_colorIndex.clear();
    int next = 0;
    for (const auto& p : result.processes)
        m_colorIndex.insert(QString::fromStdString(p.process.id), next++);
    setMinimumWidth(std::min(12000, 2 * kMarginX + m_makespan * kMinUnitWidth));
    updateGeometry();
    update();
}

void GanttChartWidget::clear()
{
    m_slices.clear();
    m_makespan = 0;
    setMinimumWidth(0);
    update();
}

QSize GanttChartWidget::sizeHint() const { return {600, kTop + kBarHeight + 34}; }
QSize GanttChartWidget::minimumSizeHint() const { return {200, kTop + kBarHeight + 34}; }

void GanttChartWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QFontMetrics fm(font());

    if (m_slices.empty() || m_makespan <= 0) {
        p.setPen(theme::TextMuted);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Run an algorithm to see the execution timeline."));
        return;
    }

    const double usable = std::max(1, width() - 2 * kMarginX);
    const double unit = usable / m_makespan;
    auto xAt = [&](int t) { return kMarginX + t * unit; };

    for (const auto& s : m_slices) {
        const QRectF r(xAt(s.start), kTop, (s.end - s.start) * unit, kBarHeight);
        if (s.idle) {
            p.fillRect(r, QColor("#F1F2F4"));
            p.fillRect(r, QBrush(QColor("#C9CED6"), Qt::BDiagPattern));
        } else {
            const QString id = QString::fromStdString(s.processId);
            p.fillRect(r, theme::processColor(m_colorIndex.value(id, 0)));
            const QString label = id;
            if (fm.horizontalAdvance(label) + 6 < r.width()) {
                p.setPen(Qt::white);
                p.drawText(r, Qt::AlignCenter, label);
            }
        }
        p.setPen(QPen(Qt::white, 1.5));
        p.drawLine(r.topLeft(), r.bottomLeft());
    }
    p.setPen(QPen(theme::Border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(kMarginX, kTop, m_makespan * unit, kBarHeight));

    // Time labels at every slice boundary, skipping ones that would overlap.
    p.setPen(theme::TextMuted);
    double lastRight = -1e9;
    auto drawTick = [&](int t, bool force) {
        const QString text = QString::number(t);
        const double x = xAt(t);
        const int w = fm.horizontalAdvance(text);
        if (!force && x - w / 2.0 < lastRight + 4)
            return;
        p.drawLine(QPointF(x, kTop + kBarHeight), QPointF(x, kTop + kBarHeight + 4));
        p.drawText(QRectF(x - w / 2.0 - 2, kTop + kBarHeight + 6, w + 4, fm.height()), Qt::AlignCenter, text);
        lastRight = x + w / 2.0;
    };
    drawTick(0, true);
    for (const auto& s : m_slices)
        drawTick(s.end, &s == &m_slices.back());
}

}  // namespace labdesk::ui
