// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"

#include "cadview.h"
#include "commandline.h"
#include "icons.h"
#include "layerpanel.h"
#include "nativeformat.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

using namespace cad;

namespace {

const QString kFileFilter = QObject::tr("Disegni CAD4All (*.c4a)");

} // namespace

MainWindow::MainWindow()
    : engine_(doc_)
{
    view_ = new CadView(engine_, this);
    setCentralWidget(view_);

    commandLine_ = new CommandLine(this);
    auto* commandDock = new QDockWidget(tr("Riga di comando"), this);
    commandDock->setObjectName(QStringLiteral("commandDock"));
    commandDock->setWidget(commandLine_);
    commandDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::BottomDockWidgetArea, commandDock);

    layerPanel_ = new LayerPanel(engine_, this);
    layerDock_ = new QDockWidget(tr("Layer"), this);
    layerDock_->setObjectName(QStringLiteral("layerDock"));
    layerDock_->setWidget(layerPanel_);
    addDockWidget(Qt::RightDockWidgetArea, layerDock_);

    CommandInput* input = commandLine_->input();
    connect(input, &CommandInput::submitted, &engine_, &CommandEngine::submit);
    connect(input, &CommandInput::escapePressed, &engine_, &CommandEngine::cancel);
    connect(input, &CommandInput::deletePressed, &engine_, &CommandEngine::eraseSelection);
    connect(&engine_, &CommandEngine::promptChanged, commandLine_, &CommandLine::setPrompt);
    connect(&engine_, &CommandEngine::messageAdded, commandLine_, &CommandLine::appendMessage);
    connect(&engine_, &CommandEngine::changed, this, &MainWindow::updateTitle);
    connect(&engine_, &CommandEngine::settingsChanged, this, &MainWindow::updateToggles);
    connect(&engine_, &CommandEngine::layerManagerRequested, this, [this] {
        layerDock_->show();
        layerDock_->raise();
    });

    createActions();
    createStatusBar();

    commandLine_->setPrompt(engine_.prompt());
    commandLine_->appendMessage(tr("Benvenuto in CAD4All %1. Digitare ? per l'elenco dei comandi.").arg(QApplication::applicationVersion()));
    input->setFocus();
    resize(1280, 800);
    updateTitle();
}

QAction* MainWindow::commandAction(const QString& text, const QString& command, const QIcon& icon)
{
    auto* action = new QAction(icon, text, this);
    action->setStatusTip(tr("Comando %1").arg(command));
    action->setToolTip(QStringLiteral("%1 (%2)").arg(text, command));
    connect(action, &QAction::triggered, this, [this, command] {
        engine_.run(command);
        commandLine_->input()->setFocus();
    });
    return action;
}

void MainWindow::createActions()
{
    // File
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Nuovo"), QKeySequence::New, this, &MainWindow::newDrawing);
    file->addAction(tr("&Apri..."), QKeySequence::Open, this, &MainWindow::openDrawing);
    file->addAction(tr("&Salva"), QKeySequence::Save, this, &MainWindow::save);
    file->addAction(tr("Salva &con nome..."), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    file->addSeparator();
    file->addAction(tr("Esegui sc&ript..."), this, &MainWindow::runScriptDialog);
    file->addSeparator();
    file->addAction(tr("&Esci"), QKeySequence::Quit, this, &QWidget::close);

    // Edit
    QMenu* edit = menuBar()->addMenu(tr("&Modifica"));
    edit->addAction(tr("&Annulla"), QKeySequence::Undo, this, [this] { engine_.run(QStringLiteral("U")); });
    edit->addAction(tr("&Ripristina"), QKeySequence::Redo, this, [this] { engine_.run(QStringLiteral("REDO")); });
    edit->addSeparator();
    edit->addAction(tr("Seleziona &tutto"), QKeySequence::SelectAll, this, [this] {
        std::vector<EntityId> ids;
        for (const auto& [id, e] : doc_.entities())
            ids.push_back(id);
        engine_.select(ids);
    });

    // Draw
    QMenu* draw = menuBar()->addMenu(tr("&Disegna"));
    QToolBar* drawBar = addToolBar(tr("Disegna"));
    drawBar->setObjectName(QStringLiteral("drawToolBar"));
    const QList<QAction*> drawActions = {
        commandAction(tr("Linea"), QStringLiteral("LINE"), icons::make(QStringLiteral("line"))),
        commandAction(tr("Polilinea"), QStringLiteral("PLINE"), icons::make(QStringLiteral("polyline"))),
        commandAction(tr("Cerchio"), QStringLiteral("CIRCLE"), icons::make(QStringLiteral("circle"))),
        commandAction(tr("Arco"), QStringLiteral("ARC"), icons::make(QStringLiteral("arc"))),
        commandAction(tr("Rettangolo"), QStringLiteral("RECTANG"), icons::make(QStringLiteral("rectangle"))),
    };
    draw->addActions(drawActions);
    drawBar->addActions(drawActions);

    // Modify
    QMenu* modify = menuBar()->addMenu(tr("&Edita"));
    QToolBar* modifyBar = addToolBar(tr("Edita"));
    modifyBar->setObjectName(QStringLiteral("modifyToolBar"));
    const QList<QAction*> modifyActions = {
        commandAction(tr("Cancella"), QStringLiteral("ERASE"), icons::make(QStringLiteral("erase"))),
        commandAction(tr("Sposta"), QStringLiteral("MOVE"), icons::make(QStringLiteral("move"))),
        commandAction(tr("Copia"), QStringLiteral("COPY"), icons::make(QStringLiteral("copy"))),
        commandAction(tr("Ruota"), QStringLiteral("ROTATE"), icons::make(QStringLiteral("rotate"))),
        commandAction(tr("Scala"), QStringLiteral("SCALE"), icons::make(QStringLiteral("scale"))),
        commandAction(tr("Specchio"), QStringLiteral("MIRROR"), icons::make(QStringLiteral("mirror"))),
    };
    modify->addActions(modifyActions);
    modifyBar->addActions(modifyActions);

    // View
    QMenu* viewMenu = menuBar()->addMenu(tr("&Visualizza"));
    QAction* zoom = viewMenu->addAction(icons::make(QStringLiteral("zoomExtents")), tr("Zoom &estensioni"), this,
                                        [this] { view_->zoomExtents(); });
    modifyBar->addSeparator();
    modifyBar->addAction(zoom);
    viewMenu->addSeparator();

    auto toggle = [&](const QString& text, QKeySequence key, bool on, auto slot) {
        QAction* a = viewMenu->addAction(text);
        a->setCheckable(true);
        a->setChecked(on);
        a->setShortcut(key);
        a->setShortcutContext(Qt::ApplicationShortcut);
        connect(a, &QAction::toggled, this, slot);
        return a;
    };
    QAction* osnap = toggle(tr("Snap ad &oggetto"), QKeySequence(Qt::Key_F3), engine_.osnapEnabled(),
                            [this](bool on) { engine_.setOsnapEnabled(on); });
    QAction* grid = toggle(tr("&Griglia"), QKeySequence(Qt::Key_F7), view_->gridVisible(), [this](bool on) {
        view_->setGridVisible(on);
        updateToggles();
    });
    QAction* ortho = toggle(tr("O&rto"), QKeySequence(Qt::Key_F8), engine_.orthoEnabled(),
                            [this](bool on) { engine_.setOrthoEnabled(on); });
    viewMenu->addSeparator();
    viewMenu->addAction(layerDock_->toggleViewAction());

    // The status bar buttons share these actions.
    osnapButton_ = new QToolButton(this);
    osnapButton_->setDefaultAction(osnap);
    osnapButton_->setText(tr("SNAP"));
    orthoButton_ = new QToolButton(this);
    orthoButton_->setDefaultAction(ortho);
    orthoButton_->setText(tr("ORTO"));
    gridButton_ = new QToolButton(this);
    gridButton_->setDefaultAction(grid);
    gridButton_->setText(tr("GRIGLIA"));

    QMenu* help = menuBar()->addMenu(tr("&Aiuto"));
    help->addAction(tr("&Informazioni su CAD4All"), this, &MainWindow::about);
}

void MainWindow::createStatusBar()
{
    coordinates_ = new QLabel(this);
    coordinates_->setMinimumWidth(260);
    statusBar()->addWidget(coordinates_);
    for (QToolButton* b : {osnapButton_, orthoButton_, gridButton_}) {
        b->setFocusPolicy(Qt::NoFocus);
        b->setAutoRaise(true);
        statusBar()->addPermanentWidget(b);
    }
    connect(view_, &CadView::cursorMoved, this, [this](Vec2 p) {
        coordinates_->setText(QStringLiteral("X: %1   Y: %2").arg(p.x, 0, 'f', 4).arg(p.y, 0, 'f', 4));
    });
    updateToggles();
}

void MainWindow::updateToggles()
{
    // QToolButton resets its text from the action; keep the short labels.
    osnapButton_->setText(tr("SNAP"));
    orthoButton_->setText(tr("ORTO"));
    gridButton_->setText(tr("GRIGLIA"));
}

void MainWindow::updateTitle()
{
    const QString name = filePath_.isEmpty() ? tr("Senza nome") : QFileInfo(filePath_).fileName();
    setWindowTitle(QStringLiteral("%1[*] - CAD4All").arg(name));
    setWindowModified(doc_.isModified());
}

void MainWindow::newDrawing()
{
    if (!maybeSave())
        return;
    engine_.cancel();
    engine_.clearSelection();
    doc_.reset();
    filePath_.clear();
    view_->zoomExtents();
    engine_.documentChanged();
}

void MainWindow::openDrawing()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Apri disegno"), QString(), kFileFilter);
    if (!path.isEmpty())
        openFile(path);
}

bool MainWindow::openFile(const QString& path)
{
    engine_.cancel();
    engine_.clearSelection();
    QString message;
    if (!io::loadNative(doc_, path, &message)) {
        QMessageBox::warning(this, tr("Apri disegno"), tr("Impossibile aprire %1:\n%2").arg(path, message));
        return false;
    }
    if (!message.isEmpty())
        commandLine_->appendMessage(message);
    filePath_ = path;
    view_->zoomExtents();
    engine_.documentChanged();
    commandLine_->appendMessage(tr("Aperto %1").arg(QFileInfo(path).fileName()));
    return true;
}

bool MainWindow::save()
{
    if (filePath_.isEmpty())
        return saveAs();
    QString error;
    if (!io::saveNative(doc_, filePath_, &error)) {
        QMessageBox::warning(this, tr("Salva"), tr("Impossibile salvare %1:\n%2").arg(filePath_, error));
        return false;
    }
    doc_.setModified(false);
    commandLine_->appendMessage(tr("Salvato %1").arg(QFileInfo(filePath_).fileName()));
    updateTitle();
    return true;
}

bool MainWindow::saveAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Salva con nome"), filePath_, kFileFilter);
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QLatin1String(".c4a"), Qt::CaseInsensitive))
        path += QLatin1String(".c4a");
    filePath_ = path;
    return save();
}

bool MainWindow::maybeSave()
{
    if (!doc_.isModified())
        return true;
    const auto answer = QMessageBox::question(this, tr("CAD4All"), tr("Salvare le modifiche al disegno?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}

void MainWindow::runScriptDialog()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Esegui script"), QString(), tr("Script (*.scr);;Tutti i file (*)"));
    if (!path.isEmpty())
        runScript(path);
}

bool MainWindow::runScript(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        commandLine_->appendMessage(tr("Impossibile leggere lo script %1").arg(path));
        return false;
    }
    QString content = QString::fromUtf8(file.readAll());
    content.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    if (content.endsWith(QLatin1Char('\n')))
        content.chop(1);
    // As in AutoCAD scripts, each space and each line break is an Enter.
    for (const QString& line : content.split(QLatin1Char('\n'))) {
        if (line.trimmed().startsWith(QLatin1Char(';')))
            continue; // comment
        for (const QString& token : line.split(QLatin1Char(' ')))
            engine_.submit(token);
    }
    return true;
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("Informazioni su CAD4All"),
                       tr("<h3>CAD4All %1</h3>"
                          "<p>CAD 2D e 3D parametrico per tecnici.</p>"
                          "<p>Software libero distribuito con licenza GNU GPL versione 3 o successiva. "
                          "Non c'è alcuna garanzia.</p>")
                           .arg(QApplication::applicationVersion()));
}
