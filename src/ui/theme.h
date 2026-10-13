#pragma once
// Visual system: one palette, one stylesheet, status colours in one place.

#include <QColor>
#include <QString>

namespace labdesk::ui::theme {

// Base palette
inline const QColor Background("#F3F4F1");   // warm light grey
inline const QColor Panel("#FFFFFF");
inline const QColor Border("#DCDFE4");
inline const QColor Text("#1E2B45");         // deep navy
inline const QColor TextMuted("#5D677A");
inline const QColor Primary("#2B7A73");      // muted teal
inline const QColor PrimarySoft("#E3F0EE");

struct StatusColors {
    QColor background;
    QColor foreground;
};

// Computer, session, maintenance and process statuses share one lookup.
StatusColors statusColors(const QString& status);

// Restrained, distinguishable colours for Gantt bars.
QColor processColor(int index);

QString styleSheet();

}  // namespace labdesk::ui::theme
