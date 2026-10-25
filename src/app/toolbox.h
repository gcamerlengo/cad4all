// SPDX-License-Identifier: GPL-3.0-or-later
// Toolbox of the parametric interface: one button per tool, grouped in sections; the
// button of the running command stays pressed, the arrow when none runs.
#pragma once

#include <QWidget>

#include <vector>

namespace cad {
class CommandEngine;
}

class QToolButton;
class QVBoxLayout;

class Toolbox : public QWidget {
    Q_OBJECT

public:
    explicit Toolbox(cad::CommandEngine& engine, QWidget* parent = nullptr);

    void addSection(const QString& title);
    // An empty command is the arrow: it ends the running command.
    void addTool(const QString& text, const QString& command, const QIcon& icon);
    // Presses the button of the running command.
    void refresh();

signals:
    void toolChosen(const QString& command);

private:
    struct Tool {
        QString command;
        QToolButton* button;
    };

    cad::CommandEngine& engine_;
    QVBoxLayout* layout_;
    std::vector<Tool> tools_;
};
