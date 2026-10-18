#pragma once
// Short viva-oriented guides: where each OS / DBMS concept is used in LabDesk
// and which screen shows it. Only concepts that the code really implements.

#include "page.h"

namespace labdesk::ui {

class ConceptGuidePage : public Page {
    Q_OBJECT
public:
    enum class Topic { OperatingSystems, Databases };
    explicit ConceptGuidePage(Topic topic, QWidget* parent = nullptr);
};

}  // namespace labdesk::ui
