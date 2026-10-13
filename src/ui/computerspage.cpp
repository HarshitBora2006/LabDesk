#include "computerspage.h"

#include "services.h"
#include "theme.h"
#include "uihelpers.h"

#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace labdesk::ui {

ComputersPage::ComputersPage(Services& services, QWidget* parent) : Page(parent), m_services(services)
{
    QVBoxLayout* root = makePageLayout(
        this, QStringLiteral("Computers"),
        QStringLiteral("Lab inventory. Status changes only through sessions and maintenance, never by hand."));
    m_banner = new MessageBanner();
    root->addWidget(m_banner);

    auto* body = new QHBoxLayout();
    body->setSpacing(12);

    QFrame* listCard = makeCard();
    QVBoxLayout* list = cardLayout(listCard);
    auto* filterRow = new QHBoxLayout();
    filterRow->addWidget(makeLabel(QStringLiteral("Show"), QStringLiteral("muted")));
    m_filter = new QComboBox();
    m_filter->addItem(QStringLiteral("All computers"), QString());
    m_filter->addItem(QStringLiteral("Available"), ComputerStatus::Available);
    m_filter->addItem(QStringLiteral("Occupied"), ComputerStatus::Occupied);
    m_filter->addItem(QStringLiteral("Under maintenance"), ComputerStatus::Maintenance);
    filterRow->addWidget(m_filter);
    filterRow->addStretch();
    list->addLayout(filterRow);
    m_table = new QTableWidget();
    setupTable(m_table, {QStringLiteral("Computer ID"), QStringLiteral("Name"), QStringLiteral("Location"),
                         QStringLiteral("Status"), QStringLiteral("In use by")});
    list->addWidget(m_table, 1);
    m_empty = makeLabel(QStringLiteral("No computers match this filter."), QStringLiteral("emptyState"));
    list->addWidget(m_empty);
    body->addWidget(listCard, 3);

    QFrame* formCard = makeCard();
    formCard->setMinimumWidth(320);
    formCard->setMaximumWidth(400);
    QVBoxLayout* formBox = cardLayout(formCard);
    m_formTitle = makeLabel(QStringLiteral("New computer"), QStringLiteral("sectionTitle"));
    formBox->addWidget(m_formTitle);
    auto* form = new QFormLayout();
    form->setVerticalSpacing(10);
    m_id = new QLineEdit();
    m_id->setPlaceholderText(QStringLiteral("e.g. LAB-C-07"));
    m_id->setMaxLength(20);
    m_name = new QLineEdit();
    m_name->setPlaceholderText(QStringLiteral("e.g. Workstation C7"));
    m_location = new QLineEdit();
    m_location->setPlaceholderText(QStringLiteral("e.g. Lab C, 2nd floor"));
    m_status = new QLabel(QStringLiteral("AVAILABLE when added"));
    m_status->setObjectName(QStringLiteral("muted"));
    form->addRow(QStringLiteral("Computer ID"), m_id);
    form->addRow(QStringLiteral("Name"), m_name);
    form->addRow(QStringLiteral("Location"), m_location);
    form->addRow(QStringLiteral("Status"), m_status);
    formBox->addLayout(form);

    m_add = makeButton(QStringLiteral("Add computer"), QStringLiteral("primary"));
    m_save = makeButton(QStringLiteral("Save changes"), QStringLiteral("primary"));
    m_delete = makeButton(QStringLiteral("Delete computer"), QStringLiteral("danger"));
    auto* clear = makeButton(QStringLiteral("New computer"));
    auto* buttons = new QHBoxLayout();
    buttons->addWidget(m_add);
    buttons->addWidget(m_save);
    buttons->addStretch();
    buttons->addWidget(clear);
    formBox->addLayout(buttons);
    formBox->addWidget(m_delete, 0, Qt::AlignLeft);
    formBox->addWidget(makeLabel(QStringLiteral("A computer that appears in any session or maintenance record "
                                                "cannot be deleted: the history must stay intact."),
                                 QStringLiteral("muted")));
    formBox->addStretch();
    body->addWidget(formCard, 2);
    root->addLayout(body, 1);

    connect(m_filter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { refresh(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ComputersPage::onSelectionChanged);
    connect(m_add, &QPushButton::clicked, this, &ComputersPage::onAdd);
    connect(m_save, &QPushButton::clicked, this, &ComputersPage::onSave);
    connect(m_delete, &QPushButton::clicked, this, &ComputersPage::onDelete);
    connect(clear, &QPushButton::clicked, this, &ComputersPage::clearForm);
    clearForm();
}

void ComputersPage::refresh()
{
    QString error;
    const auto computers = m_services.computers.listComputers(m_filter->currentData().toString(), &error);
    if (!error.isEmpty())
        m_banner->showError(QStringLiteral("Could not load computers: ") + error);

    QHash<QString, QString> usedBy;
    for (const SessionRecord& r : m_services.sessions.listSessions(true, 1000))
        usedBy.insert(r.computerId, QStringLiteral("%1 (%2)").arg(r.studentName, r.studentId));

    const QString keep = m_editingId;
    m_table->blockSignals(true);
    m_table->setRowCount(static_cast<int>(computers.size()));
    int reselect = -1;
    for (int row = 0; row < static_cast<int>(computers.size()); ++row) {
        const Computer& c = computers[static_cast<std::size_t>(row)];
        m_table->setItem(row, 0, makeItem(c.computerId, c.computerId));
        m_table->setItem(row, 1, makeItem(c.name));
        m_table->setItem(row, 2, makeItem(c.location));
        m_table->setItem(row, 3, makeStatusItem(c.status));
        m_table->setItem(row, 4, makeItem(usedBy.value(c.computerId, QStringLiteral("-"))));
        if (c.computerId == keep)
            reselect = row;
    }
    m_table->resizeColumnsToContents();
    if (reselect >= 0)
        m_table->selectRow(reselect);
    m_table->blockSignals(false);

    if (!keep.isEmpty()) {
        // Keep the read-only status label in sync even if the row is filtered out.
        if (const auto current = m_services.computers.findComputer(keep))
            m_status->setText(current->status);
        else
            clearForm();
    }
    m_empty->setVisible(computers.empty());
    m_table->setVisible(!computers.empty());
}

void ComputersPage::onSelectionChanged()
{
    const QString id = selectedRowData(m_table).toString();
    if (id.isEmpty())
        return;
    const auto computer = m_services.computers.findComputer(id);
    if (!computer) {
        m_banner->showError(QStringLiteral("Computer %1 no longer exists.").arg(id));
        return;
    }
    m_editingId = computer->computerId;
    m_id->setText(computer->computerId);
    m_id->setReadOnly(true);
    m_name->setText(computer->name);
    m_location->setText(computer->location);
    m_status->setText(computer->status);
    m_formTitle->setText(QStringLiteral("Edit %1").arg(computer->computerId));
    updateButtons();
}

void ComputersPage::clearForm()
{
    m_editingId.clear();
    m_table->clearSelection();
    m_id->clear();
    m_name->clear();
    m_location->clear();
    m_id->setReadOnly(false);
    m_status->setText(QStringLiteral("AVAILABLE when added"));
    m_formTitle->setText(QStringLiteral("New computer"));
    updateButtons();
}

void ComputersPage::updateButtons()
{
    const bool editing = !m_editingId.isEmpty();
    m_add->setVisible(!editing);
    m_save->setVisible(editing);
    m_delete->setEnabled(editing);
}

void ComputersPage::onAdd()
{
    const Computer c{m_id->text(), m_name->text(), m_location->text(), QString()};
    const QString invalid = ComputerService::validate(c);
    if (!invalid.isEmpty()) {
        m_banner->showError(invalid);
        return;
    }
    const OpResult r = m_services.computers.addComputer(c);
    if (!r.ok) {
        m_banner->showError(r.message);
        return;
    }
    m_banner->showSuccess(r.message);
    clearForm();
    emit dataChanged();
}

void ComputersPage::onSave()
{
    const OpResult r =
        m_services.computers.updateComputer({m_editingId, m_name->text(), m_location->text(), QString()});
    if (!r.ok) {
        m_banner->showError(r.message);
        return;
    }
    m_banner->showSuccess(r.message);
    emit dataChanged();
}

void ComputersPage::onDelete()
{
    if (m_editingId.isEmpty())
        return;
    if (!confirm(this, QStringLiteral("Delete computer"),
                 QStringLiteral("Delete computer %1 permanently? This cannot be undone.").arg(m_editingId),
                 QStringLiteral("Delete computer"))) {
        return;
    }
    const OpResult r = m_services.computers.deleteComputer(m_editingId);
    if (!r.ok) {
        m_banner->showError(r.message);
        return;
    }
    m_banner->showSuccess(r.message);
    clearForm();
    emit dataChanged();
}

}  // namespace labdesk::ui
