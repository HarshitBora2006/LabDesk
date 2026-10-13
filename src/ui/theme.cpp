#include "theme.h"

#include <QHash>

namespace labdesk::ui::theme {

StatusColors statusColors(const QString& status)
{
    static const QHash<QString, StatusColors> map = {
        // computers
        {QStringLiteral("AVAILABLE"), {QColor("#DDEFE8"), QColor("#24634F")}},
        {QStringLiteral("OCCUPIED"), {QColor("#F6E7C8"), QColor("#7E550F")}},
        {QStringLiteral("MAINTENANCE"), {QColor("#E1E6EF"), QColor("#3F4D68")}},
        // sessions
        {QStringLiteral("ACTIVE"), {QColor("#F6E7C8"), QColor("#7E550F")}},
        {QStringLiteral("ENDED"), {QColor("#ECEDEF"), QColor("#5D677A")}},
        // maintenance
        {QStringLiteral("OPEN"), {QColor("#F6E7C8"), QColor("#7E550F")}},
        {QStringLiteral("RESOLVED"), {QColor("#ECEDEF"), QColor("#5D677A")}},
        // process states (educational simulation)
        {QStringLiteral("NEW"), {QColor("#ECEDEF"), QColor("#4A5568")}},
        {QStringLiteral("READY"), {QColor("#E3F0EE"), QColor("#24635C")}},
        {QStringLiteral("RUNNING"), {QColor("#2B7A73"), QColor("#FFFFFF")}},
        {QStringLiteral("WAITING"), {QColor("#F6E7C8"), QColor("#7E550F")}},
        {QStringLiteral("TERMINATED"), {QColor("#E1E6EF"), QColor("#3F4D68")}},
    };
    return map.value(status, {QColor("#ECEDEF"), QColor("#1E2B45")});
}

QColor processColor(int index)
{
    static const QColor colors[] = {QColor("#4E8C84"), QColor("#C08B3E"), QColor("#5B6F9A"), QColor("#9A6A8C"),
                                    QColor("#6F8F4E"), QColor("#B5654F"), QColor("#4F7FA8"), QColor("#8A7B5A")};
    const int n = static_cast<int>(sizeof(colors) / sizeof(colors[0]));
    return colors[((index % n) + n) % n];
}

QString styleSheet()
{
    return QStringLiteral(R"CSS(
QMainWindow, QWidget#pageRoot, QScrollArea#pageScroll, QWidget#pageScrollBody { background: #F3F4F1; }
QWidget { color: #1E2B45; }

QWidget#sidebar { background: #FAFAF8; border-right: 1px solid #DCDFE4; }
QLabel#brand { font-size: 15pt; font-weight: 700; color: #1E2B45; padding: 18px 18px 0 18px; }
QLabel#brandSub { color: #5D677A; padding: 0 18px 10px 18px; }
QListWidget#nav { background: transparent; border: none; outline: 0; }
QListWidget#nav::item { padding: 8px 16px; margin: 1px 8px; border-radius: 6px; color: #1E2B45; }
QListWidget#nav::item:selected { background: #E3F0EE; color: #1E2B45; font-weight: 600; }
QListWidget#nav::item:hover:!selected { background: #EFF1EE; }
QListWidget#nav::item:disabled { color: #8A93A3; padding-top: 14px; padding-bottom: 2px; }

QLabel#pageTitle { font-size: 17pt; font-weight: 600; }
QLabel#pageSubtitle, QLabel#muted { color: #5D677A; }
QLabel#sectionTitle { font-size: 11pt; font-weight: 600; }
QLabel#statValue { font-size: 22pt; font-weight: 600; }
QLabel#statLabel { color: #5D677A; }
QLabel#emptyState { color: #5D677A; padding: 18px; }

QFrame#card { background: #FFFFFF; border: 1px solid #DCDFE4; border-radius: 8px; }

QPushButton { background: #FFFFFF; border: 1px solid #C9CED6; border-radius: 6px; padding: 6px 14px; min-height: 20px; }
QPushButton:hover { background: #F4F6F7; }
QPushButton:pressed { background: #ECEFF1; }
QPushButton:disabled { color: #A3AAB6; background: #F6F7F8; border-color: #E1E4E8; }
QPushButton[variant="primary"] { background: #2B7A73; color: #FFFFFF; border: 1px solid #2B7A73; font-weight: 600; }
QPushButton[variant="primary"]:hover { background: #236660; }
QPushButton[variant="primary"]:pressed { background: #1C534E; }
QPushButton[variant="primary"]:disabled { background: #A9C9C5; border-color: #A9C9C5; color: #F4F8F7; }
QPushButton[variant="danger"] { color: #A9473A; border-color: #D9B3AD; }
QPushButton[variant="danger"]:hover { background: #FBF1EF; }

QLineEdit, QComboBox, QSpinBox, QPlainTextEdit {
    background: #FFFFFF; border: 1px solid #C9CED6; border-radius: 5px; padding: 5px 7px;
    selection-background-color: #BFDDD8; selection-color: #1E2B45;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus { border: 1px solid #2B7A73; }
QLineEdit:read-only { background: #F6F7F8; color: #5D677A; }

QTableWidget, QListWidget#log {
    background: #FFFFFF; border: 1px solid #DCDFE4; border-radius: 6px; gridline-color: #EEF0F2;
    alternate-background-color: #FAFBFA; selection-background-color: #D6EAE6; selection-color: #1E2B45;
}
QHeaderView::section {
    background: #F5F6F4; color: #5D677A; border: none; border-bottom: 1px solid #DCDFE4;
    padding: 6px 8px; font-weight: 600;
}

QTabWidget::pane { border: 1px solid #DCDFE4; border-radius: 6px; background: #FFFFFF; top: -1px; }
QTabBar::tab { padding: 8px 16px; background: transparent; color: #5D677A; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: #1E2B45; border-bottom: 2px solid #2B7A73; font-weight: 600; }

QLabel#banner { border-radius: 6px; padding: 8px 12px; }
QLabel#banner[kind="success"] { background: #E3F1EC; color: #24634F; border: 1px solid #BFDDD0; }
QLabel#banner[kind="error"]   { background: #F8E9E6; color: #8E3A2F; border: 1px solid #E6C3BC; }
QLabel#banner[kind="info"]    { background: #E9EEF5; color: #3F4D68; border: 1px solid #CDD6E4; }

QStatusBar { background: #FAFAF8; color: #5D677A; border-top: 1px solid #DCDFE4; }
QToolTip { background: #1E2B45; color: #FFFFFF; border: none; padding: 5px 8px; }
)CSS");
}

}  // namespace labdesk::ui::theme
