#include "core/Preferences.h"
#include "core/SingleInstance.h"
#include "ui/App.h"
#include "ui/DocumentImageProvider.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>

Q_IMPORT_QML_PLUGIN(Guckste_CorePlugin)
Q_IMPORT_QML_PLUGIN(Guckste_UiPlugin)
Q_IMPORT_QML_PLUGIN(GuckstePlugin)

int main(int argc, char *argv[])
{
    // QApplication rather than QGuiApplication for the tray icon and its menu.
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("guckste"));
    QApplication::setApplicationDisplayName(QStringLiteral("Guckste"));
    QApplication::setOrganizationName(QStringLiteral("guckste"));
    QApplication::setOrganizationDomain(QStringLiteral("guckste.local"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/Guckste/logo/guckste.svg")));
    QApplication::setDesktopFileName(QStringLiteral("guckste"));
    // Guckste lives in the tray; closing a window is not quitting.
    QApplication::setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Screenshot tool with annotations and pixel measurements.\n\n"
                       "Commands:\n"
                       "  gui          Pick a region of the screen to capture (default from a launcher)\n"
                       "  full         Capture all screens to the clipboard and the screenshot folder\n"
                       "  open [file]  Open an image in the editor, asking for one without a file\n"
                       "  record       Pick a region to record, or stop the recording\n"
                       "  settings     Open the settings\n"
                       "  quit         Quit the running instance\n\n"
                       "Without a command, Guckste starts in the tray. A command goes to the\n"
                       "running instance if there is one."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("gui, full, open, record, settings or quit"));
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Image to open"), QStringLiteral("[file]"));
    parser.process(app);

    QStringList arguments = parser.positionalArguments();
    QString command = arguments.value(0);
    if (command == QStringLiteral("open") && arguments.size() > 1)
        command += u' ' + QFileInfo(arguments[1]).absoluteFilePath();
    else if (!command.isEmpty() && QFileInfo::exists(command) && arguments.size() == 1)
        command = QStringLiteral("open ") + QFileInfo(command).absoluteFilePath();

    if (SingleInstance::sendToRunning(command.isEmpty() ? QStringLiteral("gui") : command)) {
        if (command.isEmpty())
            std::puts("Guckste is already running; taking a screenshot");
        return 0;
    }
    if (command == QStringLiteral("quit"))
        return 0;

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("guckste"), new DocumentImageProvider);
    App guckste(&engine);
    guckste.setupTray();

    SingleInstance instance;
    QObject::connect(&instance, &SingleInstance::commandReceived, &guckste, &App::handleCommand);
    instance.listen();

    if (!command.isEmpty())
        guckste.handleCommand(command);
    return app.exec();
}
