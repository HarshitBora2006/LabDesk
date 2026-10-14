#include "theme.h"

#include <QApplication>
#include <QHash>
#include <QPalette>
#include <QStyleHints>
#include <QtGlobal>

namespace labdesk::ui::theme {

StatusColors statusColors(const QString& status)
{
    static const QHash<QString, StatusColors> map = {
        // computers
        {QStringLiteral("AVAILABLE"), {QColor("#DCFCE7"), QColor("#166534")}},
        {QStringLiteral("OCCUPIED"), {QColor("#FEF3C7"), QColor("#92400E")}},
        {QStringLiteral("MAINTENANCE"), {QColor("#E2E8F0"), QColor("#334155")}},
        // sessions
        {QStringLiteral("ACTIVE"), {QColor("#FEF3C7"), QColor("#92400E")}},
        {QStringLiteral("ENDED"), {QColor("#F1F5F9"), QColor("#475569")}},
        // maintenance
        {QStringLiteral("OPEN"), {QColor("#FEF3C7"), QColor("#92400E")}},
        {QStringLiteral("RESOLVED"), {QColor("#F1F5F9"), QColor("#475569")}},
        // process states (educational simulation)
        {QStringLiteral("NEW"), {QColor("#F1F5F9"), QColor("#475569")}},
        {QStringLiteral("READY"), {QColor("#DBEAFE"), QColor("#1E40AF")}},
        {QStringLiteral("RUNNING"), {QColor("#2563EB"), QColor("#FFFFFF")}},
        {QStringLiteral("WAITING"), {QColor("#FEF3C7"), QColor("#92400E")}},
        {QStringLiteral("TERMINATED"), {QColor("#E2E8F0"), QColor("#334155")}},
    };
    return map.value(status, {QColor("#F1F5F9"), QColor("#1E293B")});
}

QColor processColor(int index)
{
    static const QColor colors[] = {QColor("#2563EB"), QColor("#0F766E"), QColor("#B45309"), QColor("#6D28D9"),
                                    QColor("#BE185D"), QColor("#4D7C0F"), QColor("#0E7490"), QColor("#9A3412")};
    const int n = static_cast<int>(sizeof(colors) / sizeof(colors[0]));
    return colors[((index % n) + n) % n];
}

void applyApplicationTheme(QApplication& app)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Stops Qt from switching to the dark system palette (and gives a light title bar).
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
#endif
    QPalette p;
    const QColor disabledText("#94A3B8");
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool disabled = group == QPalette::Disabled;
        p.setColor(group, QPalette::Window, Background);
        p.setColor(group, QPalette::WindowText, disabled ? disabledText : Text);
        p.setColor(group, QPalette::Base, Panel);
        p.setColor(group, QPalette::AlternateBase, QColor("#F8FAFC"));
        p.setColor(group, QPalette::Text, disabled ? disabledText : Text);
        p.setColor(group, QPalette::PlaceholderText, disabledText);
        p.setColor(group, QPalette::Button, Panel);
        p.setColor(group, QPalette::ButtonText, disabled ? disabledText : Text);
        p.setColor(group, QPalette::BrightText, Panel);
        p.setColor(group, QPalette::Highlight, disabled ? QColor("#CBD5E1") : Primary);
        p.setColor(group, QPalette::HighlightedText, Panel);
        p.setColor(group, QPalette::ToolTipBase, Panel);
        p.setColor(group, QPalette::ToolTipText, Text);
        p.setColor(group, QPalette::Link, Primary);
        p.setColor(group, QPalette::LinkVisited, PrimaryHover);
        p.setColor(group, QPalette::Light, Panel);
        p.setColor(group, QPalette::Midlight, QColor("#F1F5F9"));
        p.setColor(group, QPalette::Mid, QColor("#CBD5E1"));
        p.setColor(group, QPalette::Dark, QColor("#94A3B8"));
        p.setColor(group, QPalette::Shadow, QColor("#64748B"));
    }
    QApplication::setPalette(p);
    app.setStyleSheet(styleSheet());
}

QString styleSheet()
{
    return QStringLiteral(R"CSS(
/* ---------- surfaces ---------- */
QMainWindow { background: #F4F7FB; }
QWidget#pageRoot, QWidget#pageScrollBody, QStackedWidget#pageStack { background: #F4F7FB; }
QScrollArea#pageScroll { background: #F4F7FB; border: none; }
QWidget#tabPage { background: #FFFFFF; }
QWidget#transparent { background: transparent; }
QDialog, QMessageBox, QInputDialog { background: #FFFFFF; }
QMessageBox QLabel, QInputDialog QLabel { color: #1E293B; }

QWidget#sidebar { background: #FFFFFF; border-right: 1px solid #E2E8F0; }
QLabel#brand { font-size: 16pt; font-weight: 700; color: #1E293B; padding: 20px 20px 0 20px; }
QLabel#brandSub { color: #64748B; padding: 2px 20px 14px 20px; }
QListWidget#nav { background: transparent; border: none; outline: 0; }
QListWidget#nav::item { padding: 9px 14px; margin: 1px 10px; border-radius: 6px; color: #334155; }
QListWidget#nav::item:selected { background: #EFF6FF; color: #1D4ED8; font-weight: 600; }
QListWidget#nav::item:hover:!selected { background: #F1F5F9; }
QListWidget#nav::item:disabled { color: #94A3B8; padding-top: 16px; padding-bottom: 4px; }

/* ---------- typography ---------- */
QLabel#pageTitle { font-size: 18pt; font-weight: 600; color: #1E293B; }
QLabel#pageSubtitle, QLabel#muted, QLabel#statLabel, QLabel#caption { color: #64748B; }
QLabel#sectionTitle { font-size: 11.5pt; font-weight: 600; color: #1E293B; }
QLabel#statValue { font-size: 21pt; font-weight: 600; color: #1E293B; }
QLabel#fieldLabel { color: #334155; font-weight: 600; }
QLabel#explainer { color: #334155; background: #F8FAFC; border: 1px solid #E2E8F0; border-radius: 8px; padding: 10px 12px; }

/* ---------- cards ---------- */
QFrame#card { background: #FFFFFF; border: 1px solid #E2E8F0; border-radius: 10px; }

/* ---------- buttons ---------- */
QPushButton { background: #FFFFFF; color: #1E293B; border: 1px solid #CBD5E1; border-radius: 6px; padding: 7px 14px; min-height: 20px; }
QPushButton:hover { background: #F8FAFC; border-color: #94A3B8; }
QPushButton:pressed { background: #F1F5F9; }
QPushButton:focus { border-color: #2563EB; }
QPushButton:disabled { color: #94A3B8; background: #F8FAFC; border-color: #E2E8F0; }
QPushButton[variant="primary"] { background: #2563EB; color: #FFFFFF; border: 1px solid #2563EB; font-weight: 600; }
QPushButton[variant="primary"]:hover { background: #1D4ED8; border-color: #1D4ED8; }
QPushButton[variant="primary"]:pressed { background: #1E40AF; }
QPushButton[variant="primary"]:disabled { background: #BFDBFE; border-color: #BFDBFE; color: #FFFFFF; }
QPushButton[variant="danger"] { color: #B91C1C; border-color: #FECACA; }
QPushButton[variant="danger"]:hover { background: #FEF2F2; border-color: #FCA5A5; }
QPushButton[allowed="false"] { color: #94A3B8; border-style: dashed; }

/* ---------- inputs ---------- */
QLineEdit, QComboBox, QSpinBox, QPlainTextEdit {
    background: #FFFFFF; color: #1E293B; border: 1px solid #CBD5E1; border-radius: 6px; padding: 5px 8px;
    selection-background-color: #DBEAFE; selection-color: #1E293B;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus { border: 1px solid #2563EB; }
QLineEdit:read-only { background: #F8FAFC; color: #64748B; }
QComboBox QAbstractItemView {
    background: #FFFFFF; color: #1E293B; border: 1px solid #E2E8F0; outline: 0;
    selection-background-color: #EFF6FF; selection-color: #1E293B;
}
QCheckBox { color: #1E293B; spacing: 8px; }

/* ---------- tables and lists ---------- */
QTableWidget, QListWidget#log {
    background: #FFFFFF; color: #1E293B; border: 1px solid #E2E8F0; border-radius: 8px; gridline-color: #F1F5F9;
    alternate-background-color: #F8FAFC; selection-background-color: #DBEAFE; selection-color: #1E293B;
}
QHeaderView { background: #F8FAFC; border: none; }
QHeaderView::section {
    background: #F8FAFC; color: #475569; border: none; border-bottom: 1px solid #E2E8F0;
    padding: 7px 8px; font-weight: 600;
}
QTableCornerButton::section { background: #F8FAFC; border: none; }

/* ---------- scrollbars (previously drawn from the dark system palette) ---------- */
QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle:vertical { background: #CBD5E1; border-radius: 4px; min-height: 28px; }
QScrollBar::handle:horizontal { background: #CBD5E1; border-radius: 4px; min-width: 28px; }
QScrollBar::handle:hover { background: #94A3B8; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

/* ---------- tabs ---------- */
QTabWidget::pane { border: 1px solid #E2E8F0; border-radius: 10px; background: #FFFFFF; top: -1px; }
QTabBar::tab { padding: 9px 18px; background: transparent; color: #64748B; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: #1E293B; border-bottom: 2px solid #2563EB; font-weight: 600; }
QTabBar::tab:hover:!selected { color: #1E293B; }

/* ---------- notifications: icon word + tinted background + left accent ---------- */
QLabel#banner { border-radius: 8px; padding: 10px 14px; border: 1px solid #E2E8F0; border-left: 4px solid #64748B; background: #F8FAFC; color: #1E293B; }
QLabel#banner[kind="success"] { background: #F0FDF4; color: #14532D; border: 1px solid #BBF7D0; border-left: 4px solid #15803D; }
QLabel#banner[kind="info"]    { background: #EFF6FF; color: #1E3A8A; border: 1px solid #BFDBFE; border-left: 4px solid #2563EB; }
QLabel#banner[kind="warning"] { background: #FFFBEB; color: #78350F; border: 1px solid #FDE68A; border-left: 4px solid #B45309; }
QLabel#banner[kind="error"]   { background: #FEF2F2; color: #7F1D1D; border: 1px solid #FECACA; border-left: 4px solid #B91C1C; }

/* ---------- empty / error states inside sections ---------- */
QLabel#stateMessage { color: #64748B; background: #F8FAFC; border: 1px dashed #CBD5E1; border-radius: 8px; padding: 18px; }
QLabel#stateMessage[kind="error"] { color: #7F1D1D; background: #FEF2F2; border: 1px solid #FECACA; }

/* ---------- misc ---------- */
QProgressBar#occupancy { background: #E2E8F0; border: none; border-radius: 4px; min-height: 8px; max-height: 8px; }
QProgressBar#occupancy::chunk { background: #2563EB; border-radius: 4px; }
QStatusBar { background: #FFFFFF; color: #64748B; border-top: 1px solid #E2E8F0; }
QStatusBar QLabel { color: #64748B; padding: 0 6px; }
QToolTip { background: #FFFFFF; color: #1E293B; border: 1px solid #CBD5E1; padding: 6px 8px; }
QSlider::groove:horizontal { height: 6px; background: #E2E8F0; border-radius: 3px; }
QSlider::handle:horizontal { background: #2563EB; width: 16px; margin: -6px 0; border-radius: 8px; }
QSlider::sub-page:horizontal { background: #93C5FD; border-radius: 3px; }
)CSS");
}

}  // namespace labdesk::ui::theme
