// SPDX-License-Identifier: GPL-3.0-or-later
#include "cadview.h"
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("CAD4All"));
    QApplication::setApplicationVersion(QStringLiteral(CAD4ALL_VERSION));
    QApplication::setOrganizationName(QStringLiteral("CAD4All"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("CAD 2D e 3D parametrico"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("disegno"), QStringLiteral("Disegno .c4a o .dxf da aprire."));
    const QCommandLineOption scriptOption(QStringLiteral("script"), QStringLiteral("Esegue uno script di comandi (.scr)."), QStringLiteral("file"));
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"), QStringLiteral("Salva un'immagine della finestra ed esce."), QStringLiteral("png"));
    const QCommandLineOption exportOption(QStringLiteral("export"), QStringLiteral("Salva il disegno (.c4a o .dxf) dopo lo script."), QStringLiteral("file"));
    parser.addOption(scriptOption);
    parser.addOption(exportOption);
    parser.addOption(screenshotOption);
    parser.process(app);

    MainWindow window;
    window.show();
    if (!parser.positionalArguments().isEmpty())
        window.openFile(parser.positionalArguments().constFirst());
    if (parser.isSet(scriptOption))
        window.runScript(parser.value(scriptOption));
    if (parser.isSet(exportOption)) {
        if (!window.exportTo(parser.value(exportOption)))
            return 1;
        // Converting a file needs no window.
        if (!parser.isSet(screenshotOption))
            return 0;
    }

    if (parser.isSet(screenshotOption)) {
        const QString path = parser.value(screenshotOption);
        QTimer::singleShot(200, &window, [&window, path] {
            window.view()->zoomExtents();
            window.grab().save(path);
            QApplication::exit(0);
        });
    }
    return app.exec();
}
