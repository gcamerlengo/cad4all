// SPDX-License-Identifier: GPL-3.0-or-later
// The drawable 2D shapes and the queries the editor needs on them:
// extents, distance, snap points, intersections and transforms.
#pragma once

#include "geometry.h"

#include <optional>
#include <variant>
#include <vector>

namespace cad {

struct Line {
    Vec2 a, b;
};

struct Circle {
    Vec2 center;
    double radius = 0.0;
};

// Arcs run counter-clockwise from startAngle to endAngle (radians).
struct Arc {
    Vec2 center;
    double radius = 0.0;
    double startAngle = 0.0;
    double endAngle = 0.0;

    Vec2 startPoint() const { return center + polar(radius, startAngle); }
    Vec2 endPoint() const { return center + polar(radius, endAngle); }
    double sweep() const { return sweepAngle(startAngle, endAngle); }
};

// Straight-segment polyline. Arc segments (bulges) come with the DXF work.
struct Polyline {
    std::vector<Vec2> points;
    bool closed = false;
};

using Shape = std::variant<Line, Circle, Arc, Polyline>;

enum SnapType : unsigned {
    SnapEndpoint = 1u << 0,
    SnapMidpoint = 1u << 1,
    SnapCenter = 1u << 2,
    SnapQuadrant = 1u << 3,
    SnapIntersection = 1u << 4,
    SnapNearest = 1u << 5,
};

struct SnapPoint {
    Vec2 point;
    SnapType type = SnapEndpoint;
};

BBox boundingBox(const Shape& shape);
double distanceTo(const Shape& shape, Vec2 p);
Vec2 nearestPoint(const Shape& shape, Vec2 p);
Shape transformed(const Shape& shape, const Transform& t);

// Appends the snap points of `shape` whose type is in `mask`.
// Intersections involve two shapes and are handled by intersections().
void collectSnapPoints(const Shape& shape, unsigned mask, std::vector<SnapPoint>& out);
std::vector<Vec2> intersections(const Shape& a, const Shape& b);

// True when the shape touches or crosses the rectangle (crossing selection).
bool crossesBox(const Shape& shape, const BBox& box);

// Circle arc through three points, oriented to pass through `mid`.
std::optional<Arc> arcFromThreePoints(Vec2 start, Vec2 mid, Vec2 end);

} // namespace cad
