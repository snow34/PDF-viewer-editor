#include "engine/Document.h"
#include "preflight/Preflight.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QIcon>

#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("PrepressPdf"));
    QApplication::setApplicationName(QStringLiteral("PrepressPdf"));
    QApplication::setApplicationDisplayName(QStringLiteral("Prepress PDF"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app.png")));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("PDF file to open."));
    QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                  QStringLiteral("Save a picture of the window to <png> and quit "
                                                 "(for automated checks)."),
                                  QStringLiteral("png"));
    parser.addOption(screenshot);
    QCommandLineOption safeMode(QStringLiteral("safe-mode"),
                                QStringLiteral("Ignore the customised theme and layout."));
    parser.addOption(safeMode);
    QCommandLineOption preflightOption(
        QStringLiteral("preflight"),
        QStringLiteral("Check <file> against a PDF/X standard without opening a window, write the report "
                       "as JSON to --report (or standard output) and exit with 0 if it passed, 1 if not. "
                       "Standards: X-1a:2001, X-1a:2003, X-3:2002, X-3:2003, X-4."),
        QStringLiteral("standard"));
    QCommandLineOption reportOption(QStringLiteral("report"),
                                    QStringLiteral("File to write the --preflight report to."),
                                    QStringLiteral("json"));
    parser.addOption(preflightOption);
    parser.addOption(reportOption);
    parser.process(app);

    if (parser.isSet(preflightOption)) {
        preflight::Standard standard;
        if (parser.positionalArguments().isEmpty()
            || !preflight::standardFromClaim(parser.value(preflightOption), &standard)) {
            std::fprintf(stderr, "Usage: prepresspdf --preflight X-4 [--report out.json] file.pdf\n");
            return 2;
        }
        const QString path = parser.positionalArguments().first();
        QString error;
        auto document = engine::Document::open(path, &error);
        if (!document) {
            std::fprintf(stderr, "Cannot open %s: %s\n", qPrintable(path), qPrintable(error));
            return 2;
        }
        const preflight::Report report = preflight::run(*document, standard);
        const QByteArray json = preflight::toJson(report, QFileInfo(path).fileName());
        if (parser.isSet(reportOption)) {
            QFile out(parser.value(reportOption));
            if (!out.open(QIODevice::WriteOnly) || out.write(json) != json.size()) {
                std::fprintf(stderr, "Cannot write %s\n", qPrintable(out.fileName()));
                return 2;
            }
        } else {
            std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
        }
        return report.passed() ? 0 : 1;
    }

    MainWindow window(parser.isSet(safeMode));
    window.show();
    if (!parser.positionalArguments().isEmpty()) {
        if (parser.isSet(screenshot))
            window.screenshotWhenReady(parser.value(screenshot));
        window.openFile(parser.positionalArguments().first());
    }
    return app.exec();
}
