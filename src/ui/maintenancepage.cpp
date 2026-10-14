#include "maintenancepage.h"

#include "services.h"
#include "timeutil.h"
#include "uihelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace labdesk::ui {

MaintenancePage::MaintenancePage(Services& services, QWidget* parent) : Page(parent), m_services(services)
{
    QVBoxLayout* root = makePageLayout(
        this, QStringLiteral("Maintenance"),
        QStringLiteral("Reporting an issue takes a computer out of service. It returns to AVAILABLE only when "
                       "its last open issue is resolved."));
    m_banner = new MessageBanner();
    root->addWidget(m_banner);

    QFrame* reportCard = makeCard();
    QVBoxLayout* report = cardLayout(reportCard);
    report->addWidget(makeLabel(QStringLiteral("Report an issue"), QStringLiteral("sectionTitle")));
    auto* row = new QHBoxLayout();
    m_computer = new QComboBox();
    m_computer->setMinimumWidth(260);
    m_description = new QLineEdit();
    m_description->setPlaceholderText(QStringLiteral("What is wrong? e.g. Monitor flickers after 10 minutes"));
    m_description->setMaxLength(500);
    auto* reportButton = makeButton(QStringLiteral("Report issue"), QStringLiteral("primary"));
    row->addWidget(m_computer);
    row->addWidget(m_description, 1);
    row->addWidget(reportButton);
    report->addLayout(row);
    report->addWidget(makeLabel(QStringLiteral("Computers in use are listed but cannot be chosen: end the "
                                               "session first."),
                                QStringLiteral("muted")));
    root->addWidget(reportCard);

    QFrame* historyCard = makeCard();
    QVBoxLayout* history = cardLayout(historyCard);
    auto* header = new QHBoxLayout();
    header->addWidget(makeLabel(QStringLiteral("Maintenance history"), QStringLiteral("sectionTitle")));
    header->addStretch();
    m_openOnly = new QCheckBox(QStringLiteral("Open issues only"));
    header->addWidget(m_openOnly);
    history->addLayout(header);
    m_table = new QTableWidget();
    setupTable(m_table, {QStringLiteral("Issue"), QStringLiteral("Computer"), QStringLiteral("Description"),
                         QStringLiteral("Reported"), QStringLiteral("Resolved"), QStringLiteral("Status")});
    history->addWidget(m_table, 1);
    m_empty = makeStateMessage();
    history->addWidget(m_empty);
    auto* actions = new QHBoxLayout();
    m_edit = makeButton(QStringLiteral("Edit description"));
    m_resolve = makeButton(QStringLiteral("Mark resolved"), QStringLiteral("primary"));
    actions->addWidget(m_edit);
    actions->addStretch();
    actions->addWidget(m_resolve);
    history->addLayout(actions);
    root->addWidget(historyCard, 1);

    connect(reportButton, &QPushButton::clicked, this, &MaintenancePage::onReport);
    connect(m_description, &QLineEdit::returnPressed, this, &MaintenancePage::onReport);
    connect(m_openOnly, &QCheckBox::toggled, this, [this]() { refresh(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &MaintenancePage::updateButtons);
    connect(m_edit, &QPushButton::clicked, this, &MaintenancePage::onEdit);
    connect(m_resolve, &QPushButton::clicked, this, &MaintenancePage::onResolve);
    updateButtons();
}

void MaintenancePage::reloadComputerChoices()
{
    const QString previous = m_computer->currentData().toString();
    m_computer->blockSignals(true);
    m_computer->clear();
    QString error;
    const auto computers = m_services.computers.listComputers(QString(), &error);
    if (!error.isEmpty())
        m_banner->showError(QStringLiteral("The computer list could not be loaded: ") + error);
    auto* model = qobject_cast<QStandardItemModel*>(m_computer->model());
    for (const Computer& c : computers) {
        QString label = QStringLiteral("%1 (%2)").arg(c.computerId, c.status.toLower());
        if (c.status == ComputerStatus::Occupied)
            label = QStringLiteral("%1 (in use)").arg(c.computerId);
        m_computer->addItem(label, c.computerId);
        if (c.status == ComputerStatus::Occupied && model) {
            if (QStandardItem* item = model->item(m_computer->count() - 1))
                item->setEnabled(false);
        }
    }
    const int index = m_computer->findData(previous);
    if (index >= 0)
        m_computer->setCurrentIndex(index);
    if (computers.empty() && error.isEmpty())
        m_computer->addItem(QStringLiteral("No computers registered yet"), QString());
    m_computer->blockSignals(false);
}

void MaintenancePage::refresh()
{
    reloadComputerChoices();

    QString error;
    const auto records = m_services.maintenance.listRecords(m_openOnly->isChecked(), &error);

    const qlonglong selected = selectedRowData(m_table).toLongLong();
    m_table->blockSignals(true);
    m_table->setRowCount(static_cast<int>(records.size()));
    int reselect = -1;
    for (int row = 0; row < static_cast<int>(records.size()); ++row) {
        const MaintenanceRecord& m = records[static_cast<std::size_t>(row)];
        m_table->setItem(row, 0, makeItem(QStringLiteral("#%1").arg(m.maintenanceId), m.maintenanceId));
        m_table->setItem(row, 1, makeItem(m.computerId));
        m_table->setItem(row, 2, makeItem(m.description));
        m_table->setItem(row, 3, makeItem(timeutil::toDisplay(m.reportedAt)));
        m_table->setItem(row, 4, makeItem(timeutil::toDisplay(m.resolvedAt)));
        m_table->setItem(row, 5, makeStatusItem(m.status));
        if (m.maintenanceId == selected)
            reselect = row;
    }
    m_table->resizeColumnsToContents();
    if (reselect >= 0)
        m_table->selectRow(reselect);
    m_table->blockSignals(false);
    if (!error.isEmpty()) {
        m_table->hide();
        showErrorState(m_empty, error);
    } else if (records.empty()) {
        m_table->hide();
        showEmptyState(m_empty, m_openOnly->isChecked()
                                    ? QStringLiteral("No active maintenance issues. Every computer is in service.")
                                    : QStringLiteral("No maintenance issues have been recorded yet. Use \"Report "
                                                     "issue\" above when a computer needs repair."));
    } else {
        m_table->show();
        m_empty->hide();
    }
    updateButtons();
}

void MaintenancePage::updateButtons()
{
    const bool hasSelection = selectedRowData(m_table).isValid();
    bool open = false;
    if (hasSelection) {
        const int row = m_table->selectedItems().first()->row();
        open = m_table->item(row, 5) && m_table->item(row, 5)->text() == MaintenanceStatus::Open;
    }
    m_edit->setEnabled(hasSelection);
    m_resolve->setEnabled(open);
}

void MaintenancePage::onReport()
{
    const QString computerId = m_computer->currentData().toString();
    if (computerId.isEmpty()) {
        m_banner->showWarning(QStringLiteral("Choose a computer before reporting an issue."));
        return;
    }
    const OpResult r = m_services.maintenance.reportIssue(computerId, m_description->text());
    if (!r.ok) {
        m_banner->showError(r.message);
        return;
    }
    m_banner->showSuccess(r.message);
    m_description->clear();
    emit dataChanged();
}

void MaintenancePage::onEdit()
{
    const QVariant id = selectedRowData(m_table);
    if (!id.isValid())
        return;
    const int row = m_table->selectedItems().first()->row();
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(this, QStringLiteral("Edit description"),
                                                        QStringLiteral("Description"),
                                                        m_table->item(row, 2)->text(), &ok);
    if (!ok)
        return;
    const OpResult r = m_services.maintenance.updateDescription(id.toLongLong(), text);
    if (!r.ok) {
        m_banner->showError(r.message);
        return;
    }
    m_banner->showSuccess(r.message);
    emit dataChanged();
}

void MaintenancePage::onResolve()
{
    const QVariant id = selectedRowData(m_table);
    if (!id.isValid())
        return;
    if (!confirm(this, QStringLiteral("Resolve issue"),
                 QStringLiteral("Mark issue #%1 as resolved? The computer is released only if no other open "
                                "issue or active session remains.")
                     .arg(id.toLongLong()),
                 QStringLiteral("Mark resolved"))) {
        return;
    }
    const OpResult r = m_services.maintenance.resolveIssue(id.toLongLong());
    if (!r.ok) {
        m_banner->showError(r.message);
        return;
    }
    m_banner->showSuccess(r.message);
    emit dataChanged();
}

}  // namespace labdesk::ui
