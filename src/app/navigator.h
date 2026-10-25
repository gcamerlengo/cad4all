// SPDX-License-Identifier: GPL-3.0-or-later
// ArchiCAD-style Navigator: the stories of the building, from the top, and
// the views of the model. A double click opens one.
#pragma once

#include "document.h"

#include <QTreeWidget>

class Navigator : public QTreeWidget {
    Q_OBJECT

public:
    explicit Navigator(const cad::Document& doc, QWidget* parent = nullptr);

    void refresh();

signals:
    void storyChosen(cad::StoryId id);
    void planChosen();
    void view3dChosen();

private:
    const cad::Document& doc_;
    QTreeWidgetItem* stories_;
    QTreeWidgetItem* plan_;
    QTreeWidgetItem* view3d_;
};
