#include "uihelpers.h"

#include "theme.h"

#include <QFont>
#include <QFrame>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace labdesk::ui {

QFrame* makeCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    return card;
}

QVBoxLayout* cardLayout(QFrame* card)
{
    return qobject_cast<QVBoxLayout*>(card->layout());
}

QLabel* makeLabel(const QString& text, const QString& objectName, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    if (!objectName.isEmpty())
        label->setObjectName(objectName);
    label->setWordWrap(true);
    return label;
}

QPushButton* makeButton(const QString& text, const QString& variant, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    if (!variant.isEmpty())
        button->setProperty("variant", variant);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

void setupTable(QTableWidget* table, const QStringList& headers)
{
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(32);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(true);
}

QTableWidgetItem* makeItem(const QString& text, const QVariant& userData)
{
    auto* item = new QTableWidgetItem(text);
    if (userData.isValid())
        item->setData(Qt::UserRole, userData);
    item->setToolTip(text);
    return item;
}

QTableWidgetItem* makeStatusItem(const QString& status)
{
    auto* item = new QTableWidgetItem(status);
    const theme::StatusColors c = theme::statusColors(status);
    item->setBackground(c.background);
    item->setForeground(c.foreground);
    QFont f = item->font();
    f.setBold(true);
    item->setFont(f);
    item->setTextAlignment(Qt::AlignCenter);
    return item;
}

QTableWidgetItem* makeNumberItem(double value, int decimals)
{
    auto* item = new QTableWidgetItem(QString::number(value, 'f', decimals));
    item->setData(Qt::UserRole, value);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

QVariant selectedRowData(const QTableWidget* table, int column)
{
    const QList<QTableWidgetItem*> selected = table->selectedItems();
    if (selected.isEmpty())
        return QVariant();
    const QTableWidgetItem* item = table->item(selected.first()->row(), column);
    return item ? item->data(Qt::UserRole) : QVariant();
}

QVBoxLayout* makePageLayout(QWidget* page, const QString& title, const QString& subtitle)
{
    page->setObjectName(QStringLiteral("pageRoot"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 22, 28, 22);
    layout->setSpacing(14);
    auto* header = new QVBoxLayout();
    header->setSpacing(2);
    header->addWidget(makeLabel(title, QStringLiteral("pageTitle")));
    if (!subtitle.isEmpty())
        header->addWidget(makeLabel(subtitle, QStringLiteral("pageSubtitle")));
    layout->addLayout(header);
    return layout;
}

bool confirm(QWidget* parent, const QString& title, const QString& text, const QString& acceptText)
{
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton, parent);
    QPushButton* accept = box.addButton(acceptText, QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(box.buttons().last()));
    box.exec();
    return box.clickedButton() == accept;
}

// ------------------------------------------------------------ MessageBanner

MessageBanner::MessageBanner(QWidget* parent) : QLabel(parent)
{
    setObjectName(QStringLiteral("banner"));
    setWordWrap(true);
    setTextInteractionFlags(Qt::TextSelectableByMouse);
    hide();
}

void MessageBanner::showKind(const QString& kind, const QString& text, bool autoHide)
{
    setProperty("kind", kind);
    style()->unpolish(this);
    style()->polish(this);
    setText(text);
    show();
    const int generation = ++m_generation;
    if (autoHide) {
        QTimer::singleShot(7000, this, [this, generation]() {
            if (generation == m_generation)
                hide();
        });
    }
}

void MessageBanner::showSuccess(const QString& text) { showKind(QStringLiteral("success"), text, true); }
void MessageBanner::showError(const QString& text) { showKind(QStringLiteral("error"), text, false); }
void MessageBanner::showInfo(const QString& text) { showKind(QStringLiteral("info"), text, true); }

void MessageBanner::clearMessage()
{
    ++m_generation;
    hide();
}

}  // namespace labdesk::ui
