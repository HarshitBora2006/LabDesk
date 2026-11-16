#pragma once
// DBMS Concept Guide: where each database concept is used in LabDesk.

#include "page.h"

namespace labdesk::ui {

class ConceptGuidePage : public Page {
    Q_OBJECT
public:
    enum class Topic { Databases };
    explicit ConceptGuidePage(Topic topic, QWidget* parent = nullptr);
};

}  // namespace labdesk::ui
