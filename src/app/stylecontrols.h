// SPDX-License-Identifier: GPL-3.0-or-later
// AutoCAD's Properties panel of the ribbon: layer, colour, linetype and
// lineweight. With objects selected they show and change theirs, otherwise
// those that new objects get.
#pragma once

#include "commandengine.h"

#include <QObject>

#include <functional>
#include <set>

class QComboBox;
class QWidget;

class StyleControls : public QObject {
    Q_OBJECT

public:
    StyleControls(cad::CommandEngine& engine, QWidget* parent);

    QComboBox* layerCombo() const { return layer_; }
    QComboBox* colorCombo() const { return color_; }
    QComboBox* linetypeCombo() const { return linetype_; }
    QComboBox* lineweightCombo() const { return lineweight_; }

    // Shows the values of the selection or the current ones; does nothing if
    // neither changed, unless `force`.
    void refresh(bool force = false);

private:
    void layerChosen(int index);
    void colorChosen(int index);
    void linetypeChosen(int index);
    void lineweightChosen(int index);
    // Changes the selected objects, or the current style when nothing is selected.
    void change(const std::function<void(cad::Entity&)>& entity, const std::function<void(cad::Document&)>& current);

    cad::CommandEngine& engine_;
    QWidget* parent_;
    QComboBox* layer_;
    QComboBox* color_;
    QComboBox* linetype_;
    QComboBox* lineweight_;
    // What the combo boxes show, to rebuild them only when it changes.
    std::uint64_t shownRevision_ = 0;
    std::set<cad::EntityId> shownSelection_;
    bool shown_ = false;
};
