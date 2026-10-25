// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "commandengine.h"
#include "document.h"
#include "recovery.h"

#include <QHash>
#include <QMainWindow>

#include <vector>

class CadView;
class CommandLine;
class InfoBox;
class LayerPanel;
class Navigator;
class QActionGroup;
class QComboBox;
class QDockWidget;
class QLabel;
class QSplitter;
class QTimer;
class QTabWidget;
class QToolBar;
class QToolButton;
class Ribbon;
class Toolbox;
class View3D;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // The arrangement of the window: AutoCAD's ribbon, or ArchiCAD's
    // Toolbox, Info Box and Navigator.
    enum class Profile { AutoCad, ArchiCad };
    // Where the 3D view is: in a panel at the side, next to the plan, or
    // in a tab of its own.
    enum class ViewLayout { Docked, SideBySide, Tabs };

    MainWindow();

    // Arranges the window; `remember` keeps the choice for the next start.
    void setProfile(Profile profile, bool remember = false);
    void setViewLayout(ViewLayout layout, bool remember = false);
    // By name, for the command line: "autocad", "archicad"; "pannello", "affiancate", "schede".
    bool setProfile(const QString& name);
    bool setViewLayout(const QString& name);

    bool openFile(const QString& path);
    // Runs an AutoCAD-style script: every space or new line is an Enter.
    bool runScript(const QString& path);
    // Writes the drawing to `path` (.c4a, .dxf or .pdf) without changing the current file.
    bool exportTo(const QString& path);
    // Offers the drawings saved automatically by a session that crashed.
    void offerRecovery();
    CadView* view() const { return view_; }
    View3D* view3d() const { return view3d_; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createActions();
    void createRibbon();
    void createToolbox();
    void createStatusBar();
    QAction* commandAction(const QString& text, const QString& command, const QIcon& icon = {});
    // The action of a command made with commandAction, by command name.
    QAction* command(const QString& name) const { return commands_.value(name); }
    QComboBox* makeStoryCombo(QWidget* parent);
    void showPlan();

    void newDrawing();
    void openDrawing();
    bool save();
    bool saveAs();
    void exportDxf();
    void exportModel();
    void plot();
    void showView3d();
    void updateStories();
    void setStory(cad::StoryId id);
    void changeStory(int step);
    bool maybeSave();
    void runScriptDialog();
    void updateTitle();
    void updateToggles();
    void autosave();
    // The automatic copy is no longer needed: the drawing was saved, opened or closed.
    void clearAutosave();
    void reportProblem();
    void about();
    void showLicence();

    cad::Document doc_;
    cad::CommandEngine engine_;
    CadView* view_;
    CommandLine* commandLine_;
    LayerPanel* layerPanel_;
    QDockWidget* layerDock_;
    View3D* view3d_;
    QDockWidget* view3dDock_;
    QSplitter* central_;
    QTabWidget* viewTabs_;
    Ribbon* ribbon_ = nullptr;
    QToolBar* ribbonBar_ = nullptr;
    QToolBar* standardBar_ = nullptr;
    std::vector<QToolBar*> classicBars_;
    Toolbox* toolbox_ = nullptr;
    QDockWidget* toolboxDock_ = nullptr;
    InfoBox* infoBox_;
    QDockWidget* infoDock_;
    Navigator* navigator_;
    QDockWidget* navigatorDock_;
    QHash<QString, QAction*> commands_;
    QHash<QString, QAction*> named_;  // other actions shared by menus, toolbars and ribbon
    QActionGroup* profiles_ = nullptr;
    QActionGroup* layouts_ = nullptr;
    Profile profile_ = Profile::AutoCad;
    ViewLayout viewLayout_ = ViewLayout::Docked;
    std::vector<QComboBox*> storyCombos_;
    // The 3D view opens by itself with the first wall, once.
    bool view3dShown_ = false;
    QLabel* coordinates_;
    QToolButton* osnapButton_;
    QToolButton* orthoButton_;
    QToolButton* gridButton_;
    QToolButton* lineweightButton_;
    // Automatic saves, every few minutes while the drawing has changes.
    cad::io::RecoverySession recovery_;
    QTimer* autosaveTimer_;
    std::uint64_t autosavedRevision_ = 0;
    QString filePath_;
    // The DWG the drawing was read from, which is never written.
    QString dwgSource_;
};
