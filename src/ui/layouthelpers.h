#pragma once
// Layout helpers that keep cards aligned and evenly spaced at any window size,
// without fixed pixel positions.
//
// FlowLayout     - places items left-to-right and wraps them to the next row,
//                  with equal horizontal and vertical spacing (height-for-width).
// ResponsiveGrid - a grid of equal-width columns; the number of columns is
//                  recomputed from the available width whenever it is resized.

#include <QLayout>
#include <QList>
#include <QWidget>

class QGridLayout;

namespace labdesk::ui {

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int margin = 0, int horizontalSpacing = 8,
                        int verticalSpacing = 8);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;

    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    QSize minimumSize() const override;
    QSize sizeHint() const override;
    void setGeometry(const QRect& rect) override;

private:
    int doLayout(const QRect& rect, bool testOnly) const;

    QList<QLayoutItem*> m_items;
    int m_hSpace;
    int m_vSpace;
};

class ResponsiveGrid : public QWidget {
public:
    explicit ResponsiveGrid(int minimumColumnWidth, int spacing = 14, QWidget* parent = nullptr);

    // Replaces (and deletes) the current widgets. The grid takes ownership.
    void setWidgets(const QList<QWidget*>& widgets);
    int columns() const { return m_columns; }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void relayout(bool force);

    QGridLayout* m_grid;
    QList<QWidget*> m_widgets;
    int m_minimumColumnWidth;
    int m_columns = 0;
};

}  // namespace labdesk::ui
