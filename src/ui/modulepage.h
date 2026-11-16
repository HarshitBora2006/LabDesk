#pragma once
// A sidebar entry that groups several pages behind a row of sub-navigation
// tabs (e.g. Computer Management: Computers, Maintenance).
// Only the visible sub-page is refreshed; changes in any sub-page propagate.

#include "page.h"

#include <vector>

class QTabWidget;

namespace labdesk::ui {

class ModulePage : public Page {
    Q_OBJECT
public:
    explicit ModulePage(QWidget* parent = nullptr);
    void addSubPage(Page* page, const QString& title);
    void refresh() override;
    Page* subPage(int index) const;

private:
    QTabWidget* m_tabs = nullptr;
    std::vector<Page*> m_pages;
};

}  // namespace labdesk::ui
