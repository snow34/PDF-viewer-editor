#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("PrepressPdf"));
    QApplication::setApplicationName(QStringLiteral("PrepressPdf"));
    QApplication::setApplicationDisplayName(QStringLiteral("Prepress PDF"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("PDF file to open."));
    QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                  QStringLiteral("Save a picture of the window to <png> and quit "
                                                 "(for automated checks)."),
                                  QStringLiteral("png"));
    parser.addOption(screenshot);
    parser.process(app);

    MainWindow window;
    window.show();
    if (!parser.positionalArguments().isEmpty()) {
        if (parser.isSet(screenshot))
            window.screenshotWhenReady(parser.value(screenshot));
        window.openFile(parser.positionalArguments().first());
    }
    return app.exec();
}
