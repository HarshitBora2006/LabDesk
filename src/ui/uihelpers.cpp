#include "uihelpers.h"

#include "theme.h"

#include <QFont>
#include <QFrame>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
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

void alignNumericColumns(QTableWidget* table, std::initializer_list<int> columns)
{
    for (int c : columns) {
        if (QTableWidgetItem* header = table->horizontalHeaderItem(c))
            header->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}

QVBoxLayout* makePageLayout(QWidget* page, const QString& title, const QString& subtitle)
{
    page->setObjectName(QStringLiteral("pageRoot"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 26, 32, 28);
    layout->setSpacing(16);
    auto* header = new QVBoxLayout();
    header->setSpacing(2);
    header->addWidget(makeLabel(title, QStringLiteral("pageTitle")));
    if (!subtitle.isEmpty())
        header->addWidget(makeLabel(subtitle, QStringLiteral("pageSubtitle")));
    layout->addLayout(header);
    return layout;
}

QWidget* makeScrollPage(QWidget* page)
{
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* scroll = new QScrollArea();
    scroll->setObjectName(QStringLiteral("pageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget();
    body->setObjectName(QStringLiteral("pageScrollBody"));
    body->setAttribute(Qt::WA_StyledBackground, true);
    scroll->setWidget(body);
    outer->addWidget(scroll);
    return body;
}

QLabel* makeExplainer(const QString& text)
{
    auto* label = makeLabel(text, QStringLiteral("explainer"));
    label->setTextFormat(Qt::PlainText);
    return label;
}

QLabel* makeStatusChip(const QString& status, const QString& text)
{
    const theme::StatusColors c = theme::statusColors(status);
    auto* chip = new QLabel(text);
    chip->setStyleSheet(QStringLiteral("background:%1; color:%2; border-radius:10px; padding:3px 10px; "
                                       "font-weight:600;")
                            .arg(c.background.name(), c.foreground.name()));
    chip->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return chip;
}

QLabel* makeStateMessage()
{
    auto* label = new QLabel();
    label->setObjectName(QStringLiteral("stateMessage"));
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    label->hide();
    return label;
}

void repolish(QWidget* widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void showEmptyState(QLabel* label, const QString& text)
{
    label->setProperty("kind", QStringLiteral("empty"));
    repolish(label);
    label->setText(text);
    label->show();
}

void showErrorState(QLabel* label, const QString& text)
{
    label->setProperty("kind", QStringLiteral("error"));
    repolish(label);
    label->setText(QStringLiteral("Could not load this section from the database.\n") + text);
    label->show();
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
    setTextFormat(Qt::RichText);
    setTextInteractionFlags(Qt::TextSelectableByMouse);
    hide();
}

void MessageBanner::showKind(const QString& kind, const QString& prefix, const QString& text, bool autoHide)
{
    setProperty("kind", kind);
    repolish(this);
    QString body = text.toHtmlEscaped();
    body.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    setText(QStringLiteral("<b>%1</b>&nbsp;&nbsp;%2").arg(prefix, body));
    show();
    const int generation = ++m_generation;
    if (autoHide && !m_persistent) {
        QTimer::singleShot(8000, this, [this, generation]() {
            if (generation == m_generation)
                hide();
        });
    }
}

void MessageBanner::showSuccess(const QString& text) { showKind(QStringLiteral("success"), QStringLiteral("Success"), text, true); }
void MessageBanner::showInfo(const QString& text) { showKind(QStringLiteral("info"), QStringLiteral("Note"), text, true); }
void MessageBanner::showWarning(const QString& text) { showKind(QStringLiteral("warning"), QStringLiteral("Warning"), text, false); }
void MessageBanner::showError(const QString& text) { showKind(QStringLiteral("error"), QStringLiteral("Error"), text, false); }

void MessageBanner::clearMessage()
{
    ++m_generation;
    hide();
}

}  // namespace labdesk::ui
