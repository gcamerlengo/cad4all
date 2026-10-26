// SPDX-License-Identifier: GPL-3.0-or-later
// Properties palette (AutoCAD's PROPERTIES, Ctrl+1): the layer, look and
// geometry of the selected objects, editable in place. The fields and the
// changes come from objectproperties.h; each change is one undo step.
#pragma once

#include "document.h"

#include <QWidget>

#include <string>
#include <vector>

namespace cad {
class CommandEngine;
}

class QFormLayout;
class QLabel;
class QScrollArea;

class PropertiesPalette : public QWidget {
    Q_OBJECT

public:
    explicit PropertiesPalette(cad::CommandEngine& engine, QWidget* parent = nullptr);

    void refresh();

private:
    void apply(const std::string& key, const QString& value);

    cad::CommandEngine& engine_;
    std::vector<cad::EntityId> ids_;
    QString signature_;  // what the fields show: rebuilt when it changes
    QLabel* title_;
    QScrollArea* scroll_;
    QWidget* fields_ = nullptr;
};
