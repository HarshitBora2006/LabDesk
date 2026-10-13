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

bool confirm(QWidget* parent, const QString& title, const QString& text, const QString& acceptText);

// Inline feedback line. Success/info messages clear themselves; errors stay
// until the next message so they are not missed.
class MessageBanner : public QLabel {
public:
    explicit MessageBanner(QWidget* parent = nullptr);
    void showSuccess(const QString& text);
    void showError(const QString& text);
    void showInfo(const QString& text);
    void clearMessage();

private:
    void showKind(const QString& kind, const QString& text, bool autoHide);
    int m_generation = 0;
};

}  // namespace labdesk::ui
