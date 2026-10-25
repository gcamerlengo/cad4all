// SPDX-License-Identifier: GPL-3.0-or-later
// Layer manager: create layers, set the current one, change colour,
// linetype, lineweight, visibility and lock.
#pragma once

#include "commandengine.h"

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

class LayerPanel : public QWidget {
    Q_OBJECT

public:
    explicit LayerPanel(cad::CommandEngine& engine, QWidget* parent = nullptr);

    void refresh();

private:
    enum Column { NameColumn, VisibleColumn, LockedColumn, ColorColumn, LinetypeColumn, LineweightColumn };

    void createLayer();
    void makeCurrent(QTreeWidgetItem* item);
    void itemChanged(QTreeWidgetItem* item, int column);
    void itemDoubleClicked(QTreeWidgetItem* item, int column);

    cad::CommandEngine& engine_;
    QTreeWidget* tree_;
    std::uint64_t shownRevision_ = 0;
};
