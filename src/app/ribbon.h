// SPDX-License-Identifier: GPL-3.0-or-later
// AutoCAD-style ribbon: tabs of titled panels, each with large buttons
// (icon above the text) and columns of small ones.
#pragma once

#include <QTabWidget>

class QAction;
class QGridLayout;
class QHBoxLayout;

class RibbonPanel : public QWidget {
    Q_OBJECT

public:
    RibbonPanel(const QString& title, QWidget* parent = nullptr);

    void addLarge(QAction* action);
    // Small buttons fill columns of three, top to bottom.
    void addSmall(QAction* action);
    void addSmall(QWidget* widget);

private:
    void nextColumn();

    QHBoxLayout* row_;
    QGridLayout* column_ = nullptr;
    int columnItems_ = 0;
};

class Ribbon : public QTabWidget {
    Q_OBJECT

public:
    explicit Ribbon(QWidget* parent = nullptr);

    // A new tab; panels are added left to right with addPanel.
    QWidget* addPage(const QString& title);
    RibbonPanel* addPanel(QWidget* page, const QString& title);
};
