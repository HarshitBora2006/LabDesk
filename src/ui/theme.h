#pragma once
// Visual system: one palette, one stylesheet, status colours in one place.
//
// Root cause of the earlier "black areas": the app never set a QPalette, so on
// Windows in dark mode (Qt 6.5+ Fusion follows the system scheme) every surface
// the stylesheet did not paint explicitly (tab pages, scroll-area viewports,
// scrollbars, header gaps, popups, message boxes) used a near-black palette,
// while a global "QWidget { color: navy }" rule put dark text on top of them.
// applyApplicationTheme() now sets a complete light palette first and the
// stylesheet no longer forces a text colour on every widget.

#include <QColor>
#include <QString>

class QApplication;

namespace labdesk::ui::theme {

// Base palette
inline const QColor Background("#F4F7FB");
inline const QColor Panel("#FFFFFF");
inline const QColor Border("#E2E8F0");
inline const QColor Text("#1E293B");
inline const QColor TextMuted("#64748B");
inline const QColor Primary("#2563EB");
inline const QColor PrimaryHover("#1D4ED8");
inline const QColor PrimarySoft("#EFF6FF");
inline const QColor Success("#15803D");
inline const QColor Warning("#B45309");
inline const QColor Error("#B91C1C");

struct StatusColors {
    QColor background;
    QColor foreground;
};

// Computer, session, maintenance and process statuses share one lookup.
StatusColors statusColors(const QString& status);

// Restrained, distinguishable colours for Gantt bars (white text is readable on all).
QColor processColor(int index);

QString styleSheet();

// Light palette + stylesheet (+ light colour scheme on Qt 6.8+). Call once,
// after QApplication is constructed and the Fusion style is set.
void applyApplicationTheme(QApplication& app);

}  // namespace labdesk::ui::theme
