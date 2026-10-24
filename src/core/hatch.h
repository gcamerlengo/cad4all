// SPDX-License-Identifier: GPL-3.0-or-later
// Hatches: the built-in patterns, the pattern lines inside a boundary and
// the search for the closed boundary around a point (HATCH "internal point").
#pragma once

#include "shapes.h"

#include <array>
#include <string>
#include <vector>

namespace cad {

// Definition of a built-in pattern (metric sizes, as in AutoCAD's
// acadiso.pat). SOLID has no lines. Nullptr when the name is unknown.
const std::vector<HatchPatternLine>* hatchPattern(const std::string& name);
std::vector<std::string> hatchPatternNames();

// A hatch of the built-in pattern `name` (upper case) inside `loops`.
Hatch makeHatch(std::vector<Polyline> loops, const std::string& name, double scale, double angle);

// The loops as polygons, arcs replaced by chords at most `maxStep` radians apart.
std::vector<std::vector<Vec2>> flattenLoops(const std::vector<Polyline>& loops, double maxStep = kPi / 90.0);

// Even-odd test against the loops.
bool insideHatch(const Hatch& hatch, Vec2 p);

// Smallest distance between two parallel pattern lines, in drawing units
// (0 for a solid fill). Tells whether the pattern is readable at a zoom.
double hatchSpacing(const Hatch& hatch);

// Pattern lines (dashes and dots included; a dot is a line of zero length)
// clipped to the hatch area and to `clip`. When more than `maxSegments`
// would be needed the result is empty and `tooDense` is set.
std::vector<Line> hatchLines(const Hatch& hatch, const BBox& clip, size_t maxSegments = 200000, bool* tooDense = nullptr);

// The area of the hatch as trapezoids (corners in order), for a solid fill
// in formats that only know filled quadrilaterals (DXF R12 SOLID).
std::vector<std::array<Vec2, 4>> hatchTrapezoids(const Hatch& hatch);

// The closed boundary around `point` formed by `shapes` (lines, arcs,
// circles, polylines; the rest is ignored), as AutoCAD finds it: the
// smallest closed region containing the point, plus the outer boundary of
// every separate group of objects inside it (islands; what lies inside an
// island is not hatched). Empty when the point is not enclosed.
std::vector<Polyline> findHatchBoundary(const std::vector<Shape>& shapes, Vec2 point);

} // namespace cad
