#pragma once
// Small helpers so every page builds cards, tables, buttons and feedback
// the same way. Keeps the pages consistent and short.

#include <QLabel>
#include <QString>
#include <QStringList>
#include <QVariant>

class QFrame;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QVBoxLayout;
class QWidget;

namespace labdesk::ui {

// Card = white panel with border; returns the frame, layout via card->layout().
QFrame* makeCard(QWidget* parent = nullptr);
QVBoxLayout* cardLayout(QFrame* card);
QLabel* makeLabel(const QString& text, const QString& objectName = QString(), QWidget* parent = nullptr);
QPushButton* makeButton(const QString& text, const QString& variant = QString(), QWidget* parent = nullptr);

// Standard read-only, row-selecting table.
void setupTable(QTableWidget* table, const QStringList& headers);
QTableWidgetItem* makeItem(const QString& text, const QVariant& userData = QVariant());
QTableWidgetItem* makeStatusItem(const QString& status);
QTableWidgetItem* makeNumberItem(double value, int decimals = 0);
// Data stored with makeItem(..., userData) in column `column` of the selected row.
QVariant selectedRowData(const QTableWidget* table, int column = 0);

// Page skeleton: title + subtitle, returns the layout to add content to.
QVBoxLayout* makePageLayout(QWidget* page, const QString& title, const QString& subtitle);

// Puts a vertically scrolling area inside `page` and returns the body widget
// to build the page on. Content then never gets squashed at small window sizes.
QWidget* makeScrollPage(QWidget* page);

// Plain-language explanation box (light grey panel).
QLabel* makeExplainer(const QString& text);

// Small rounded status label ("Available 9") using the shared status colours.
QLabel* makeStatusChip(const QString& status, const QString& text);

// Empty / error state for a section. Use showEmptyState only after the query
// SUCCEEDED and returned no rows; use showErrorState when the query failed.
QLabel* makeStateMessage();
void showEmptyState(QLabel* label, const QString& text);
void showErrorState(QLabel* label, const QString& text);

// Re-applies the stylesheet after a dynamic property change.
void repolish(QWidget* widget);

bool confirm(QWidget* parent, const QString& title, const QString& text, const QString& acceptText);

// Inline notification. Each kind has its own tint, accent bar AND a leading
// word ("Success", "Note", "Warning", "Error"), so the type is not conveyed by
// colour alone. Success/info hide after a few seconds unless persistent;
// warnings and errors stay until the next message.
class MessageBanner : public QLabel {
public:
    explicit MessageBanner(QWidget* parent = nullptr);
    void showSuccess(const QString& text);
    void showInfo(const QString& text);
    void showWarning(const QString& text);
    void showError(const QString& text);
    void clearMessage();
    void setPersistent(bool persistent) { m_persistent = persistent; }

private:
    void showKind(const QString& kind, const QString& prefix, const QString& text, bool autoHide);
    int m_generation = 0;
    bool m_persistent = false;
};

}  // namespace labdesk::ui
