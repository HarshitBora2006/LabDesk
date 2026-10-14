#include "statediagramwidget.h"

#include "theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <cmath>

namespace labdesk::ui {

namespace {

using S = os::ProcessState;

void drawArrow(QPainter& p, QPointF from, QPointF to, const QColor& color, const QString& label, QPointF labelOffset)
{
    p.setPen(QPen(color, 1.6));
    p.drawLine(from, to);
    const double angle = std::atan2(to.y() - from.y(), to.x() - from.x());
    const double size = 9.0;
    QPolygonF head;
    head << to << QPointF(to.x() - size * std::cos(angle - 0.45), to.y() - size * std::sin(angle - 0.45))
         << QPointF(to.x() - size * std::cos(angle + 0.45), to.y() - size * std::sin(angle + 0.45));
    p.setBrush(color);
    p.drawPolygon(head);
    p.setBrush(Qt::NoBrush);
    const QPointF mid = (from + to) / 2.0 + labelOffset;
    p.setPen(color == theme::Primary ? theme::Primary : theme::TextMuted);
    p.drawText(QRectF(mid.x() - 60, mid.y() - 10, 120, 20), Qt::AlignCenter, label);
}

}  // namespace

StateDiagramWidget::StateDiagramWidget(QWidget* parent) : QWidget(parent) {}

void StateDiagramWidget::setCurrentState(std::optional<os::ProcessState> state)
{
    m_state = state;
    update();
}

void StateDiagramWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const double w = width();
    const double h = height();
    const double hw = std::min(58.0, w * 0.085);  // node half-width
    const double hh = 19.0;                       // node half-height
    const double topY = h * 0.30;
    const double bottomY = h * 0.78;

    struct Node {
        S state;
        QPointF centre;
    };
    const Node nodes[] = {
        {S::New, {w * 0.10, topY}},       {S::Ready, {w * 0.36, topY}},  {S::Running, {w * 0.64, topY}},
        {S::Terminated, {w * 0.90, topY}}, {S::Waiting, {w * 0.50, bottomY}},
    };
    auto centre = [&](S s) {
        for (const Node& n : nodes)
            if (n.state == s)
                return n.centre;
        return QPointF();
    };

    auto edgeColor = [&](S from, S to) {
        return (m_state && *m_state == from && os::isValidTransition(from, to)) ? theme::Primary : QColor("#AEB5C1");
    };

    const QPointF nw = centre(S::New), rd = centre(S::Ready), rn = centre(S::Running), tm = centre(S::Terminated),
                  wt = centre(S::Waiting);
    drawArrow(p, {nw.x() + hw, nw.y()}, {rd.x() - hw, rd.y()}, edgeColor(S::New, S::Ready), QStringLiteral("admit"),
              {0, -14});
    drawArrow(p, {rd.x() + hw, rd.y() - 8}, {rn.x() - hw, rn.y() - 8}, edgeColor(S::Ready, S::Running),
              QStringLiteral("dispatch"), {0, -14});
    drawArrow(p, {rn.x() - hw, rn.y() + 8}, {rd.x() + hw, rd.y() + 8}, edgeColor(S::Running, S::Ready),
              QStringLiteral("preempt"), {0, 14});
    drawArrow(p, {rn.x() + hw, rn.y()}, {tm.x() - hw, tm.y()}, edgeColor(S::Running, S::Terminated),
              QStringLiteral("terminate"), {0, -14});
    drawArrow(p, {rn.x(), rn.y() + hh}, {wt.x() + hw * 0.6, wt.y() - hh}, edgeColor(S::Running, S::Waiting),
              QStringLiteral("block for I/O"), {58, 0});
    drawArrow(p, {wt.x() - hw * 0.6, wt.y() - hh}, {rd.x(), rd.y() + hh}, edgeColor(S::Waiting, S::Ready),
              QStringLiteral("complete I/O"), {-46, 0});

    QFont bold = font();
    bold.setBold(true);
    p.setFont(bold);
    for (const Node& n : nodes) {
        const QRectF r(n.centre.x() - hw, n.centre.y() - hh, 2 * hw, 2 * hh);
        const bool current = m_state && *m_state == n.state;
        const bool next = m_state && os::isValidTransition(*m_state, n.state);
        QColor fill = theme::Panel;
        QColor text = theme::Text;
        QPen border(QColor("#C9CED6"), 1.2);
        if (current) {
            fill = theme::Primary;
            text = Qt::white;
            border = QPen(theme::Primary, 1.2);
        } else if (next) {
            fill = theme::PrimarySoft;
            border = QPen(theme::Primary, 2.0);
        }
        p.setPen(border);
        p.setBrush(fill);
        p.drawRoundedRect(r, 8, 8);
        p.setPen(text);
        p.drawText(r, Qt::AlignCenter, QString::fromLatin1(os::toString(n.state)));
    }
}

}  // namespace labdesk::ui
