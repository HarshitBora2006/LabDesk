#include "layouthelpers.h"

#include <QGridLayout>
#include <QResizeEvent>

namespace labdesk::ui {

// ---------------------------------------------------------------- FlowLayout

FlowLayout::FlowLayout(QWidget* parent, int margin, int horizontalSpacing, int verticalSpacing)
    : QLayout(parent), m_hSpace(horizontalSpacing), m_vSpace(verticalSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
    QLayoutItem* item = nullptr;
    while ((item = takeAt(0)) != nullptr)
        delete item;
}

void FlowLayout::addItem(QLayoutItem* item) { m_items.append(item); }
int FlowLayout::count() const { return static_cast<int>(m_items.size()); }
QLayoutItem* FlowLayout::itemAt(int index) const { return m_items.value(index, nullptr); }

QLayoutItem* FlowLayout::takeAt(int index)
{
    if (index < 0 || index >= m_items.size())
        return nullptr;
    return m_items.takeAt(index);
}

Qt::Orientations FlowLayout::expandingDirections() const { return {}; }
bool FlowLayout::hasHeightForWidth() const { return true; }
int FlowLayout::heightForWidth(int width) const { return doLayout(QRect(0, 0, width, 0), true); }
QSize FlowLayout::sizeHint() const { return minimumSize(); }

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : m_items)
        size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    const QMargins m = contentsMargins();
    const QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = area.x();
    int y = area.y();
    int lineHeight = 0;
    for (QLayoutItem* item : m_items) {
        if (item->isEmpty())
            continue;  // hidden widgets take no space
        const QSize hint = item->sizeHint();
        if (x > area.x() && x + hint.width() > area.right() + 1) {
            x = area.x();  // wrap to the next row
            y += lineHeight + m_vSpace;
            lineHeight = 0;
        }
        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), hint));
        x += hint.width() + m_hSpace;
        lineHeight = qMax(lineHeight, hint.height());
    }
    return y + lineHeight - rect.y() + m.bottom();
}

// ------------------------------------------------------------ ResponsiveGrid

ResponsiveGrid::ResponsiveGrid(int minimumColumnWidth, int spacing, QWidget* parent)
    : QWidget(parent), m_grid(new QGridLayout(this)), m_minimumColumnWidth(qMax(1, minimumColumnWidth))
{
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(spacing);
    m_grid->setVerticalSpacing(spacing);
}

void ResponsiveGrid::setWidgets(const QList<QWidget*>& widgets)
{
    for (QWidget* w : m_widgets) {
        m_grid->removeWidget(w);
        delete w;
    }
    m_widgets = widgets;
    for (QWidget* w : m_widgets)
        w->setParent(this);
    relayout(true);
    for (QWidget* w : m_widgets)
        w->show();
}

void ResponsiveGrid::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    relayout(false);
}

void ResponsiveGrid::relayout(bool force)
{
    const int spacing = m_grid->horizontalSpacing();
    const int fit = qMax(1, (width() + spacing) / (m_minimumColumnWidth + spacing));
    const int columns = qMax(1, qMin(fit, static_cast<int>(m_widgets.size())));
    if (!force && columns == m_columns)
        return;
    m_columns = columns;
    for (QWidget* w : m_widgets)
        m_grid->removeWidget(w);
    for (int c = 0; c < m_grid->columnCount(); ++c)
        m_grid->setColumnStretch(c, 0);
    for (int i = 0; i < m_widgets.size(); ++i)
        m_grid->addWidget(m_widgets.at(i), i / columns, i % columns);
    for (int c = 0; c < columns; ++c)
        m_grid->setColumnStretch(c, 1);  // equal-width columns
}

}  // namespace labdesk::ui
