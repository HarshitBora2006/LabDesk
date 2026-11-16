#include "bankerspage.h"

#include "uihelpers.h"

#include <QColor>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

namespace labdesk::ui {

namespace {

class CountDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        auto* box = new QSpinBox(parent);
        box->setRange(0, 99);
        box->setFrame(false);
        return box;
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        static_cast<QSpinBox*>(editor)->setValue(index.data(Qt::EditRole).toInt());
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
    {
        auto* box = static_cast<QSpinBox*>(editor);
        box->interpretText();
        model->setData(index, box->value(), Qt::EditRole);
    }
};

QString resourceName(int j) { return QString(QLatin1Char(static_cast<char>('A' + j))); }

QString vectorText(const os::Vector& v)
{
    QStringList parts;
    for (int x : v)
        parts << QString::number(x);
    return QStringLiteral("(") + parts.join(QStringLiteral(", ")) + QStringLiteral(")");
}

QTableWidget* matrixTable(bool editable, QObject* delegateParent)
{
    auto* t = new QTableWidget();
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setAlternatingRowColors(true);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->verticalHeader()->setDefaultSectionSize(30);
    t->setEditTriggers(editable ? (QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                                   | QAbstractItemView::AnyKeyPressed)
                                : QAbstractItemView::NoEditTriggers);
    if (editable)
        t->setItemDelegate(new CountDelegate(delegateParent));
    return t;
}

QFrame* titledCard(const QString& title, const QString& caption, QWidget* content)
{
    QFrame* card = makeCard();
    cardLayout(card)->addWidget(makeLabel(title, QStringLiteral("sectionTitle")));
    if (!caption.isEmpty())
        cardLayout(card)->addWidget(makeLabel(caption, QStringLiteral("caption")));
    cardLayout(card)->addWidget(content);
    return card;
}

}  // namespace

BankersPage::BankersPage(QWidget* parent) : Page(parent)
{
    QWidget* body = makeScrollPage(this);
    QVBoxLayout* root = makePageLayout(body, QStringLiteral("Deadlock Avoidance"),
                                       QStringLiteral("Banker's Algorithm"));
    m_banner = new MessageBanner();
    root->addWidget(m_banner);

    // size controls
    auto* controls = new QHBoxLayout();
    controls->setSpacing(10);
    controls->addWidget(makeLabel(QStringLiteral("Processes"), QStringLiteral("fieldLabel")));
    m_processCount = new QSpinBox();
    m_processCount->setRange(1, 10);
    controls->addWidget(m_processCount);
    controls->addSpacing(8);
    controls->addWidget(makeLabel(QStringLiteral("Resource types"), QStringLiteral("fieldLabel")));
    m_resourceCount = new QSpinBox();
    m_resourceCount->setRange(1, 6);
    controls->addWidget(m_resourceCount);
    auto* resize = makeButton(QStringLiteral("Apply size"));
    auto* example = makeButton(QStringLiteral("Load textbook example"));
    auto* check = makeButton(QStringLiteral("Check safety"), QStringLiteral("primary"));
    controls->addWidget(resize);
    controls->addStretch(1);
    controls->addWidget(example);
    controls->addWidget(check);
    root->addLayout(controls);

    m_available = matrixTable(true, this);
    m_available->setFixedHeight(32 + 34);
    root->addWidget(titledCard(QStringLiteral("Available"), QStringLiteral("Double-click a cell to edit."),
                               m_available));
    auto* matrices = new QGridLayout();
    matrices->setHorizontalSpacing(16);
    matrices->setVerticalSpacing(16);
    m_allocation = matrixTable(true, this);
    m_maximum = matrixTable(true, this);
    m_need = matrixTable(false, this);
    for (QTableWidget* t : {m_allocation, m_maximum, m_need})
        t->setMinimumHeight(220);
    matrices->addWidget(titledCard(QStringLiteral("Allocation"), QStringLiteral("What each process holds now."),
                                   m_allocation),
                        0, 0);
    matrices->addWidget(titledCard(QStringLiteral("Maximum demand"), QStringLiteral("The most each process may ever "
                                                                                    "hold."),
                                   m_maximum),
                        0, 1);
    matrices->addWidget(titledCard(QStringLiteral("Remaining need"), QStringLiteral("Calculated: Maximum - "
                                                                                    "Allocation."),
                                   m_need),
                        0, 2);
    root->addLayout(matrices);

    // result
    auto* resultBox = new QWidget();
    auto* resultLayout = new QVBoxLayout(resultBox);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(8);
    m_verdict = makeLabel(QStringLiteral("Press \"Check safety\"."), QStringLiteral("sectionTitle"));
    m_sequence = makeLabel(QString(), QStringLiteral("caption"));
    m_steps = new QTableWidget();
    setupTable(m_steps, {QStringLiteral("Step"), QStringLiteral("Process"), QStringLiteral("Work before"),
                         QStringLiteral("Its need"), QStringLiteral("Work after it finishes")});
    m_steps->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_steps->setMinimumHeight(200);
    resultLayout->addWidget(m_verdict);
    resultLayout->addWidget(m_sequence);
    resultLayout->addWidget(m_steps);
    root->addWidget(titledCard(QStringLiteral("Safety check"), QString(), resultBox));

    // request / release
    auto* requestBox = new QWidget();
    auto* requestRow = new QHBoxLayout(requestBox);
    requestRow->setContentsMargins(0, 0, 0, 0);
    requestRow->setSpacing(10);
    m_requestProcess = new QComboBox();
    m_requestVector = new QLineEdit();
    m_requestVector->setPlaceholderText(QStringLiteral("Amounts per resource, e.g. 1 0 2"));
    auto* request = makeButton(QStringLiteral("Request"), QStringLiteral("primary"));
    auto* release = makeButton(QStringLiteral("Release"));
    requestRow->addWidget(m_requestProcess);
    requestRow->addWidget(m_requestVector, 1);
    requestRow->addWidget(request);
    requestRow->addWidget(release);
    root->addWidget(titledCard(QStringLiteral("Resource request and release"),
                               QStringLiteral("Granted only if the system stays safe."),
                               requestBox));
    root->addStretch(1);

    connect(resize, &QPushButton::clicked, this,
            [this]() { resizeTables(m_processCount->value(), m_resourceCount->value()); });
    connect(example, &QPushButton::clicked, this, [this]() {
        loadState(os::textbookBankersExample());
        m_banner->showInfo(QStringLiteral("Loaded the textbook example."));
    });
    connect(check, &QPushButton::clicked, this, &BankersPage::onCheckSafety);
    connect(request, &QPushButton::clicked, this, [this]() { onRequest(false); });
    connect(release, &QPushButton::clicked, this, [this]() { onRequest(true); });
    for (QTableWidget* t : {m_allocation, m_maximum})
        connect(t, &QTableWidget::itemChanged, this, [this]() { updateNeed(); });

    loadState(os::textbookBankersExample());
}

void BankersPage::resizeTables(int processes, int resources)
{
    m_loading = true;
    QStringList resourceHeaders;
    for (int j = 0; j < resources; ++j)
        resourceHeaders << resourceName(j);
    QStringList processHeaders;
    for (int i = 0; i < processes; ++i)
        processHeaders << QStringLiteral("P%1").arg(i);
    auto setup = [&](QTableWidget* t, int rows, const QStringList& rowHeaders) {
        t->setRowCount(rows);
        t->setColumnCount(resources);
        t->setHorizontalHeaderLabels(resourceHeaders);
        t->setVerticalHeaderLabels(rowHeaders);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < resources; ++c)
                if (!t->item(r, c)) {
                    auto* item = new QTableWidgetItem();
                    item->setData(Qt::EditRole, 0);
                    item->setTextAlignment(Qt::AlignCenter);
                    t->setItem(r, c, item);
                }
    };
    setup(m_available, 1, {QStringLiteral("Free")});
    setup(m_allocation, processes, processHeaders);
    setup(m_maximum, processes, processHeaders);
    setup(m_need, processes, processHeaders);
    m_requestProcess->clear();
    m_requestProcess->addItems(processHeaders);
    m_processCount->setValue(processes);
    m_resourceCount->setValue(resources);
    m_loading = false;
    updateNeed();
    m_verdict->setText(QStringLiteral("Press \"Check safety\"."));
    m_sequence->clear();
    m_steps->setRowCount(0);
}

void BankersPage::loadState(const os::BankersState& s)
{
    const int n = static_cast<int>(s.maximum.size());
    const int m = static_cast<int>(s.available.size());
    resizeTables(n, m);
    m_loading = true;
    for (int j = 0; j < m; ++j)
        m_available->item(0, j)->setData(Qt::EditRole, s.available[static_cast<std::size_t>(j)]);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j) {
            m_allocation->item(i, j)->setData(Qt::EditRole, s.allocation[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)]);
            m_maximum->item(i, j)->setData(Qt::EditRole, s.maximum[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)]);
        }
    m_loading = false;
    updateNeed();
}

bool BankersPage::readState(os::BankersState& out)
{
    out = os::BankersState();
    const int n = m_allocation->rowCount();
    const int m = m_available->columnCount();
    for (int j = 0; j < m; ++j)
        out.available.push_back(m_available->item(0, j)->data(Qt::EditRole).toInt());
    for (int i = 0; i < n; ++i) {
        os::Vector alloc, max;
        for (int j = 0; j < m; ++j) {
            alloc.push_back(m_allocation->item(i, j)->data(Qt::EditRole).toInt());
            max.push_back(m_maximum->item(i, j)->data(Qt::EditRole).toInt());
        }
        out.allocation.push_back(alloc);
        out.maximum.push_back(max);
    }
    const auto problems = os::validateBankers(out);
    if (!problems.empty()) {
        QStringList lines;
        for (const auto& p : problems)
            lines << QString::fromStdString(p);
        m_banner->showError(lines.join(QLatin1Char('\n')));
        return false;
    }
    return true;
}

void BankersPage::updateNeed()
{
    if (m_loading)
        return;
    for (int i = 0; i < m_need->rowCount(); ++i)
        for (int j = 0; j < m_need->columnCount(); ++j) {
            const int need = m_maximum->item(i, j)->data(Qt::EditRole).toInt()
                             - m_allocation->item(i, j)->data(Qt::EditRole).toInt();
            QTableWidgetItem* item = m_need->item(i, j);
            item->setData(Qt::EditRole, need);
            item->setForeground(need < 0 ? QColor("#B91C1C") : QColor("#1E293B"));
        }
}

void BankersPage::showSafety(const os::SafetyResult& r, const os::BankersState& state)
{
    const os::Matrix need = os::needMatrix(state);  // need of the state that was checked
    m_verdict->setText(r.safe ? QStringLiteral("SAFE state") : QStringLiteral("UNSAFE state"));
    m_verdict->setStyleSheet(r.safe ? QStringLiteral("color:#15803D;") : QStringLiteral("color:#B91C1C;"));
    QStringList order;
    for (int p : r.sequence)
        order << QStringLiteral("P%1").arg(p);
    if (r.safe) {
        m_sequence->setText(QStringLiteral("Safe sequence: %1")
                                .arg(order.join(QStringLiteral(" -> "))));
    } else {
        QStringList stuck;
        for (int p : r.unfinished)
            stuck << QStringLiteral("P%1").arg(p);
        m_sequence->setText(QStringLiteral("No safe sequence. %1 can finish; %2 cannot. A deadlock is possible but has not happened yet.")
                                .arg(order.isEmpty() ? QStringLiteral("No process") : order.join(QStringLiteral(", ")),
                                     stuck.join(QStringLiteral(", "))));
    }
    m_steps->setRowCount(static_cast<int>(r.steps.size()));
    for (int i = 0; i < static_cast<int>(r.steps.size()); ++i) {
        const os::SafetyStep& st = r.steps[static_cast<std::size_t>(i)];
        m_steps->setItem(i, 0, makeNumberItem(i + 1));
        m_steps->setItem(i, 1, makeItem(QStringLiteral("P%1").arg(st.process)));
        m_steps->setItem(i, 2, makeItem(vectorText(st.workBefore)));
        m_steps->setItem(i, 3, makeItem(st.process < static_cast<int>(need.size())
                                            ? vectorText(need[static_cast<std::size_t>(st.process)])
                                            : QString()));
        m_steps->setItem(i, 4, makeItem(vectorText(st.workAfter)));
    }
}

void BankersPage::onCheckSafety()
{
    os::BankersState s;
    if (!readState(s))
        return;
    m_banner->clearMessage();
    showSafety(os::checkSafety(s), s);
}

bool BankersPage::readVector(const QString& text, os::Vector& out, QString* error) const
{
    out.clear();
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[\\s,()]+")), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const int v = part.toInt(&ok);
        if (!ok || v < 0) {
            if (error)
                *error = QStringLiteral("\"%1\" is not a non-negative whole number.").arg(part);
            return false;
        }
        out.push_back(v);
    }
    if (static_cast<int>(out.size()) != m_available->columnCount()) {
        if (error)
            *error = QStringLiteral("Enter exactly %1 number(s), one per resource type.").arg(m_available->columnCount());
        return false;
    }
    return true;
}

void BankersPage::onRequest(bool release)
{
    os::BankersState s;
    if (!readState(s))
        return;
    os::Vector amounts;
    QString error;
    if (!readVector(m_requestVector->text(), amounts, &error)) {
        m_banner->showError(error);
        return;
    }
    const int process = m_requestProcess->currentIndex();
    const os::RequestResult r =
        release ? os::releaseResources(s, process, amounts) : os::requestResources(s, process, amounts);
    const QString reason = QString::fromStdString(r.reason);
    switch (r.outcome) {
    case os::RequestOutcome::Granted:
        loadState(r.state);
        showSafety(r.safety, r.state);
        m_banner->showSuccess(reason);
        break;
    case os::RequestOutcome::Denied: {
        // Show why the TENTATIVE state was unsafe; the tables keep the unchanged state.
        os::BankersState tentative = s;
        for (std::size_t j = 0; j < amounts.size(); ++j) {
            tentative.available[j] -= amounts[j];
            tentative.allocation[static_cast<std::size_t>(process)][j] += amounts[j];
        }
        showSafety(r.safety, tentative);
        m_banner->showWarning(reason);
        break;
    }
    case os::RequestOutcome::MustWait:
        m_banner->showWarning(reason);
        break;
    case os::RequestOutcome::Invalid:
        m_banner->showError(reason);
        break;
    }
}

}  // namespace labdesk::ui
