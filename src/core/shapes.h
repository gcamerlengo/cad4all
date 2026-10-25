// SPDX-License-Identifier: GPL-3.0-or-later
// The drawable 2D shapes and the queries the editor needs on them:
// extents, distance, snap points, intersections and transforms.
#pragma once

#include "geometry.h"

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
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

// Polyline of straight and arc segments. Segment i runs from points[i] to
// points[i + 1], the last one back to points[0] when the polyline is closed.
// bulges[i] is tan(sweep / 4) of segment i: 0 for a straight segment,
// positive when the arc turns counter-clockwise (the DXF convention).
// Missing bulges count as 0.
struct Polyline {
    std::vector<Vec2> points;
    bool closed = false;
    std::vector<double> bulges;

    Polyline() = default;
    Polyline(std::vector<Vec2> pts, bool isClosed = false, std::vector<double> segmentBulges = {})
        : points(std::move(pts)), closed(isClosed), bulges(std::move(segmentBulges))
    {
    }

    double bulge(size_t i) const { return i < bulges.size() ? bulges[i] : 0.0; }
    void setBulge(size_t i, double b);
    size_t segmentCount() const;
    bool hasArcs() const;
};

// Single-line text. `position` is on the baseline, at the left end, the
// centre or the right end depending on `align`. `height` is the height of
// capital letters in drawing units; `content` is UTF-8.
struct Text {
    enum class Align { Left, Center, Right };

    Vec2 position;
    double height = 2.5;
    double rotation = 0.0;  // radians, counter-clockwise
    std::string content;
    Align align = Align::Left;
};

// Dimension. Its lines, arrows and label are derived from the definition
// points by dimensionGeometry(), so moving or scaling it keeps it consistent.
struct Dimension {
    enum class Kind { Linear, Aligned, Radius, Diameter };

    Kind kind = Kind::Linear;
    // Linear and aligned: origins of the two extension lines.
    // Radius and diameter: centre and a point on the circle.
    Vec2 p1, p2;
    // Linear and aligned: a point on the dimension line.
    // Radius and diameter: where the label goes.
    Vec2 location;
    double angle = 0.0;  // linear only: direction of the dimension line
    double textHeight = 2.5;
    double arrowSize = 2.5;
    // Empty: the measured value. Otherwise shown as is, with "<>" replaced
    // by the measured value (as in AutoCAD).
    std::string text;

    double measurement() const;
};

// One family of parallel lines of a hatch pattern, as in AutoCAD .pat files:
// lines at `angle` (radians) through `origin`; each next line is shifted by
// `offset`, measured along the line (x) and across it (y). `dashes` repeat
// along the line: positive = drawn, negative = gap, zero = dot. No dashes
// means a continuous line.
struct HatchPatternLine {
    double angle = 0.0;
    Vec2 origin;
    Vec2 offset;
    std::vector<double> dashes;
};

// Hatch: the area inside `loops` (closed polylines, filled with the even-odd
// rule, so a loop inside another is a hole) covered with a pattern. The
// pattern lines are its definition at scale 1 and angle 0; `scale`, `angle`
// and `origin` place it in the drawing. A solid fill has no pattern lines.
struct Hatch {
    std::vector<Polyline> loops;
    std::string pattern = "ANSI31";
    std::vector<HatchPatternLine> lines;
    double scale = 1.0;
    double angle = 0.0;  // radians
    Vec2 origin;

    bool solid() const { return lines.empty(); }
};

// Door or window in a wall. Sizes are in drawing units; the opening goes
// through the whole thickness of its wall.
struct Opening {
    enum class Kind { Door, Window };

    Kind kind = Kind::Door;
    double position = 0.0;  // centre, as distance from the wall start along its axis
    double width = 900.0;
    double height = 2100.0;
    double sill = 0.0;  // bottom of the opening above the base of the wall
    // Doors: the leaf swings to the right of the wall (walking from start to
    // end) instead of the left, and hangs on the jamb nearer to the end.
    bool swingRight = false;
    bool hingeAtEnd = false;
};

// Straight wall drawn along its reference line from `start` to `end`. The
// justification tells where the reference line runs: on the axis, on the
// left face or on the right face (walking from start to end). Walls whose
// ends meet are joined in plan and in 3D (see building.h).
struct Wall {
    enum class Justification { Center, Left, Right };

    Vec2 start, end;
    double thickness = 300.0;
    double height = 2700.0;
    Justification justification = Justification::Center;
    std::vector<Opening> openings;
};

// Floor slab: the area inside `loops` (even-odd, so a loop inside another is
// a hole) with its top `offset` above the elevation of its story.
struct Slab {
    std::vector<Polyline> loops;
    double thickness = 300.0;
    double offset = 0.0;
};

// Roof over a convex footprint with straight edges, usually the outer faces
// of the walls. Every edge slopes up into the roof at `pitch`, except the
// gable ends (vertical, closed by a wall up to the roof); the slopes start
// `base` above the story along the footprint and the roof sticks out by
// `overhang` all around. No sloping edge makes a flat roof.
struct Roof {
    Polyline footprint;
    std::vector<bool> gables;  // per edge of the footprint; missing = sloping
    double pitch = 0.5235987755982988;  // radians (30 degrees)
    double base = 2700.0;
    double overhang = 500.0;
    double thickness = 250.0;

    bool gable(size_t edge) const { return edge < gables.size() && gables[edge]; }
};

// Stair from its story up by `height`, in `risers` equal steps `going`
// deep. `start` is the middle of the first step's front edge and `angle`
// the direction of the first flight. An L stair turns once, a U stair turns
// back, both on a square landing halfway up; `turnRight` says which way.
struct Stair {
    enum class Kind { Straight, L, U };

    Vec2 start;
    double angle = 0.0;  // radians
    Kind kind = Kind::Straight;
    bool turnRight = false;
    double width = 1000.0;
    double height = 3000.0;
    int risers = 17;
    double going = 280.0;
};

struct Block;

// Reference to a block (AutoCAD's INSERT): the objects of the block, taken
// from its base point to `position`, scaled and rotated. Every reference
// shares the definition; redefining a block gives them the new one (see
// block.h and Document::defineBlock).
struct Insert {
    std::shared_ptr<const Block> block;
    Vec2 position;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotation = 0.0;  // radians, counter-clockwise
};

using Shape = std::variant<Line, Circle, Arc, Polyline, Text, Dimension, Hatch, Wall, Slab, Roof, Stair, Insert>;

// Arc of a polyline segment from `from` to `to`: always counter-clockwise, so
// a negative bulge gives an arc from `to` to `from`.
Arc arcFromBulge(Vec2 from, Vec2 to, double bulge);
// Bulge of an arc segment that sweeps `sweep` radians (negative = clockwise).
inline double bulgeOfSweep(double sweep) { return std::tan(sweep / 4.0); }
// Segment i as a Line or an Arc.
Shape polylineSegment(const Polyline& pl, size_t i);
// Point at t in [0, 1] along segment i, from its first vertex.
Vec2 segmentPoint(const Polyline& pl, size_t i, double t);
// Parameter in [0, 1] of the point of segment i nearest to p.
double segmentParam(const Polyline& pl, size_t i, Vec2 p);
// Direction of travel at t along segment i (unit vector).
Vec2 segmentTangent(const Polyline& pl, size_t i, double t);

enum SnapType : unsigned {
    SnapEndpoint = 1u << 0,
    SnapMidpoint = 1u << 1,
    SnapCenter = 1u << 2,
    SnapQuadrant = 1u << 3,
    SnapIntersection = 1u << 4,
    SnapNearest = 1u << 5,
    SnapInsertion = 1u << 6,
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

// Approximate extent of a text: there is no font in the core, so the width
// is estimated from the number of characters. The view draws the real glyphs.
double estimatedTextWidth(const Text& text);
// Corners of the text box, counter-clockwise from the bottom left.
std::array<Vec2, 4> textCorners(const Text& text);

// What a dimension draws: lines, filled arrowheads (tip first) and its label.
struct DimensionGeometry {
    std::vector<Line> lines;
    std::vector<std::array<Vec2, 3>> arrows;
    Text label;
};
DimensionGeometry dimensionGeometry(const Dimension& dim);
// Measured value as shown in a label: up to two decimals, decimal comma.
std::string formatMeasurement(double value);

// Circle arc through three points, oriented to pass through `mid`.
std::optional<Arc> arcFromThreePoints(Vec2 start, Vec2 mid, Vec2 end);

} // namespace cad
