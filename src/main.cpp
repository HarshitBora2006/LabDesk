// LabDesk entry point: opens (or creates) the SQLite database, applies the
// schema, optionally seeds clearly-labelled demo data, then shows the window.
//
// Command line:
//   labdesk [--db <path>] [--no-seed]

#include "database.h"
#include "mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QMessageBox>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LabDesk"));
    QApplication::setOrganizationName(QStringLiteral("LabDesk"));
    QApplication::setApplicationVersion(QStringLiteral("0.6.0"));
    QApplication::setStyle(QStringLiteral("Fusion"));

    QFont font = QApplication::font();
    if (font.pointSizeF() < 10.0)
        font.setPointSizeF(10.0);
    QApplication::setFont(font);
    // Complete light palette + stylesheet. Must happen before any widget is
    // created, otherwise unstyled surfaces pick up the (possibly dark) system palette.
    labdesk::ui::theme::applyApplicationTheme(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Smart computer lab management and CPU scheduling"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption dbOption(QStringList{QStringLiteral("db")},
                                      QStringLiteral("SQLite database file to use."), QStringLiteral("path"));
    const QCommandLineOption noSeedOption(QStringList{QStringLiteral("no-seed")},
                                          QStringLiteral("Do not insert demo data into an empty database."));
    parser.addOption(dbOption);
    parser.addOption(noSeedOption);
    parser.process(app);

    const QString path = parser.isSet(dbOption) ? parser.value(dbOption) : labdesk::Database::defaultFilePath();
    labdesk::Database db(path);
    QString error;
    if (!db.open(&error)) {
        QMessageBox::critical(nullptr, QStringLiteral("LabDesk cannot start"),
                              QStringLiteral("The database could not be opened.\n\n%1").arg(error));
        return 1;
    }
    if (!parser.isSet(noSeedOption) && !db.seedDemoData(&error)) {
        QMessageBox::warning(nullptr, QStringLiteral("Demo data"),
                             QStringLiteral("Demo data could not be added; continuing with an empty database.\n\n%1")
                                 .arg(error));
    }

    labdesk::MainWindow window(db);  // destroyed before `db`, so no dangling connections
    window.show();
    return app.exec();
}
