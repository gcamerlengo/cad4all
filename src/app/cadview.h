// SPDX-License-Identifier: GPL-3.0-or-later
// The drawing area: shows the document, the previews of the active command,
// object snap markers and the crosshair; turns mouse input into points,
// selections, zoom and pan.
#pragma once

#include "commandengine.h"

#include <QWidget>

#include <optional>

class CadView : public QWidget {
    Q_OBJECT

public:
    explicit CadView(cad::CommandEngine& engine, QWidget* parent = nullptr);

    void zoomExtents();
    bool gridVisible() const { return grid_; }
    void setGridVisible(bool on);
    // AutoCAD's LWDISPLAY: lines as thick as their lineweight, or all thin.
    bool lineweightsVisible() const { return lineweights_; }
    void setLineweightsVisible(bool on);

    cad::Vec2 toWorld(QPointF screen) const;
    // The part of the drawing shown.
    cad::BBox visibleArea() const;
    QPointF toScreen(cad::Vec2 world) const;

signals:
    void cursorMoved(cad::Vec2 world);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateCursor(QPointF pos);
    void finishWindowSelection(QPointF end, bool remove);
    double pixels(double px) const { return px / scale_; }

    void drawGrid(QPainter& p) const;
    void drawGrips(QPainter& p) const;
    void drawSnapMarker(QPainter& p, const cad::SnapPoint& snap) const;
    void drawCrosshair(QPainter& p) const;

    cad::CommandEngine& engine_;

    // View: world point at the widget centre and pixels per drawing unit.
    cad::Vec2 center_{0.0, 0.0};
    double scale_ = 20.0;
    bool grid_ = true;
    bool lineweights_ = false;

    QPointF mouse_;
    bool mouseInside_ = false;
    bool panning_ = false;
    QPointF panLast_;
    std::optional<QPointF> windowStart_;
    bool windowDragged_ = false;
    // Where a grip was pressed, while the button is held.
    std::optional<QPointF> gripPress_;

    // Point the next click will give to the command (after snap and ortho).
    cad::Vec2 effective_;
    std::optional<cad::SnapPoint> snap_;
};
