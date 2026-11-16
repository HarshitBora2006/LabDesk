#include "conceptguidepage.h"

#include "layouthelpers.h"
#include "uihelpers.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

#include <iterator>

namespace labdesk::ui {

namespace {
struct Entry {
    const char* title;
    const char* where;
    const char* see;
};

const Entry kDb[] = {
    {"Primary keys",
     "Every table has one: student_id, computer_id and admin_id are natural keys; session_id, maintenance_id, "
     "request_id and process_id are AUTOINCREMENT surrogate keys that are never reused.",
     "DBMS Management > Schema and Relationships"},
    {"Foreign keys and referential integrity",
     "session, maintenance and lab_request refer to their parents with ON DELETE RESTRICT, so a computer or student "
     "with history cannot be deleted; process.session_id uses ON DELETE SET NULL. Enforcement is switched on and "
     "verified for every connection.",
     "Schema and Relationships; Database Overview shows foreign keys ON"},
    {"Constraints",
     "NOT NULL, UNIQUE (case-insensitive email), CHECK (allowed statuses, timestamp format, priority range, end after "
     "start) and partial UNIQUE indexes (one active session per computer and per student).",
     "Schema and Relationships"},
    {"Triggers",
     "Cross-table rules: an active session needs an occupied computer, no maintenance during a session, history rows "
     "cannot be changed or deleted, a request can only point at its own student's session.",
     "Schema and Relationships (triggers of each table)"},
    {"Normalisation",
     "Each fact is stored once: student names only in student, locations only in computer; other screens get them "
     "with joins. Derived values (waiting time, burst, totals) are computed by queries, not stored.",
     "SQL Operations (JOIN examples)"},
    {"Joins and aggregates",
     "Usage logs join session, student and computer; reports use GROUP BY with COUNT, SUM and AVG; the dashboard uses "
     "aggregate sub-queries.",
     "SQL Operations; Reports"},
    {"Transactions",
     "Allocation, session end with queue hand-over, maintenance and workload saves run inside BEGIN IMMEDIATE ... "
     "COMMIT. Any failure rolls back every change. Prepared statements with ? parameters are used for all input.",
     "DBMS Management > Transactions and Integrity"},
    {"Schema migration",
     "Schema versions are additive (CREATE ... IF NOT EXISTS) and recorded in PRAGMA user_version, so older databases "
     "gain new tables without losing data.",
     "Database Overview (schema version)"},
};
}  // namespace

ConceptGuidePage::ConceptGuidePage(Topic, QWidget* parent) : Page(parent)
{
    QWidget* body = makeScrollPage(this);
    QVBoxLayout* root = makePageLayout(body, QStringLiteral("DBMS Concept Guide"),
                                       QStringLiteral("Where database concepts appear in LabDesk."));
    auto* grid = new ResponsiveGrid(400, 16);
    QList<QWidget*> cards;
    const Entry* begin = std::begin(kDb);
    const Entry* end = std::end(kDb);
    for (const Entry* e = begin; e != end; ++e) {
        QFrame* card = makeCard();
        cardLayout(card)->addWidget(makeLabel(QString::fromUtf8(e->title), QStringLiteral("guideTitle")));
        cardLayout(card)->addWidget(makeLabel(QString::fromUtf8(e->where)));
        cardLayout(card)->addWidget(makeLabel(QStringLiteral("See: ") + QString::fromUtf8(e->see), QStringLiteral("caption")));
        cardLayout(card)->addStretch(1);
        cards.append(card);
    }
    grid->setWidgets(cards);
    root->addWidget(grid);
    root->addStretch(1);
}

}  // namespace labdesk::ui
