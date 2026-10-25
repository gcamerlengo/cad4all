// SPDX-License-Identifier: GPL-3.0-or-later
// Sheets (layouts): the list of the drawing's sheets with a preview of the
// selected one, and buttons to create, edit, delete, export and print them.
#pragma once

#include "commandengine.h"

#include <QWidget>

#include <functional>

class QListWidget;
class QPushButton;

class LayoutPreview : public QWidget {
    Q_OBJECT

public:
    explicit LayoutPreview(const cad::Document& doc, QWidget* parent = nullptr);

    void setLayout(const cad::Layout* layout);
    QSize sizeHint() const override { return {320, 230}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const cad::Document& doc_;
    std::string name_;
};

class LayoutPanel : public QWidget {
    Q_OBJECT

public:
    // `visibleArea` gives the part of the drawing shown in the plan, for
    // sheets that show the current view.
    LayoutPanel(cad::CommandEngine& engine, std::function<cad::BBox()> visibleArea, QWidget* parent = nullptr);

    void refresh();
    void select(const QString& name);
    QString currentName() const;

    void createLayout();
    void editLayout();
    void removeLayout();
    void exportPdf();
    void print();

signals:
    void message(const QString& text);

private:
    // Asks for the sheet's settings; false when cancelled.
    bool editDialog(cad::Layout& layout, bool isNew);
    void updateButtons();

    cad::CommandEngine& engine_;
    std::function<cad::BBox()> visibleArea_;
    QListWidget* list_;
    LayoutPreview* preview_;
    QPushButton* edit_;
    QPushButton* remove_;
    QPushButton* export_;
    QPushButton* print_;
    std::uint64_t shownRevision_ = 0;
};
