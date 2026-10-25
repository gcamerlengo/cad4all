// SPDX-License-Identifier: GPL-3.0-or-later
// Sections and elevations of the 3D model, as 2D drawings: the model is cut
// by a vertical plane and seen from one side of it. What the plane cuts is
// outlined (and filled), what lies beyond it is drawn with its hidden lines
// removed.
#pragma once

#include "building.h"

#include <optional>

namespace cad {

// The view: a vertical plane through `origin` looking along `direction`
// (horizontal). In the drawing x runs to the right as seen by the viewer,
// measured from `origin`, and y is the height.
struct SectionPlane {
    Vec2 origin;
    Vec2 direction{0.0, 1.0};
    // Only what lies between these x, when set (a section line's ends).
    std::optional<double> from, to;
};

struct SectionDrawing {
    // Outlines of what the plane cuts; closed ones are the solid parts.
    std::vector<Polyline> cut;
    // Edges beyond the plane, visible parts only.
    std::vector<Line> beyond;
    // The ground line at height 0, under the whole drawing.
    Line ground;

    bool empty() const { return cut.empty() && beyond.empty(); }
};

SectionDrawing sectionDrawing(const Model3D& model, const SectionPlane& plane);

// The x of a point of the plan in the drawing of `plane`.
double sectionX(const SectionPlane& plane, Vec2 p);

} // namespace cad
