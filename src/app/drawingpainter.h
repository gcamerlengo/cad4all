// SPDX-License-Identifier: GPL-3.0-or-later
// Draws shapes with a QPainter through a view mapping (world point at the
// device centre, device units per drawing unit). Shared by the drawing area
// and by printing, so paper and screen show the same thing.
#pragma once

#include "shapes.h"

#include <QFont>
#include <QPainter>

class DrawingPainter {
public:
    DrawingPainter(QPainter& painter, cad::Vec2 center, double scale, QPointF deviceCenter);

    QPointF toDevice(cad::Vec2 w) const
    {
        return {deviceCenter_.x() + (w.x - center_.x) * scale_, deviceCenter_.y() - (w.y - center_.y) * scale_};
    }
    // The part of the drawing that is drawn; shapes outside it are skipped.
    void setVisibleArea(const cad::BBox& box) { visible_ = box; }

    // Uses the painter's current pen; fills (arrows, hatches) take its colour.
    void drawShape(const cad::Shape& shape);
    void drawText(const cad::Text& text);
    void drawHatch(const cad::Hatch& hatch);

private:
    QPainterPath polylinePath(const cad::Polyline& pl) const;

    QPainter& p_;
    cad::Vec2 center_;
    double scale_;
    QPointF deviceCenter_;
    cad::BBox visible_;
    // Drawn at a fixed pixel size and scaled, so text of any height is exact.
    QFont textFont_;
};
