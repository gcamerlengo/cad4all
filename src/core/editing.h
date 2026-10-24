// SPDX-License-Identifier: GPL-3.0-or-later
// Geometry behind the editing commands OFFSET, TRIM, EXTEND, FILLET,
// CHAMFER and STRETCH.
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

// RadiusTooLarge also means a chamfer distance longer than its line.
enum class FilletError { None, Parallel, RadiusTooLarge };

// Joins two lines with an arc of `radius` (0 = trim or extend them to meet).
// The picks say which part of each line to keep.
std::optional<FilletResult> filletLines(const Line& first, Vec2 pickFirst, const Line& second, Vec2 pickSecond,
                                        double radius, FilletError* error = nullptr);

struct CurveFilletResult {
    Shape first;
    Shape second;
    std::optional<Arc> arc;
};

// Fillet between lines, arcs and circles. Lines and arcs are trimmed or
// extended to the tangent points; circles stay whole, as in AutoCAD. Of the
// possible fillets, the one whose tangent points are nearest to the picks wins.
std::optional<CurveFilletResult> filletCurves(const Shape& first, Vec2 pickFirst, const Shape& second, Vec2 pickSecond,
                                              double radius, FilletError* error = nullptr);

struct ChamferResult {
    Line first;
    Line second;
    std::optional<Line> bevel;  // none when both distances are zero
};

// Cuts the corner between two lines at `dist1` from the corner along the
// first line and `dist2` along the second.
std::optional<ChamferResult> chamferLines(const Line& first, Vec2 pickFirst, const Line& second, Vec2 pickSecond,
                                          double dist1, double dist2, FilletError* error = nullptr);

// Second chamfer distance for a chamfer of length `dist1` on the first line
// at `angle` (radians) from it, given the two lines.
std::optional<double> chamferDistanceFromAngle(const Line& first, Vec2 pickFirst, const Line& second, Vec2 pickSecond,
                                               double dist1, double angle);

// STRETCH: moves by `offset` the defining points of `shape` that lie inside
// `window` (line ends, polyline vertices, text insertion, dimension points).
// Arcs with one end inside keep their other end and midpoint; circles move
// when their centre is inside.
Shape stretchShape(const Shape& shape, const BBox& window, Vec2 offset);

} // namespace cad
