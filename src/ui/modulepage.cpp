#include "modulepage.h"

#include <QTabWidget>
#include <QVBoxLayout>

namespace labdesk::ui {

ModulePage::ModulePage(QWidget* parent) : Page(parent)
{
    setObjectName(QStringLiteral("pageRoot"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_tabs = new QTabWidget();
    m_tabs->setObjectName(QStringLiteral("moduleTabs"));
    m_tabs->setDocumentMode(true);
    layout->addWidget(m_tabs);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (Page* page = subPage(index))
            page->refresh();
    });
}

void ModulePage::addSubPage(Page* page, const QString& title)
{
    m_pages.push_back(page);
    m_tabs->addTab(page, title);
    connect(page, &Page::dataChanged, this, &Page::dataChanged);
}

Page* ModulePage::subPage(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_pages.size()))
        return nullptr;
    return m_pages[static_cast<std::size_t>(index)];
}

void ModulePage::refresh()
{
    if (Page* page = subPage(m_tabs->currentIndex()))
        page->refresh();
}

}  // namespace labdesk::ui
