// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "commandengine.h"
#include "document.h"

#include <QMainWindow>

class CadView;
class CommandLine;
class LayerPanel;
class QComboBox;
class QDockWidget;
class QLabel;
class QToolButton;
class View3D;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

    bool openFile(const QString& path);
    // Runs an AutoCAD-style script: every space or new line is an Enter.
    bool runScript(const QString& path);
    // Writes the drawing to `path` (.c4a, .dxf or .pdf) without changing the current file.
    bool exportTo(const QString& path);
    CadView* view() const { return view_; }
    View3D* view3d() const { return view3d_; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createActions();
    void createStatusBar();
    QAction* commandAction(const QString& text, const QString& command, const QIcon& icon = {});

    void newDrawing();
    void openDrawing();
    bool save();
    bool saveAs();
    void exportDxf();
    void plot();
    void showView3d();
    void updateStories();
    void setStory(cad::StoryId id);
    void changeStory(int step);
    bool maybeSave();
    void runScriptDialog();
    void updateTitle();
    void updateToggles();
    void about();

    cad::Document doc_;
    cad::CommandEngine engine_;
    CadView* view_;
    CommandLine* commandLine_;
    LayerPanel* layerPanel_;
    QDockWidget* layerDock_;
    View3D* view3d_;
    QDockWidget* view3dDock_;
    QComboBox* storyCombo_ = nullptr;
    // The 3D view opens by itself with the first wall, once.
    bool view3dShown_ = false;
    QLabel* coordinates_;
    QToolButton* osnapButton_;
    QToolButton* orthoButton_;
    QToolButton* gridButton_;
    QString filePath_;
    // The DWG the drawing was read from, which is never written.
    QString dwgSource_;
};
