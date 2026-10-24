// SPDX-License-Identifier: GPL-3.0-or-later
// Geometry behind the editing commands OFFSET, TRIM, EXTEND and FILLET.
// Everything here is pure computation: the commands decide which entities
// to change and record the changes in the document.
#pragma once

#include "shapes.h"

#include <optional>
#include <vector>

namespace cad {

// Copy of `shape` at distance `dist`, on the side where `sidePoint` lies.
// Returns nothing when the result would collapse (e.g. a circle offset
// inwards by more than its radius).
std::optional<Shape> offsetShape(const Shape& shape, double dist, Vec2 sidePoint);

struct TrimResult {
    // False when the shape cannot be trimmed at that point (nothing changes).
    bool changed = false;
    // What remains of the shape: empty means the whole shape goes away.
    std::vector<Shape> pieces;
};

// Removes the part of `shape` around `pickPoint`, bounded by the nearest
// intersections with `boundaries`. With no intersection the whole shape is
// removed, as AutoCAD's quick trim does.
TrimResult trimShape(const Shape& shape, const std::vector<Shape>& boundaries, Vec2 pickPoint);

// Lengthens the end of `shape` nearest to `pickPoint` up to the first
// boundary it meets. Lines, arcs and open polylines can be extended.
std::optional<Shape> extendShape(const Shape& shape, const std::vector<Shape>& boundaries, Vec2 pickPoint);

struct FilletResult {
    Line first;
    Line second;
    std::optional<Arc> arc;  // none when the radius is zero (sharp corner)
};

enum class FilletError { None, Parallel, RadiusTooLarge };

// Joins two lines with an arc of `radius` (0 = trim or extend them to meet).
// The picks say which part of each line to keep.
std::optional<FilletResult> filletLines(const Line& first, Vec2 pickFirst, const Line& second, Vec2 pickSecond,
                                        double radius, FilletError* error = nullptr);

} // namespace cad
