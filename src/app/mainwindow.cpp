// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"

#include "cadview.h"
#include "commandline.h"
#include "icons.h"
#include "infobox.h"
#include "layerpanel.h"
#include "navigator.h"
#include "ribbon.h"
#include "stylecontrols.h"
#include "toolbox.h"
#include "dwgreader.h"
#include "dxf.h"
#include "meshexport.h"
#include "nativeformat.h"
#include "plot.h"
#include "plotdialog.h"
#include "recovery.h"
#include "view3d.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace cad;

namespace {

const QString kOpenFilter = QObject::tr("Disegni (*.c4a *.dxf *.dwg);;Disegni CAD4All (*.c4a);;DXF (*.dxf);;DWG di AutoCAD (*.dwg)");
const QString kNativeFilter = QObject::tr("Disegni CAD4All (*.c4a)");
const QString kDxfFilter = QObject::tr("DXF R12 per AutoCAD e altri CAD (*.dxf)");

bool isDxf(const QString& path)
{
    return path.endsWith(QLatin1String(".dxf"), Qt::CaseInsensitive);
}

bool isDwg(const QString& path)
{
    return path.endsWith(QLatin1String(".dwg"), Qt::CaseInsensitive);
}

bool isPdf(const QString& path)
{
    return path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive);
}

} // namespace

MainWindow::MainWindow()
    : engine_(doc_)
{
    view_ = new CadView(engine_, this);
    view3d_ = new View3D(doc_, this);
    // The plan alone, the plan with the 3D view beside it, or both in tabs.
    central_ = new QSplitter(Qt::Horizontal, this);
    central_->setChildrenCollapsible(false);
    viewTabs_ = new QTabWidget(central_);
    viewTabs_->setDocumentMode(true);
    viewTabs_->setTabPosition(QTabWidget::South);
    viewTabs_->hide();
    central_->addWidget(viewTabs_);
    central_->addWidget(view_);
    setCentralWidget(central_);

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

    navigator_ = new Navigator(doc_, this);
    navigatorDock_ = new QDockWidget(tr("Navigatore"), this);
    navigatorDock_->setObjectName(QStringLiteral("navigatorDock"));
    navigatorDock_->setWidget(navigator_);
    addDockWidget(Qt::RightDockWidgetArea, navigatorDock_);
    navigatorDock_->hide();

    view3dDock_ = new QDockWidget(tr("Vista 3D"), this);
    view3dDock_->setObjectName(QStringLiteral("view3dDock"));
    view3dDock_->setWidget(view3d_);
    addDockWidget(Qt::RightDockWidgetArea, view3dDock_);
    view3dDock_->hide();

    infoBox_ = new InfoBox(engine_, this);
    infoDock_ = new QDockWidget(tr("Info Box"), this);
    infoDock_->setObjectName(QStringLiteral("infoDock"));
    infoDock_->setWidget(infoBox_);
    addDockWidget(Qt::TopDockWidgetArea, infoDock_);
    infoDock_->hide();

    CommandInput* input = commandLine_->input();
    connect(input, &CommandInput::submitted, &engine_, &CommandEngine::submit);
    connect(input, &CommandInput::escapePressed, &engine_, &CommandEngine::cancel);
    connect(input, &CommandInput::deletePressed, &engine_, &CommandEngine::eraseSelection);
    connect(&engine_, &CommandEngine::promptChanged, commandLine_, &CommandLine::setPrompt);
    connect(&engine_, &CommandEngine::messageAdded, commandLine_, &CommandLine::appendMessage);
    connect(&engine_, &CommandEngine::changed, this, &MainWindow::updateTitle);
    // While TEXT waits for the text itself, Space types a space.
    connect(&engine_, &CommandEngine::changed, input, [this, input] { input->setSpaceIsText(engine_.wantsText()); });
    connect(&engine_, &CommandEngine::settingsChanged, this, &MainWindow::updateToggles);
    connect(&engine_, &CommandEngine::plotRequested, this, &MainWindow::plot, Qt::QueuedConnection);
    connect(&engine_, &CommandEngine::changed, this, [this] {
        updateStories();
        view3d_->refresh();
        // Only the side panel opens by itself: a tab would hide the plan being drawn.
        if (view3d_->hasModel() && !view3dShown_ && viewLayout_ == ViewLayout::Docked)
            showView3d();
    });
    connect(&engine_, &CommandEngine::view3dRequested, this, &MainWindow::showView3d);
    connect(&engine_, &CommandEngine::layerManagerRequested, this, [this] {
        layerDock_->show();
        layerDock_->raise();
    });
    connect(navigator_, &Navigator::storyChosen, this, [this](StoryId id) {
        setStory(id);
        showPlan();
    });
    connect(navigator_, &Navigator::planChosen, this, &MainWindow::showPlan);
    connect(navigator_, &Navigator::view3dChosen, this, &MainWindow::showView3d);

    createActions();
    createRibbon();
    createToolbox();
    createStatusBar();

    const QSettings settings;
    const QString profile = settings.value(QStringLiteral("interfaccia/profilo"), QStringLiteral("autocad")).toString();
    setProfile(profile == QLatin1String("archicad") ? Profile::ArchiCad : Profile::AutoCad);
    setViewLayout(settings.value(QStringLiteral("interfaccia/viste")).toString());

    // Like AutoCAD's SAVETIME, in minutes; 0 turns it off.
    autosaveTimer_ = new QTimer(this);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::autosave);
    const int minutes = settings.value(QStringLiteral("salvataggio/minuti"), 5).toInt();
    if (minutes > 0)
        autosaveTimer_->start(minutes * 60 * 1000);

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
    commands_.insert(command, action);
    return action;
}

QComboBox* MainWindow::makeStoryCombo(QWidget* parent)
{
    auto* combo = new QComboBox(parent);
    combo->setToolTip(tr("Piano corrente"));
    combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    combo->setFocusPolicy(Qt::ClickFocus);
    connect(combo, &QComboBox::activated, this, [this, combo](int index) {
        setStory(combo->itemData(index).toInt());
    });
    storyCombos_.push_back(combo);
    updateStories();
    return combo;
}

// The combo boxes list the stories from the top, like a section of the building.
void MainWindow::updateStories()
{
    navigator_->refresh();
    QStringList names;
    for (auto it = doc_.stories().rbegin(); it != doc_.stories().rend(); ++it)
        names << tr("%1  (%2)").arg(QString::fromStdString(it->name)).arg(it->elevation);
    for (QComboBox* combo : storyCombos_) {
        QStringList shown;
        for (int i = 0; i < combo->count(); ++i)
            shown << combo->itemText(i);
        const QSignalBlocker block(combo);
        if (names != shown) {
            combo->clear();
            int k = 0;
            for (auto it = doc_.stories().rbegin(); it != doc_.stories().rend(); ++it)
                combo->addItem(names[k++], it->id);
        }
        combo->setCurrentIndex(combo->findData(doc_.currentStory()));
    }
}

void MainWindow::setStory(StoryId id)
{
    if (id == doc_.currentStory())
        return;
    // A command half done on one story makes no sense on another.
    engine_.cancel();
    if (doc_.setCurrentStory(id)) {
        const Story* s = doc_.story(id);
        commandLine_->appendMessage(tr("Piano corrente: %1 (quota %2)").arg(QString::fromStdString(s->name)).arg(s->elevation));
    }
    engine_.documentChanged();
    view_->update();
    commandLine_->input()->setFocus();
}

void MainWindow::changeStory(int step)
{
    const Story* s = step > 0 ? doc_.storyAbove(doc_.currentStory()) : doc_.storyBelow(doc_.currentStory());
    if (s)
        setStory(s->id);
}

void MainWindow::showView3d()
{
    view3dShown_ = true;
    if (viewLayout_ == ViewLayout::Docked) {
        view3dDock_->show();
        view3dDock_->raise();
    } else if (viewLayout_ == ViewLayout::Tabs) {
        viewTabs_->setCurrentWidget(view3d_);
    }
    view3d_->refresh();
}

void MainWindow::showPlan()
{
    if (viewLayout_ == ViewLayout::Tabs)
        viewTabs_->setCurrentWidget(view_);
    commandLine_->input()->setFocus();
}

void MainWindow::setProfile(Profile profile, bool remember)
{
    profile_ = profile;
    const bool archicad = profile == Profile::ArchiCad;
    ribbonBar_->setVisible(!archicad);
    for (QToolBar* bar : classicBars_)
        bar->setVisible(archicad && bar->property("archicad").toBool());
    toolboxDock_->setVisible(archicad);
    infoDock_->setVisible(archicad);
    navigatorDock_->setVisible(archicad);
    layerDock_->setVisible(!archicad);
    profiles_->actions().at(archicad ? 1 : 0)->setChecked(true);
    if (remember) {
        QSettings().setValue(QStringLiteral("interfaccia/profilo"), archicad ? QStringLiteral("archicad") : QStringLiteral("autocad"));
        // Each profile comes with its usual place for the 3D view.
        setViewLayout(archicad ? ViewLayout::SideBySide : ViewLayout::Docked, true);
    }
}

bool MainWindow::setProfile(const QString& name)
{
    const QString n = name.toLower();
    if (n != QLatin1String("autocad") && n != QLatin1String("archicad"))
        return false;
    setProfile(n == QLatin1String("archicad") ? Profile::ArchiCad : Profile::AutoCad);
    setViewLayout(QString());
    return true;
}

namespace {

const QStringList kLayoutNames = {QStringLiteral("pannello"), QStringLiteral("affiancate"), QStringLiteral("schede")};

} // namespace

bool MainWindow::setViewLayout(const QString& name)
{
    const qsizetype i = kLayoutNames.indexOf(name.toLower());
    if (i < 0 && !name.isEmpty())
        return false;
    // No name: the usual one for the profile.
    if (i < 0)
        setViewLayout(profile_ == Profile::ArchiCad ? ViewLayout::SideBySide : ViewLayout::Docked);
    else
        setViewLayout(static_cast<ViewLayout>(i));
    return true;
}

void MainWindow::setViewLayout(ViewLayout layout, bool remember)
{
    viewLayout_ = layout;
    // addTab, addWidget and setWidget take each view from where it was.
    viewTabs_->clear();
    if (layout == ViewLayout::Tabs) {
        view3dDock_->hide();
        viewTabs_->addTab(view_, icons::make(QStringLiteral("plan")), tr("Pianta"));
        viewTabs_->addTab(view3d_, icons::make(QStringLiteral("view3d")), tr("Vista 3D"));
        viewTabs_->show();
    } else {
        viewTabs_->hide();
        central_->addWidget(view_);
        if (layout == ViewLayout::SideBySide) {
            view3dDock_->hide();
            central_->addWidget(view3d_);
            const int w = std::max(central_->width(), 1000);
            central_->setSizes({0, w * 11 / 20, w * 9 / 20});
        } else {
            view3dDock_->setWidget(view3d_);
            view3dDock_->setVisible(view3dShown_);
        }
    }
    view_->show();
    view3d_->show();
    view3dDock_->toggleViewAction()->setEnabled(layout == ViewLayout::Docked);
    layouts_->actions().at(static_cast<int>(layout))->setChecked(true);
    if (remember)
        QSettings().setValue(QStringLiteral("interfaccia/viste"), kLayoutNames[static_cast<int>(layout)]);
    view3d_->refresh();
}

void MainWindow::createActions()
{
    auto named = [this](const QString& key, const QString& icon, const QString& text, auto slot, QKeySequence key2 = {}) {
        auto* a = new QAction(icon.isEmpty() ? QIcon() : icons::make(icon), text, this);
        a->setShortcut(key2);
        connect(a, &QAction::triggered, this, slot);
        named_.insert(key, a);
        return a;
    };
    named(QStringLiteral("new"), QStringLiteral("new"), tr("&Nuovo"), [this] { newDrawing(); }, QKeySequence::New);
    named(QStringLiteral("open"), QStringLiteral("open"), tr("&Apri..."), [this] { openDrawing(); }, QKeySequence::Open);
    named(QStringLiteral("save"), QStringLiteral("save"), tr("&Salva"), [this] { save(); }, QKeySequence::Save);
    named(QStringLiteral("saveAs"), QString(), tr("Salva &con nome..."), [this] { saveAs(); }, QKeySequence::SaveAs);
    named(QStringLiteral("exportDxf"), QStringLiteral("export"), tr("Esporta &DXF..."), [this] { exportDxf(); });
    named(QStringLiteral("exportModel"), QStringLiteral("view3d"), tr("Esporta &modello 3D (OBJ, STL)..."), [this] { exportModel(); });
    named(QStringLiteral("print"), QStringLiteral("print"), tr("S&tampa..."), [this] { plot(); }, QKeySequence::Print);
    named(QStringLiteral("script"), QStringLiteral("script"), tr("Esegui sc&ript..."), [this] { runScriptDialog(); });
    named(QStringLiteral("undo"), QStringLiteral("undo"), tr("&Annulla"), [this] { engine_.run(QStringLiteral("U")); }, QKeySequence::Undo);
    named(QStringLiteral("redo"), QStringLiteral("redo"), tr("&Ripristina"), [this] { engine_.run(QStringLiteral("REDO")); }, QKeySequence::Redo);
    named(QStringLiteral("zoom"), QStringLiteral("zoomExtents"), tr("Zoom &estensioni"), [this] { view_->zoomExtents(); });
    named(QStringLiteral("view3d"), QStringLiteral("view3d"), tr("Vista &3D"), [this] { showView3d(); })->setToolTip(tr("Vista 3D (VISTA3D)"));
    named(QStringLiteral("plan"), QStringLiteral("plan"), tr("&Pianta"), [this] { showPlan(); });
    named(QStringLiteral("storyUp"), QString(), tr("Piano s&uperiore"), [this] { changeStory(+1); }, QKeySequence(Qt::CTRL | Qt::Key_PageUp));
    named(QStringLiteral("storyDown"), QString(), tr("Piano &inferiore"), [this] { changeStory(-1); }, QKeySequence(Qt::CTRL | Qt::Key_PageDown));
    named(QStringLiteral("layers"), QStringLiteral("layers"), tr("&Layer"), [this] {
        layerDock_->show();
        layerDock_->raise();
    });

    commandAction(tr("Linea"), QStringLiteral("LINE"), icons::make(QStringLiteral("line")));
    commandAction(tr("Polilinea"), QStringLiteral("PLINE"), icons::make(QStringLiteral("polyline")));
    commandAction(tr("Cerchio"), QStringLiteral("CIRCLE"), icons::make(QStringLiteral("circle")));
    commandAction(tr("Arco"), QStringLiteral("ARC"), icons::make(QStringLiteral("arc")));
    commandAction(tr("Rettangolo"), QStringLiteral("RECTANG"), icons::make(QStringLiteral("rectangle")));
    commandAction(tr("Ellisse"), QStringLiteral("ELLIPSE"), icons::make(QStringLiteral("ellipse")));
    commandAction(tr("Spline"), QStringLiteral("SPLINE"), icons::make(QStringLiteral("spline")));
    commandAction(tr("Tratteggio"), QStringLiteral("HATCH"), icons::make(QStringLiteral("hatch")));
    commandAction(tr("Modifica tratteggio"), QStringLiteral("HATCHEDIT"));
    commandAction(tr("Muro"), QStringLiteral("WALL"), icons::make(QStringLiteral("wall")));
    commandAction(tr("Porta"), QStringLiteral("DOOR"), icons::make(QStringLiteral("door")));
    commandAction(tr("Finestra"), QStringLiteral("WINDOW"), icons::make(QStringLiteral("window")));
    commandAction(tr("Solaio"), QStringLiteral("SLAB"), icons::make(QStringLiteral("slab")));
    commandAction(tr("Tetto"), QStringLiteral("ROOF"), icons::make(QStringLiteral("roof")));
    commandAction(tr("Scala"), QStringLiteral("STAIR"), icons::make(QStringLiteral("stair")));
    commandAction(tr("Modifica muro"), QStringLiteral("WALLEDIT"));
    commandAction(tr("Piani..."), QStringLiteral("STORY"), icons::make(QStringLiteral("story")));
    commandAction(tr("Cancella"), QStringLiteral("ERASE"), icons::make(QStringLiteral("erase")));
    commandAction(tr("Sposta"), QStringLiteral("MOVE"), icons::make(QStringLiteral("move")));
    commandAction(tr("Copia"), QStringLiteral("COPY"), icons::make(QStringLiteral("copy")));
    commandAction(tr("Ruota"), QStringLiteral("ROTATE"), icons::make(QStringLiteral("rotate")));
    commandAction(tr("Scala"), QStringLiteral("SCALE"), icons::make(QStringLiteral("scale")));
    commandAction(tr("Specchio"), QStringLiteral("MIRROR"), icons::make(QStringLiteral("mirror")));
    commandAction(tr("Offset"), QStringLiteral("OFFSET"), icons::make(QStringLiteral("offset")));
    commandAction(tr("Taglia"), QStringLiteral("TRIM"), icons::make(QStringLiteral("trim")));
    commandAction(tr("Estendi"), QStringLiteral("EXTEND"), icons::make(QStringLiteral("extend")));
    commandAction(tr("Raccordo"), QStringLiteral("FILLET"), icons::make(QStringLiteral("fillet")));
    commandAction(tr("Cima"), QStringLiteral("CHAMFER"), icons::make(QStringLiteral("chamfer")));
    commandAction(tr("Stira"), QStringLiteral("STRETCH"), icons::make(QStringLiteral("stretch")));
    commandAction(tr("Serie"), QStringLiteral("ARRAY"), icons::make(QStringLiteral("array")));
    commandAction(tr("Esplodi"), QStringLiteral("EXPLODE"), icons::make(QStringLiteral("explode")));
    commandAction(tr("Unisci"), QStringLiteral("JOIN"), icons::make(QStringLiteral("join")));
    commandAction(tr("Direttrice"), QStringLiteral("LEADER"), icons::make(QStringLiteral("leader")));
    commandAction(tr("Distanza"), QStringLiteral("DIST"), icons::make(QStringLiteral("measure")));
    commandAction(tr("Area"), QStringLiteral("AREA"));
    commandAction(tr("Lista"), QStringLiteral("LIST"));
    commandAction(tr("Testo"), QStringLiteral("TEXT"), icons::make(QStringLiteral("text")));
    commandAction(tr("Modifica testo"), QStringLiteral("TEXTEDIT"));
    commandAction(tr("Quota lineare"), QStringLiteral("DIMLINEAR"), icons::make(QStringLiteral("dimLinear")));
    commandAction(tr("Quota allineata"), QStringLiteral("DIMALIGNED"), icons::make(QStringLiteral("dimAligned")));
    commandAction(tr("Quota raggio"), QStringLiteral("DIMRADIUS"), icons::make(QStringLiteral("dimRadius")));
    commandAction(tr("Quota diametro"), QStringLiteral("DIMDIAMETER"), icons::make(QStringLiteral("dimDiameter")));
    commandAction(tr("Scala quote"), QStringLiteral("DIMSCALE"));
    commandAction(tr("Corrispondenza proprietà"), QStringLiteral("MATCHPROP"), icons::make(QStringLiteral("matchProperties")));
    commandAction(tr("Cambia proprietà"), QStringLiteral("CHPROP"));
    commandAction(tr("Scala tipo di linea"), QStringLiteral("LTSCALE"));

    auto actions = [this](std::initializer_list<const char*> names) {
        QList<QAction*> list;
        for (const char* n : names) {
            const QString name = QString::fromLatin1(n);
            list << (commands_.contains(name) ? commands_.value(name) : named_.value(name));
        }
        return list;
    };
    const auto drawActions = actions({"LINE", "PLINE", "CIRCLE", "ARC", "RECTANG", "ELLIPSE", "SPLINE", "HATCH"});
    const auto architectureActions = actions({"WALL", "DOOR", "WINDOW", "SLAB", "ROOF", "STAIR"});
    const auto modifyActions = actions({"ERASE", "MOVE", "COPY", "ROTATE", "SCALE", "MIRROR", "OFFSET", "TRIM", "EXTEND", "FILLET", "CHAMFER", "STRETCH", "ARRAY", "EXPLODE", "JOIN"});
    const auto annotateActions = actions({"TEXT", "DIMLINEAR", "DIMALIGNED", "DIMRADIUS", "DIMDIAMETER"});

    // Menus
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addActions(actions({"new", "open", "save", "saveAs", "exportDxf", "exportModel"}));
    file->addSeparator();
    file->addAction(named_.value(QStringLiteral("print")));
    file->addSeparator();
    file->addAction(named_.value(QStringLiteral("script")));
    file->addSeparator();
    file->addAction(tr("&Esci"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* edit = menuBar()->addMenu(tr("&Modifica"));
    edit->addActions(actions({"undo", "redo"}));
    edit->addSeparator();
    edit->addAction(tr("Seleziona &tutto"), QKeySequence::SelectAll, this, [this] {
        std::vector<EntityId> ids;
        for (const auto& [id, e] : doc_.entities())
            ids.push_back(id);
        engine_.select(ids);
    });

    QMenu* draw = menuBar()->addMenu(tr("&Disegna"));
    draw->addActions(drawActions);
    draw->addAction(command(QStringLiteral("HATCHEDIT")));

    QMenu* architecture = menuBar()->addMenu(tr("A&rchitettura"));
    architecture->addActions(architectureActions);
    architecture->addAction(command(QStringLiteral("WALLEDIT")));
    architecture->addSeparator();
    architecture->addActions(actions({"STORY", "storyUp", "storyDown"}));
    architecture->addSeparator();
    architecture->addAction(named_.value(QStringLiteral("view3d")));

    QMenu* modify = menuBar()->addMenu(tr("&Edita"));
    modify->addActions(modifyActions);

    modify->addSeparator();
    modify->addActions(actions({"MATCHPROP", "CHPROP", "LTSCALE"}));
    modify->addSeparator();
    QMenu* inquiry = modify->addMenu(tr("&Interroga"));
    inquiry->addActions(actions({"DIST", "AREA", "LIST"}));

    QMenu* annotate = menuBar()->addMenu(tr("&Annota"));
    annotate->addActions(annotateActions);
    annotate->addSeparator();
    annotate->addActions(actions({"TEXTEDIT", "DIMSCALE", "LEADER"}));

    // Classic toolbars: the ArchiCAD profile shows some of them, the others
    // are there for whoever wants them.
    auto toolBar = [this](const QString& title, const char* name, const QList<QAction*>& list, bool archicad) {
        QToolBar* bar = addToolBar(title);
        bar->setObjectName(QString::fromLatin1(name));
        bar->addActions(list);
        bar->setProperty("archicad", archicad);
        classicBars_.push_back(bar);
        return bar;
    };
    standardBar_ = toolBar(tr("Standard"), "standardToolBar", actions({"new", "open", "save", "print", "undo", "redo"}), true);
    toolBar(tr("Disegna"), "drawToolBar", drawActions, false);
    QToolBar* architectureBar = toolBar(tr("Architettura"), "architectureToolBar", architectureActions, false);
    architectureBar->addSeparator();
    architectureBar->addAction(command(QStringLiteral("STORY")));
    architectureBar->addWidget(makeStoryCombo(architectureBar));
    architectureBar->addAction(named_.value(QStringLiteral("view3d")));
    // ArchiCAD has its tools in the Toolbox: on top only the stories and the 3D.
    QToolBar* storyBar = toolBar(tr("Piani"), "storyToolBar", actions({"STORY"}), true);
    storyBar->addWidget(makeStoryCombo(storyBar));
    storyBar->addActions(actions({"plan", "view3d"}));
    // Layer, colour, linetype and lineweight, which the ribbon has in its Base tab.
    QToolBar* propertiesBar = toolBar(tr("Proprietà"), "propertiesToolBar", actions({"layers"}), true);
    auto* barStyle = new StyleControls(engine_, propertiesBar);
    for (QComboBox* c : {barStyle->layerCombo(), barStyle->colorCombo(), barStyle->linetypeCombo(), barStyle->lineweightCombo()})
        propertiesBar->addWidget(c);
    propertiesBar->addAction(command(QStringLiteral("MATCHPROP")));
    QToolBar* modifyBar = toolBar(tr("Edita"), "modifyToolBar", modifyActions, true);
    modifyBar->addSeparator();
    modifyBar->addAction(named_.value(QStringLiteral("zoom")));
    toolBar(tr("Annota"), "annotateToolBar", annotateActions, false);

    // View
    QMenu* viewMenu = menuBar()->addMenu(tr("&Visualizza"));
    viewMenu->addAction(named_.value(QStringLiteral("zoom")));
    viewMenu->addSeparator();

    auto toggle = [&](const QString& key, const QString& text, QKeySequence shortcut, bool on, auto slot) {
        QAction* a = viewMenu->addAction(text);
        a->setCheckable(true);
        a->setChecked(on);
        a->setShortcut(shortcut);
        a->setShortcutContext(Qt::ApplicationShortcut);
        connect(a, &QAction::toggled, this, slot);
        named_.insert(key, a);
        return a;
    };
    QAction* osnap = toggle(QStringLiteral("osnap"), tr("Snap ad &oggetto"), QKeySequence(Qt::Key_F3), engine_.osnapEnabled(),
                            [this](bool on) { engine_.setOsnapEnabled(on); });
    QAction* grid = toggle(QStringLiteral("grid"), tr("&Griglia"), QKeySequence(Qt::Key_F7), view_->gridVisible(), [this](bool on) {
        view_->setGridVisible(on);
        updateToggles();
    });
    QAction* ortho = toggle(QStringLiteral("ortho"), tr("O&rto"), QKeySequence(Qt::Key_F8), engine_.orthoEnabled(),
                            [this](bool on) { engine_.setOrthoEnabled(on); });
    QAction* lineweights = toggle(QStringLiteral("lineweights"), tr("&Spessori di linea"), QKeySequence(), view_->lineweightsVisible(),
                                  [this](bool on) {
                                      view_->setLineweightsVisible(on);
                                      updateToggles();
                                  });
    viewMenu->addSeparator();

    QMenu* profileMenu = viewMenu->addMenu(tr("&Interfaccia"));
    profiles_ = new QActionGroup(this);
    for (const auto& [text, profile] : {std::pair{tr("Come &AutoCAD: ribbon"), Profile::AutoCad},
                                        std::pair{tr("Come A&rchiCAD: strumenti, Info Box e navigatore"), Profile::ArchiCad}}) {
        QAction* a = profileMenu->addAction(text);
        a->setCheckable(true);
        profiles_->addAction(a);
        connect(a, &QAction::triggered, this, [this, profile] { setProfile(profile, true); });
    }
    QMenu* layoutMenu = viewMenu->addMenu(tr("Vista &3D"));
    layouts_ = new QActionGroup(this);
    for (const auto& [text, layout] : {std::pair{tr("In un &pannello"), ViewLayout::Docked},
                                       std::pair{tr("&Affiancata alla pianta"), ViewLayout::SideBySide},
                                       std::pair{tr("A &schede"), ViewLayout::Tabs}}) {
        QAction* a = layoutMenu->addAction(text);
        a->setCheckable(true);
        layouts_->addAction(a);
        connect(a, &QAction::triggered, this, [this, layout] { setViewLayout(layout, true); });
    }
    viewMenu->addSeparator();
    viewMenu->addAction(layerDock_->toggleViewAction());
    viewMenu->addAction(view3dDock_->toggleViewAction());
    viewMenu->addAction(navigatorDock_->toggleViewAction());
    viewMenu->addAction(infoDock_->toggleViewAction());
    // Filled when the ribbon and the Toolbox exist.
    named_.insert(QStringLiteral("panels"), viewMenu->menuAction());

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
    lineweightButton_ = new QToolButton(this);
    lineweightButton_->setDefaultAction(lineweights);
    lineweightButton_->setText(tr("SPESSORI"));

    QMenu* help = menuBar()->addMenu(tr("&Aiuto"));
    help->addAction(tr("&Segnala un problema..."), this, &MainWindow::reportProblem);
    help->addSeparator();
    help->addAction(tr("&Licenza"), this, &MainWindow::showLicence);
    help->addAction(tr("Informazioni su &Qt"), qApp, &QApplication::aboutQt);
    help->addSeparator();
    help->addAction(tr("&Informazioni su CAD4All"), this, &MainWindow::about);
}

void MainWindow::createRibbon()
{
    ribbon_ = new Ribbon(this);
    auto large = [this](RibbonPanel* panel, std::initializer_list<const char*> names) {
        for (const char* n : names) {
            const QString name = QString::fromLatin1(n);
            panel->addLarge(commands_.contains(name) ? commands_.value(name) : named_.value(name));
        }
    };
    auto small = [this](RibbonPanel* panel, std::initializer_list<const char*> names) {
        for (const char* n : names) {
            const QString name = QString::fromLatin1(n);
            panel->addSmall(commands_.contains(name) ? commands_.value(name) : named_.value(name));
        }
    };

    QWidget* home = ribbon_->addPage(tr("Base"));
    RibbonPanel* p = ribbon_->addPanel(home, tr("Disegna"));
    large(p, {"LINE", "PLINE", "CIRCLE", "ARC"});
    small(p, {"RECTANG", "ELLIPSE", "SPLINE", "HATCH", "HATCHEDIT"});
    p = ribbon_->addPanel(home, tr("Edita"));
    small(p, {"MOVE", "COPY", "STRETCH", "ROTATE", "MIRROR", "SCALE", "TRIM", "EXTEND", "OFFSET", "FILLET", "CHAMFER", "ERASE",
              "ARRAY", "EXPLODE", "JOIN"});
    p = ribbon_->addPanel(home, tr("Annotazione"));
    large(p, {"TEXT", "DIMLINEAR"});
    p = ribbon_->addPanel(home, tr("Layer"));
    large(p, {"layers"});
    auto* style = new StyleControls(engine_, p);
    p->addSmall(style->layerCombo());
    p = ribbon_->addPanel(home, tr("Proprietà"));
    large(p, {"MATCHPROP"});
    p->addSmall(style->colorCombo());
    p->addSmall(style->linetypeCombo());
    p->addSmall(style->lineweightCombo());
    p = ribbon_->addPanel(home, tr("Utilità"));
    large(p, {"zoom"});
    small(p, {"undo", "redo"});
    small(p, {"DIST", "AREA", "LIST"});

    QWidget* arch = ribbon_->addPage(tr("Architettura"));
    p = ribbon_->addPanel(arch, tr("Costruzione"));
    large(p, {"WALL", "DOOR", "WINDOW", "SLAB", "ROOF", "STAIR"});
    p = ribbon_->addPanel(arch, tr("Modifica"));
    small(p, {"WALLEDIT"});
    p = ribbon_->addPanel(arch, tr("Piani"));
    large(p, {"STORY"});
    p->addSmall(makeStoryCombo(p));
    small(p, {"storyUp", "storyDown"});
    p = ribbon_->addPanel(arch, tr("Modello"));
    large(p, {"view3d", "exportModel"});

    QWidget* annotatePage = ribbon_->addPage(tr("Annota"));
    p = ribbon_->addPanel(annotatePage, tr("Testo"));
    large(p, {"TEXT"});
    small(p, {"TEXTEDIT", "LEADER"});
    p = ribbon_->addPanel(annotatePage, tr("Quote"));
    large(p, {"DIMLINEAR", "DIMALIGNED"});
    small(p, {"DIMRADIUS", "DIMDIAMETER", "DIMSCALE"});
    p = ribbon_->addPanel(annotatePage, tr("Tratteggi"));
    large(p, {"HATCH"});
    small(p, {"HATCHEDIT"});

    QWidget* viewPage = ribbon_->addPage(tr("Vista"));
    p = ribbon_->addPanel(viewPage, tr("Navigazione"));
    large(p, {"zoom", "plan", "view3d"});
    p = ribbon_->addPanel(viewPage, tr("Aiuti al disegno"));
    small(p, {"osnap", "grid", "ortho"});
    p = ribbon_->addPanel(viewPage, tr("Vista 3D"));
    for (QAction* a : layouts_->actions())
        p->addSmall(a);
    p = ribbon_->addPanel(viewPage, tr("Interfaccia"));
    for (QAction* a : profiles_->actions())
        p->addSmall(a);

    QWidget* output = ribbon_->addPage(tr("Output"));
    p = ribbon_->addPanel(output, tr("Stampa"));
    large(p, {"print"});
    p = ribbon_->addPanel(output, tr("Esporta"));
    large(p, {"exportDxf", "exportModel"});
    p = ribbon_->addPanel(output, tr("Script"));
    large(p, {"script"});

    ribbon_->setCurrentIndex(0);
    ribbonBar_ = new QToolBar(tr("Ribbon"), this);
    ribbonBar_->setObjectName(QStringLiteral("ribbonToolBar"));
    ribbonBar_->setMovable(false);
    ribbonBar_->setFloatable(false);
    ribbonBar_->addWidget(ribbon_);
    addToolBar(Qt::TopToolBarArea, ribbonBar_);
    // The ribbon on its own row, above the classic toolbars.
    insertToolBar(classicBars_.front(), ribbonBar_);
    insertToolBarBreak(classicBars_.front());
    named_.value(QStringLiteral("panels"))->menu()->addAction(ribbonBar_->toggleViewAction());
}

void MainWindow::createToolbox()
{
    toolbox_ = new Toolbox(engine_, this);
    toolbox_->addSection(tr("Selezione"));
    toolbox_->addTool(tr("Freccia"), QString(), icons::make(QStringLiteral("select")));
    toolbox_->addSection(tr("Costruzione"));
    toolbox_->addTool(tr("Muro"), QStringLiteral("WALL"), icons::make(QStringLiteral("wall")));
    toolbox_->addTool(tr("Porta"), QStringLiteral("DOOR"), icons::make(QStringLiteral("door")));
    toolbox_->addTool(tr("Finestra"), QStringLiteral("WINDOW"), icons::make(QStringLiteral("window")));
    toolbox_->addTool(tr("Solaio"), QStringLiteral("SLAB"), icons::make(QStringLiteral("slab")));
    toolbox_->addTool(tr("Tetto"), QStringLiteral("ROOF"), icons::make(QStringLiteral("roof")));
    toolbox_->addTool(tr("Scala"), QStringLiteral("STAIR"), icons::make(QStringLiteral("stair")));
    toolbox_->addSection(tr("Documentazione"));
    toolbox_->addTool(tr("Quota"), QStringLiteral("DIMLINEAR"), icons::make(QStringLiteral("dimLinear")));
    toolbox_->addTool(tr("Testo"), QStringLiteral("TEXT"), icons::make(QStringLiteral("text")));
    toolbox_->addTool(tr("Tratteggio"), QStringLiteral("HATCH"), icons::make(QStringLiteral("hatch")));
    toolbox_->addTool(tr("Linea"), QStringLiteral("LINE"), icons::make(QStringLiteral("line")));
    toolbox_->addTool(tr("Polilinea"), QStringLiteral("PLINE"), icons::make(QStringLiteral("polyline")));
    toolbox_->addTool(tr("Arco"), QStringLiteral("ARC"), icons::make(QStringLiteral("arc")));
    toolbox_->addTool(tr("Cerchio"), QStringLiteral("CIRCLE"), icons::make(QStringLiteral("circle")));
    toolbox_->addTool(tr("Rettangolo"), QStringLiteral("RECTANG"), icons::make(QStringLiteral("rectangle")));
    connect(toolbox_, &Toolbox::toolChosen, this, [this](const QString& command) {
        infoBox_->setTool(command);
        commandLine_->input()->setFocus();
    });

    toolboxDock_ = new QDockWidget(tr("Strumenti"), this);
    toolboxDock_->setObjectName(QStringLiteral("toolboxDock"));
    toolboxDock_->setWidget(toolbox_);
    addDockWidget(Qt::LeftDockWidgetArea, toolboxDock_);
    toolboxDock_->hide();
    named_.value(QStringLiteral("panels"))->menu()->addAction(toolboxDock_->toggleViewAction());
}

void MainWindow::createStatusBar()
{
    coordinates_ = new QLabel(this);
    coordinates_->setMinimumWidth(260);
    statusBar()->addWidget(coordinates_);
    for (QToolButton* b : {osnapButton_, orthoButton_, gridButton_, lineweightButton_}) {
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
    lineweightButton_->setText(tr("SPESSORI"));
}

void MainWindow::updateTitle()
{
    QString name = filePath_.isEmpty() ? tr("Senza nome") : QFileInfo(filePath_).fileName();
    if (filePath_.isEmpty() && !dwgSource_.isEmpty())
        name = tr("%1 (DWG in sola lettura)").arg(QFileInfo(dwgSource_).fileName());
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
    dwgSource_.clear();
    clearAutosave();
    view_->zoomExtents();
    engine_.documentChanged();
}

void MainWindow::openDrawing()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Apri disegno"), QString(), kOpenFilter);
    if (!path.isEmpty())
        openFile(path);
}

bool MainWindow::openFile(const QString& path)
{
    engine_.cancel();
    engine_.clearSelection();
    QString message;
    const bool loaded = isDwg(path)   ? io::loadDwg(doc_, path, &message)
                        : isDxf(path) ? io::loadDxf(doc_, path, &message)
                                      : io::loadNative(doc_, path, &message);
    if (!loaded) {
        QMessageBox::warning(this, tr("Apri disegno"), tr("Impossibile aprire %1:\n%2").arg(path, message));
        return false;
    }
    if (!message.isEmpty())
        commandLine_->appendMessage(message);
    // DWG is read only: Save asks where to keep it, as .c4a or .dxf.
    filePath_ = isDwg(path) ? QString() : path;
    dwgSource_ = isDwg(path) ? path : QString();
    clearAutosave();
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
    const bool saved = isDxf(filePath_) ? io::saveDxf(doc_, filePath_, &error) : io::saveNative(doc_, filePath_, &error);
    if (!saved) {
        QMessageBox::warning(this, tr("Salva"), tr("Impossibile salvare %1:\n%2").arg(filePath_, error));
        return false;
    }
    doc_.setModified(false);
    clearAutosave();
    commandLine_->appendMessage(tr("Salvato %1").arg(QFileInfo(filePath_).fileName()));
    updateTitle();
    return true;
}

bool MainWindow::saveAs()
{
    QString filter = isDxf(filePath_) ? kDxfFilter : kNativeFilter;
    const QString suggested = filePath_.isEmpty() && !dwgSource_.isEmpty() ? dwgSource_.left(dwgSource_.size() - 4) : filePath_;
    QString path = QFileDialog::getSaveFileName(this, tr("Salva con nome"), suggested,
                                                kNativeFilter + QStringLiteral(";;") + kDxfFilter, &filter);
    if (path.isEmpty())
        return false;
    const bool dxf = filter == kDxfFilter || isDxf(path);
    const QString suffix = dxf ? QStringLiteral(".dxf") : QStringLiteral(".c4a");
    if (!path.endsWith(suffix, Qt::CaseInsensitive))
        path += suffix;
    filePath_ = path;
    dwgSource_.clear();
    return save();
}

bool MainWindow::exportTo(const QString& path)
{
    QString error;
    bool saved = false;
    if (isPdf(path)) {
        // The whole drawing fitted on an A3 sheet, in black and white.
        PlotSettings settings;
        settings.paper = QPageSize::A3;
        saved = writePdf(path, doc_, settings, doc_.extents(), &error);
    } else if (path.endsWith(QLatin1String(".obj"), Qt::CaseInsensitive)) {
        saved = io::saveObj(buildingModel(doc_), path, &error);
    } else if (path.endsWith(QLatin1String(".stl"), Qt::CaseInsensitive)) {
        saved = io::saveStl(buildingModel(doc_), path, &error);
    } else {
        saved = isDxf(path) ? io::saveDxf(doc_, path, &error) : io::saveNative(doc_, path, &error);
    }
    if (!saved)
        commandLine_->appendMessage(tr("Impossibile salvare %1: %2").arg(path, error));
    return saved;
}

void MainWindow::plot()
{
    engine_.cancel();
    const QString name = filePath_.isEmpty() ? tr("Senza nome") : QFileInfo(filePath_).completeBaseName();
    PlotDialog dialog(doc_, doc_.extents(), view_->visibleArea(), name, this);
    const QString result = dialog.plot();
    if (!result.isEmpty())
        commandLine_->appendMessage(result);
    commandLine_->input()->setFocus();
}

void MainWindow::exportModel()
{
    const QString obj = tr("Wavefront OBJ (*.obj)");
    const QString stl = tr("STL per la stampa 3D (*.stl)");
    QString filter = obj;
    QString path = QFileDialog::getSaveFileName(this, tr("Esporta modello 3D"), QString(), obj + QStringLiteral(";;") + stl, &filter);
    if (path.isEmpty())
        return;
    const QString ext = filter == stl ? QStringLiteral(".stl") : QStringLiteral(".obj");
    if (!path.endsWith(ext, Qt::CaseInsensitive))
        path += ext;
    const Model3D model = buildingModel(doc_);
    if (model.empty()) {
        QMessageBox::information(this, tr("Esporta modello 3D"), tr("Il disegno non ha muri, solai, tetti o scale."));
        return;
    }
    QString error;
    const bool saved = ext == QLatin1String(".stl") ? io::saveStl(model, path, &error) : io::saveObj(model, path, &error);
    if (!saved) {
        QMessageBox::warning(this, tr("Esporta modello 3D"), tr("Impossibile salvare %1:\n%2").arg(path, error));
        return;
    }
    commandLine_->appendMessage(tr("Esportato %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::exportDxf()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Esporta DXF"), QString(), kDxfFilter);
    if (path.isEmpty())
        return;
    if (!isDxf(path))
        path += QLatin1String(".dxf");
    QString error;
    if (!io::saveDxf(doc_, path, &error)) {
        QMessageBox::warning(this, tr("Esporta DXF"), tr("Impossibile salvare %1:\n%2").arg(path, error));
        return;
    }
    commandLine_->appendMessage(tr("Esportato %1").arg(QFileInfo(path).fileName()));
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
    if (maybeSave()) {
        clearAutosave();
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::autosave()
{
    if (!doc_.isModified() || doc_.revision() == autosavedRevision_)
        return;
    QString error;
    if (!recovery_.save(doc_, filePath_, dwgSource_, &error)) {
        commandLine_->appendMessage(tr("Salvataggio automatico non riuscito: %1").arg(error));
        return;
    }
    autosavedRevision_ = doc_.revision();
    commandLine_->appendMessage(tr("Salvataggio automatico eseguito."));
}

void MainWindow::clearAutosave()
{
    recovery_.clear();
    autosavedRevision_ = doc_.revision();
}

void MainWindow::offerRecovery()
{
    const std::vector<io::RecoveryFile> files = io::findRecoveryFiles();
    for (const io::RecoveryFile& rf : files) {
        const QString source = !rf.source.isEmpty() ? QFileInfo(rf.source).fileName()
                               : !rf.dwgSource.isEmpty() ? QFileInfo(rf.dwgSource).fileName()
                                                         : tr("Senza nome");
        QMessageBox box(QMessageBox::Warning, tr("Recupero disegno"),
                        tr("CAD4All non è stato chiuso correttamente.\n\n"
                           "È disponibile una copia di \"%1\" salvata automaticamente il %2.\n"
                           "Recuperare il disegno?")
                            .arg(source, QLocale().toString(rf.saved, QLocale::ShortFormat)),
                        QMessageBox::NoButton, this);
        QPushButton* recover = box.addButton(tr("Recupera"), QMessageBox::AcceptRole);
        QPushButton* discard = box.addButton(tr("Elimina la copia"), QMessageBox::DestructiveRole);
        box.addButton(tr("Più tardi"), QMessageBox::RejectRole);
        box.setDefaultButton(recover);
        box.exec();
        if (box.clickedButton() == discard) {
            io::removeRecoveryFile(rf);
            continue;
        }
        if (box.clickedButton() != recover)
            continue;
        QString message;
        if (!io::loadNative(doc_, rf.path, &message)) {
            QMessageBox::warning(this, tr("Recupero disegno"), tr("Impossibile leggere la copia %1:\n%2").arg(rf.path, message));
            continue;
        }
        engine_.cancel();
        engine_.clearSelection();
        // Save writes it back where it came from; a DWG still asks for a new name.
        filePath_ = rf.source;
        dwgSource_ = rf.dwgSource;
        io::removeRecoveryFile(rf);
        doc_.setModified(true);
        autosavedRevision_ = 0;
        view_->zoomExtents();
        engine_.documentChanged();
        updateTitle();
        commandLine_->appendMessage(tr("Recuperato il disegno salvato automaticamente: salvarlo per non perderlo."));
        // One drawing per window: the other copies are offered at the next start.
        return;
    }
}

// Opens an email to the author with the version and the system filled in.
void MainWindow::reportProblem()
{
    const QString subject = tr("CAD4All %1: segnalazione").arg(QApplication::applicationVersion());
    const QString body = tr("Versione: CAD4All %1 su %2 (%3)\n\n"
                            "Cosa è successo (e cosa si aspettava):\n\n\n"
                            "Come ripeterlo (i comandi usati, in ordine):\n\n\n"
                            "Quanto blocca il lavoro (si chiude / non riesco / scomodo / dettaglio):\n\n\n"
                            "Alleghi il disegno (.c4a, .dxf, .dwg) e uno screenshot, se possibile.\n")
                             .arg(QApplication::applicationVersion(), QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
    const QByteArray mail = "mailto:" CAD4ALL_EMAIL "?subject=" + QUrl::toPercentEncoding(subject)
                            + "&body=" + QUrl::toPercentEncoding(body);
    if (!QDesktopServices::openUrl(QUrl::fromEncoded(mail)))
        QMessageBox::information(this, tr("Segnala un problema"),
                                 tr("Scriva a %1 indicando la versione (%2), che cosa è successo e come ripeterlo.")
                                     .arg(QStringLiteral(CAD4ALL_EMAIL), QApplication::applicationVersion()));
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
        const QStringList tokens = line.split(QLatin1Char(' '));
        for (qsizetype i = 0; i < tokens.size(); ++i) {
            if (engine_.wantsText()) {
                // Free text (TEXT command): spaces belong to it, up to the end of the line.
                engine_.submit(tokens.mid(i).join(QLatin1Char(' ')));
                break;
            }
            engine_.submit(tokens[i]);
        }
    }
    return true;
}

void MainWindow::about()
{
    const QString author = QStringLiteral(CAD4ALL_AUTHOR);
    const QString email = QStringLiteral(CAD4ALL_EMAIL);
    QMessageBox box(this);
    box.setWindowTitle(tr("Informazioni su CAD4All"));
    box.setIconPixmap(QApplication::windowIcon().pixmap(96, 96));
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<h3>CAD4All %1</h3>"
                   "<p>CAD 2D e 3D parametrico per tecnici.</p>"
                   "<p>Copyright © %2 %3<br><a href=\"mailto:%4\">%4</a><br>"
                   "<a href=\"%5\">%5</a></p>"
                   "<p>Software libero distribuito con licenza GNU GPL versione 3 o successiva "
                   "(Aiuto &gt; Licenza). Non c'è alcuna garanzia.</p>")
                    .arg(QApplication::applicationVersion(), QStringLiteral(CAD4ALL_YEARS), author, email,
                         QStringLiteral(CAD4ALL_HOMEPAGE)));
    box.exec();
}

void MainWindow::showLicence()
{
    QFile file(QStringLiteral(":/LICENSE"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Licenza di CAD4All"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(tr("CAD4All è software libero: puoi ridistribuirlo e modificarlo secondo i termini della "
                                "GNU General Public License, versione 3 o successiva. Copyright © %1 %2.")
                                 .arg(QStringLiteral(CAD4ALL_YEARS), QStringLiteral(CAD4ALL_AUTHOR)),
                             &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* text = new QPlainTextEdit(QString::fromUtf8(file.readAll()), &dialog);
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(720, 600);
    dialog.exec();
}
