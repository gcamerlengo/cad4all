// SPDX-License-Identifier: GPL-3.0-or-later
#include "cadview.h"
#include "mainwindow.h"
#include "view3d.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QIcon>
#include <QTimer>

namespace {

// macOS opens a drawing double-clicked in Finder with an event, not with
// an argument.
class FileOpener : public QObject {
public:
    explicit FileOpener(MainWindow& window) : window_(window) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::FileOpen) {
            window_.openFile(static_cast<QFileOpenEvent*>(event)->file());
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    MainWindow& window_;
};

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("CAD4All"));
    QApplication::setApplicationVersion(QStringLiteral(CAD4ALL_VERSION));
    QApplication::setOrganizationName(QStringLiteral("CAD4All"));
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256, 512})
        icon.addFile(QStringLiteral(":/resources/cad4all-%1.png").arg(size), QSize(size, size));
    QApplication::setWindowIcon(icon);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("CAD 2D e 3D parametrico"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("disegno"), QStringLiteral("Disegno .c4a, .dxf o .dwg da aprire."));
    const QCommandLineOption scriptOption(QStringLiteral("script"), QStringLiteral("Esegue uno script di comandi (.scr)."), QStringLiteral("file"));
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"), QStringLiteral("Salva un'immagine della finestra ed esce."), QStringLiteral("png"));
    const QCommandLineOption exportOption(QStringLiteral("export"), QStringLiteral("Salva il disegno (.c4a, .dxf, .pdf) o il modello 3D (.obj, .stl) dopo lo script."), QStringLiteral("file"));
    parser.addOption(scriptOption);
    parser.addOption(exportOption);
    parser.addOption(screenshotOption);
    const QCommandLineOption screenshot3dOption(QStringLiteral("screenshot3d"), QStringLiteral("Salva un'immagine della vista 3D (dopo lo script)."), QStringLiteral("png"));
    parser.addOption(screenshot3dOption);
    const QCommandLineOption profileOption(QStringLiteral("profilo"), QStringLiteral("Interfaccia: autocad (ribbon) o archicad (strumenti, Info Box, navigatore)."), QStringLiteral("nome"));
    parser.addOption(profileOption);
    const QCommandLineOption layoutOption(QStringLiteral("viste"), QStringLiteral("Vista 3D: pannello, affiancate o schede."), QStringLiteral("nome"));
    parser.addOption(layoutOption);
    parser.process(app);

    MainWindow window;
    FileOpener opener(window);
    app.installEventFilter(&opener);
    if (parser.isSet(profileOption) && !window.setProfile(parser.value(profileOption)))
        parser.showHelp(1);
    if (parser.isSet(layoutOption) && !window.setViewLayout(parser.value(layoutOption)))
        parser.showHelp(1);
    window.show();
    if (!parser.positionalArguments().isEmpty())
        window.openFile(parser.positionalArguments().constFirst());
    if (parser.isSet(scriptOption))
        window.runScript(parser.value(scriptOption));
    if (parser.isSet(screenshot3dOption)) {
        if (!window.view3d()->image(QSize(1200, 800)).save(parser.value(screenshot3dOption)))
            return 1;
        if (!parser.isSet(screenshotOption) && !parser.isSet(exportOption))
            return 0;
    }
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
