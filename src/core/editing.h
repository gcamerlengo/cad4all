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

struct PolylineCornersResult {
    Polyline polyline;
    int changed = 0;  // corners rounded or bevelled
    int skipped = 0;  // corners whose segments were too short
};

// FILLET Polyline: rounds every corner between two straight segments.
PolylineCornersResult filletPolyline(const Polyline& pl, double radius);
// CHAMFER Polyline: bevels every corner between two straight segments,
// `dist1` along the incoming segment and `dist2` along the outgoing one. With
// `angle` (radians from the incoming segment) dist2 follows from dist1.
PolylineCornersResult chamferPolyline(const Polyline& pl, double dist1, double dist2,
                                      std::optional<double> angle = std::nullopt);

// STRETCH: moves by `offset` the defining points of `shape` that lie inside
// `window` (line ends, polyline vertices, text insertion, dimension points).
// Arcs with one end inside keep their other end and midpoint; circles move
// when their centre is inside.
Shape stretchShape(const Shape& shape, const BBox& window, Vec2 offset);

// Grips, AutoCAD's squares on a selected object: line ends and midpoint,
// circle centre and quadrants, arc centre, ends and midpoint, polyline
// vertices and segment midpoints, text insertion, dimension points, wall ends
// and midpoint, block insertion point. Other shapes have none.
std::vector<Vec2> gripPoints(const Shape& shape);
// The shape with grip `index` dragged to `to`: an end or vertex moves alone,
// a midpoint or centre moves the whole line, segment or circle, a quadrant
// changes the radius, an arc point keeps the other two. Nothing when the
// result would be degenerate.
std::optional<Shape> moveGrip(const Shape& shape, size_t index, Vec2 to);

// LIST, DIST and AREA: length of lines, arcs, polylines and the
// circumference of circles; area of circles and closed polylines.
double shapeLength(const Shape& shape);
std::optional<double> shapeArea(const Shape& shape);
// Area enclosed by a closed polyline, arcs included.
double polylineArea(const Polyline& pl);

// EXPLODE: a polyline into its lines and arcs, a dimension into lines, text
// and solid arrowheads, a paragraph text into its lines, a block reference into its objects (one level: nested
// references stay). Empty for shapes that cannot be exploded.
std::vector<Shape> explodeShape(const Shape& shape);

// JOIN: lines, arcs and open polylines that meet end to end (within
// `tolerance`) become one polyline; lines in a row become one line. Each
// result says which of `shapes` it is made of; shapes that join nothing are
// left out.
struct JoinedShape {
    Shape shape;
    std::vector<size_t> sources;
};
std::vector<JoinedShape> joinShapes(const std::vector<Shape>& shapes, double tolerance);

// ELLIPSE drawn as a closed polyline of arcs, as AutoCAD does with
// PELLIPSE=1: `major` goes from the centre to the end of the major axis,
// `ratio` is minor / major.
Polyline ellipsePolyline(Vec2 center, Vec2 major, double ratio);
// SPLINE through `fit` points, as a polyline of short segments (centripetal
// Catmull-Rom).
Polyline splinePolyline(const std::vector<Vec2>& fit, bool closed);

} // namespace cad
