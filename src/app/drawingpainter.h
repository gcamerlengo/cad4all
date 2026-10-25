// SPDX-License-Identifier: GPL-3.0-or-later
// Draws shapes with a QPainter through a view mapping (world point at the
// device centre, device units per drawing unit). Shared by the drawing area
// and by printing, so paper and screen show the same thing.
#pragma once

#include "building.h"
#include "shapes.h"
#include "style.h"

#include <QFont>
#include <QPainter>

// Extent of what a shape draws: a joined wall can reach past its own box by
// the length of its mitred corners.
cad::BBox drawnBox(const cad::Shape& shape);

// Linetypes apply to lines, circles, arcs and polylines; texts, dimensions,
// hatches and building elements keep their own look.
bool usesLinetype(const cad::Shape& shape);
// Gives `pen` the dashes of `type`, whose lengths are multiplied by
// `lengthScale` (LTSCALE) and drawn at `deviceScale` device units per drawing
// unit. Dashes too small to see are drawn as a continuous line.
void applyLinetype(QPen& pen, const cad::Linetype& type, double lengthScale, double deviceScale);

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
    // A wall with its joined faces (drawShape draws it on its own).
    void drawWall(const cad::Wall& wall, const cad::WallFaces& faces);

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
