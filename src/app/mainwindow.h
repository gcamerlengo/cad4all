// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "commandengine.h"
#include "document.h"

#include <QMainWindow>

class CadView;
class CommandLine;
class LayerPanel;
class QDockWidget;
class QLabel;
class QToolButton;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

    bool openFile(const QString& path);
    // Runs an AutoCAD-style script: every space or new line is an Enter.
    bool runScript(const QString& path);
    // Writes the drawing to `path` (.c4a or .dxf) without changing the current file.
    bool exportTo(const QString& path);
    CadView* view() const { return view_; }

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
    QLabel* coordinates_;
    QToolButton* osnapButton_;
    QToolButton* orthoButton_;
    QToolButton* gridButton_;
    QString filePath_;
};
