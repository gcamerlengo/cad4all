// SPDX-License-Identifier: GPL-3.0-or-later
// ArchiCAD-style Info Box: a strip with the parameters of the current
// architectural tool, or of the selected wall, slab, roof or stair, which
// can be changed right there. Changes to an object are one undo step.
#pragma once

#include "document.h"

#include <QWidget>

#include <optional>
#include <string>

namespace cad {
class CommandEngine;
}

class QHBoxLayout;
class QLabel;

class InfoBox : public QWidget {
    Q_OBJECT

public:
    explicit InfoBox(cad::CommandEngine& engine, QWidget* parent = nullptr);

    // The tool shown when no object is selected and no tool runs.
    void setTool(const QString& command);
    void refresh();

private:
    void rebuild();
    void apply(const std::string& key, double value);

    cad::CommandEngine& engine_;
    QString tool_ = QStringLiteral("WALL");
    std::optional<cad::EntityId> object_;
    QString signature_;  // what the fields show: rebuilt when it changes
    QLabel* icon_;
    QLabel* title_;
    QHBoxLayout* layout_;
    QWidget* fields_ = nullptr;
};
