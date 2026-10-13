#include "studentspage.h"

#include "services.h"
#include "uihelpers.h"

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

StudentsPage::StudentsPage(Services& services, QWidget* parent) : Page(parent), m_services(services)
{
    QVBoxLayout* root = makePageLayout(this, QStringLiteral("Students"),
                                       QStringLiteral("Register students who may sign in to the student portal."));
    m_banner = new MessageBanner();
    root->addWidget(m_banner);

    auto* body = new QHBoxLayout();
    body->setSpacing(12);

    QFrame* listCard = makeCard();
    QVBoxLayout* list = cardLayout(listCard);
    m_search = new QLineEdit();
    m_search->setPlaceholderText(QStringLiteral("Search by ID, name, email or department"));
    m_search->setClearButtonEnabled(true);
    list->addWidget(m_search);
    m_table = new QTableWidget();
    setupTable(m_table, {QStringLiteral("Student ID"), QStringLiteral("Name"), QStringLiteral("Email"),
                         QStringLiteral("Department"), QStringLiteral("Using computer")});
    list->addWidget(m_table, 1);
    m_empty = makeLabel(QStringLiteral("No students yet. Fill in the form to add the first one."),
                        QStringLiteral("emptyState"));
    list->addWidget(m_empty);
    body->addWidget(listCard, 3);

    QFrame* formCard = makeCard();
    formCard->setMinimumWidth(320);
    formCard->setMaximumWidth(400);
    QVBoxLayout* formBox = cardLayout(formCard);
    m_formTitle = makeLabel(QStringLiteral("New student"), QStringLiteral("sectionTitle"));
    formBox->addWidget(m_formTitle);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    form->setVerticalSpacing(10);
    m_id = new QLineEdit();
    m_id->setPlaceholderText(QStringLiteral("e.g. 22CSE104"));
    m_id->setMaxLength(20);
    m_name = new QLineEdit();
    m_email = new QLineEdit();
    m_email->setPlaceholderText(QStringLiteral("name@university.edu"));
    m_department = new QLineEdit();
    m_department->setPlaceholderText(QStringLiteral("e.g. Computer Science"));
    m_password = new QLineEdit();
    m_password->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Student ID"), m_id);
    form->addRow(QStringLiteral("Name"), m_name);
    form->addRow(QStringLiteral("Email"), m_email);
    form->addRow(QStringLiteral("Department"), m_department);
    form->addRow(QStringLiteral("Password"), m_password);
    formBox->addLayout(form);

    m_add = makeButton(QStringLiteral("Add student"), QStringLiteral("primary"));
    m_save = makeButton(QStringLiteral("Save changes"), QStringLiteral("primary"));
    m_delete = makeButton(QStringLiteral("Delete student"), QStringLiteral("danger"));
    auto* clear = makeButton(QStringLiteral("New student"));
    auto* buttons = new QHBoxLayout();
    buttons->addWidget(m_add);
    buttons->addWidget(m_save);
    buttons->addStretch();
    buttons->addWidget(clear);
    formBox->addLayout(buttons);
    formBox->addWidget(m_delete, 0, Qt::AlignLeft);
    formBox->addWidget(makeLabel(QStringLiteral("Students with session history cannot be deleted; their "
                                                "records are kept for auditing."),
                                 QStringLiteral("muted")));
    formBox->addStretch();
    body->addWidget(formCard, 2);
    root->addLayout(body, 1);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, &StudentsPage::onSelectionChanged);
    connect(m_search, &QLineEdit::textChanged, this, &StudentsPage::applyFilter);
    connect(m_add, &QPushButton::clicked, this, &StudentsPage::onAdd);
    connect(m_save, &QPushButton::clicked, this, &StudentsPage::onSave);
    connect(m_delete, &QPushButton::clicked, this, &StudentsPage::onDelete);
    connect(clear, &QPushButton::clicked, this, &StudentsPage::clearForm);
    clearForm();
}

void StudentsPage::refresh()
{
    QString error;
    const auto students = m_services.students.listStudents(&error);
    if (!error.isEmpty())
        m_banner->showError(QStringLiteral("Could not load students: ") + error);

    QHash<QString, QString> usingComputer;
    for (const SessionRecord& r : m_services.sessions.listSessions(true, 1000))
        usingComputer.insert(r.studentId, r.computerId);

    const QString keep = m_editingId;
    m_table->blockSignals(true);
    m_table->setRowCount(static_cast<int>(students.size()));
    int reselect = -1;
    for (int row = 0; row < static_cast<int>(students.size()); ++row) {
        const Student& s = students[static_cast<std::size_t>(row)];
        m_table->setItem(row, 0, makeItem(s.studentId, s.studentId));
        m_table->setItem(row, 1, makeItem(s.name));
        m_table->setItem(row, 2, makeItem(s.email));
        m_table->setItem(row, 3, makeItem(s.department));
        m_table->setItem(row, 4, makeItem(usingComputer.value(s.studentId, QStringLiteral("-"))));
        if (s.studentId == keep)
            reselect = row;
    }
    m_table->resizeColumnsToContents();
    if (reselect >= 0)
        m_table->selectRow(reselect);
    m_table->blockSignals(false);
    if (reselect < 0 && !keep.isEmpty())
        clearForm();  // the student being edited no longer exists

    m_empty->setVisible(students.empty());
    m_table->setVisible(!students.empty());
    applyFilter();
}

void StudentsPage::applyFilter()
{
    const QString needle = m_search->text().trimmed();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        bool match = needle.isEmpty();
        for (int col = 0; col < 4 && !match; ++col) {
            const QTableWidgetItem* item = m_table->item(row, col);
            match = item && item->text().contains(needle, Qt::CaseInsensitive);
        }
        m_table->setRowHidden(row, !match);
    }
}

void StudentsPage::onSelectionChanged()
{
    const QString id = selectedRowData(m_table).toString();
    if (id.isEmpty())
        return;
    QString error;
    const auto student = m_services.students.findStudent(id, &error);
    if (!student) {
        m_banner->showError(error.isEmpty() ? QStringLiteral("Student %1 no longer exists.").arg(id) : error);
        return;
    }
    m_editingId = student->studentId;
    m_id->setText(student->studentId);
    m_id->setReadOnly(true);
    m_name->setText(student->name);
    m_email->setText(student->email);
    m_department->setText(student->department);
    m_password->clear();
    m_password->setPlaceholderText(QStringLiteral("Leave blank to keep current password"));
    m_formTitle->setText(QStringLiteral("Edit %1").arg(student->studentId));
    updateButtons();
}

void StudentsPage::clearForm()
{
    m_editingId.clear();
    m_table->clearSelection();
    for (QLineEdit* e : {m_id, m_name, m_email, m_department, m_password})
        e->clear();
    m_id->setReadOnly(false);
    m_password->setPlaceholderText(QStringLiteral("At least 6 characters"));
    m_formTitle->setText(QStringLiteral("New student"));
    updateButtons();
}

void StudentsPage::updateButtons()
{
    const bool editing = !m_editingId.isEmpty();
    m_add->setVisible(!editing);
    m_save->setVisible(editing);
    m_delete->setEnabled(editing);
}

void StudentsPage::onAdd()
{
    const Student s{m_id->text(), m_name->text(), m_email->text(), m_department->text()};
    const QString invalid = StudentService::validate(s, true, m_password->text());
    if (!invalid.isEmpty()) {
        m_banner->showError(invalid);
        return;
    }
    const OpResult r = m_services.students.addStudent(s, m_password->text());
    if (!r.ok) {
        m_banner->showError(r.message);
        return;
    }
    m_banner->showSuccess(r.message);
    clearForm();
    emit dataChanged();
}

void StudentsPage::onSave()
{
    const Student s{m_editingId, m_name->text(), m_email->text(), m_department->text()};
    const OpResult r = m_services.students.updateStudent(s, m_password->text());
    if (!r.ok) {
        m_banner->showError(r.message);
        return;
    }
    m_banner->showSuccess(r.message);
    m_password->clear();
    emit dataChanged();
}

void StudentsPage::onDelete()
{
    if (m_editingId.isEmpty())
        return;
    if (!confirm(this, QStringLiteral("Delete student"),
                 QStringLiteral("Delete student %1 permanently? This cannot be undone.").arg(m_editingId),
                 QStringLiteral("Delete student"))) {
        return;
    }
    const OpResult r = m_services.students.deleteStudent(m_editingId);
    if (!r.ok) {
        m_banner->showError(r.message);
        return;
    }
    m_banner->showSuccess(r.message);
    clearForm();
    emit dataChanged();
}

}  // namespace labdesk::ui
