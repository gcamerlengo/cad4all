// SPDX-License-Identifier: GPL-3.0-or-later
// Basic 2D math used by every part of CAD4All: vectors, bounding boxes,
// affine transforms and the low level intersection routines.
#pragma once

#include <cmath>
#include <limits>
#include <vector>

namespace cad {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
// Geometric tolerance in drawing units. Drawings are in real units (usually
// metres or millimetres), so 1e-9 is far below anything a user can draw.
constexpr double kEps = 1e-9;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    bool operator==(const Vec2&) const = default;
};

inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline double length(Vec2 v) { return std::hypot(v.x, v.y); }
inline double distance(Vec2 a, Vec2 b) { return length(b - a); }
inline Vec2 perpendicular(Vec2 v) { return {-v.y, v.x}; }
inline Vec2 normalized(Vec2 v)
{
    const double len = length(v);
    return len < kEps ? Vec2{} : v / len;
}
inline Vec2 midpoint(Vec2 a, Vec2 b) { return (a + b) * 0.5; }
inline double angleOf(Vec2 v) { return std::atan2(v.y, v.x); }
inline Vec2 polar(double len, double angle) { return {len * std::cos(angle), len * std::sin(angle)}; }
inline bool almostEqual(Vec2 a, Vec2 b, double eps = kEps) { return distance(a, b) <= eps; }

inline double degToRad(double deg) { return deg * kPi / 180.0; }
inline double radToDeg(double rad) { return rad * 180.0 / kPi; }

// Wraps an angle into [0, 2*pi).
double normalizeAngle(double a);
// True when angle `a` lies on the counter-clockwise sweep from `start` to `end`.
bool angleInSweep(double a, double start, double end, double eps = kEps);
// Counter-clockwise sweep from start to end, in (0, 2*pi].
double sweepAngle(double start, double end);

struct BBox {
    Vec2 min{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    Vec2 max{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};

    bool valid() const { return min.x <= max.x && min.y <= max.y; }
    void add(Vec2 p);
    void add(const BBox& b);
    BBox inflated(double d) const;
    bool intersects(const BBox& o) const;
    bool contains(const BBox& o) const;
    bool contains(Vec2 p) const;
    Vec2 center() const { return midpoint(min, max); }
    double width() const { return max.x - min.x; }
    double height() const { return max.y - min.y; }

    static BBox fromCorners(Vec2 a, Vec2 b);
};

// Affine transform: x' = a*x + c*y + tx, y' = b*x + d*y + ty.
struct Transform {
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, tx = 0.0, ty = 0.0;

    Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    Vec2 applyVector(Vec2 v) const { return {a * v.x + c * v.y, b * v.x + d * v.y}; }
    double determinant() const { return a * d - b * c; }
    // Uniform scale factor of the transform (1 for moves, rotations, mirrors).
    double scaleFactor() const { return std::sqrt(std::abs(determinant())); }
    bool isMirroring() const { return determinant() < 0.0; }

    // Returns the transform that applies `first`, then `*this`.
    Transform operator*(const Transform& first) const;

    static Transform translation(Vec2 delta);
    static Transform rotation(Vec2 center, double angle);
    static Transform scaling(Vec2 center, double factor);
    static Transform mirror(Vec2 p1, Vec2 p2);
};

Vec2 closestPointOnSegment(Vec2 p, Vec2 a, Vec2 b);
double distanceToSegment(Vec2 p, Vec2 a, Vec2 b);

// Intersections between bounded segments and full circles. Results are not
// filtered for arcs; callers clip them to the arc sweep.
std::vector<Vec2> intersectSegments(Vec2 a1, Vec2 a2, Vec2 b1, Vec2 b2);
std::vector<Vec2> intersectSegmentCircle(Vec2 a, Vec2 b, Vec2 center, double radius);
std::vector<Vec2> intersectCircles(Vec2 c1, double r1, Vec2 c2, double r2);

} // namespace cad
