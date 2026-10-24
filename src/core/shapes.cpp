// SPDX-License-Identifier: GPL-3.0-or-later
#include "shapes.h"

#include <algorithm>

namespace cad {

namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

// A single curve piece: either a segment or a circle/arc. Polylines are split
// into these so every pairwise query only has to handle two kinds.
struct Piece {
    bool isSegment = true;
    Vec2 a, b;          // segment
    Vec2 center;        // circle or arc
    double radius = 0.0;
    bool fullCircle = false;
    double start = 0.0, end = 0.0;

    static Piece segment(Vec2 a, Vec2 b)
    {
        Piece p;
        p.a = a;
        p.b = b;
        return p;
    }

    bool containsAngleOf(Vec2 p) const
    {
        return fullCircle || angleInSweep(angleOf(p - center), start, end, 1e-9);
    }
};

std::vector<Piece> pieces(const Shape& shape)
{
    std::vector<Piece> out;
    std::visit(Overloaded{
                   [&](const Line& l) { out.push_back(Piece::segment(l.a, l.b)); },
                   [&](const Circle& c) {
                       Piece p;
                       p.isSegment = false;
                       p.center = c.center;
                       p.radius = c.radius;
                       p.fullCircle = true;
                       out.push_back(p);
                   },
                   [&](const Arc& a) {
                       Piece p;
                       p.isSegment = false;
                       p.center = a.center;
                       p.radius = a.radius;
                       p.start = a.startAngle;
                       p.end = a.endAngle;
                       out.push_back(p);
                   },
                   [&](const Polyline& pl) {
                       const size_t n = pl.points.size();
                       for (size_t i = 0; i + 1 < n; ++i)
                           out.push_back(Piece::segment(pl.points[i], pl.points[i + 1]));
                       if (pl.closed && n > 2)
                           out.push_back(Piece::segment(pl.points[n - 1], pl.points[0]));
                   },
               },
               shape);
    return out;
}

std::vector<Vec2> intersectPieces(const Piece& p, const Piece& q)
{
    std::vector<Vec2> raw;
    if (p.isSegment && q.isSegment)
        return intersectSegments(p.a, p.b, q.a, q.b);
    if (p.isSegment)
        raw = intersectSegmentCircle(p.a, p.b, q.center, q.radius);
    else if (q.isSegment)
        raw = intersectSegmentCircle(q.a, q.b, p.center, p.radius);
    else
        raw = intersectCircles(p.center, p.radius, q.center, q.radius);

    std::vector<Vec2> out;
    for (Vec2 v : raw) {
        if ((p.isSegment || p.containsAngleOf(v)) && (q.isSegment || q.containsAngleOf(v)))
            out.push_back(v);
    }
    return out;
}

Vec2 nearestOnPiece(const Piece& piece, Vec2 p)
{
    if (piece.isSegment)
        return closestPointOnSegment(p, piece.a, piece.b);
    const Vec2 dir = p - piece.center;
    const Vec2 onCircle = piece.center + (length(dir) < kEps ? Vec2{piece.radius, 0.0} : normalized(dir) * piece.radius);
    if (piece.fullCircle || piece.containsAngleOf(p))
        return onCircle;
    const Vec2 s = piece.center + polar(piece.radius, piece.start);
    const Vec2 e = piece.center + polar(piece.radius, piece.end);
    return distance(p, s) <= distance(p, e) ? s : e;
}

} // namespace

BBox boundingBox(const Shape& shape)
{
    BBox box;
    std::visit(Overloaded{
                   [&](const Line& l) {
                       box.add(l.a);
                       box.add(l.b);
                   },
                   [&](const Circle& c) {
                       box.add(c.center - Vec2{c.radius, c.radius});
                       box.add(c.center + Vec2{c.radius, c.radius});
                   },
                   [&](const Arc& a) {
                       box.add(a.startPoint());
                       box.add(a.endPoint());
                       for (int k = 0; k < 4; ++k) {
                           const double ang = k * kPi / 2.0;
                           if (angleInSweep(ang, a.startAngle, a.endAngle))
                               box.add(a.center + polar(a.radius, ang));
                       }
                   },
                   [&](const Polyline& pl) {
                       for (Vec2 p : pl.points)
                           box.add(p);
                   },
               },
               shape);
    return box;
}

Vec2 nearestPoint(const Shape& shape, Vec2 p)
{
    Vec2 best = p;
    double bestDist = std::numeric_limits<double>::infinity();
    for (const Piece& piece : pieces(shape)) {
        const Vec2 q = nearestOnPiece(piece, p);
        const double d = distance(p, q);
        if (d < bestDist) {
            bestDist = d;
            best = q;
        }
    }
    return best;
}

double distanceTo(const Shape& shape, Vec2 p)
{
    return distance(p, nearestPoint(shape, p));
}

Shape transformed(const Shape& shape, const Transform& t)
{
    return std::visit(Overloaded{
                          [&](const Line& l) -> Shape { return Line{t.apply(l.a), t.apply(l.b)}; },
                          [&](const Circle& c) -> Shape { return Circle{t.apply(c.center), c.radius * t.scaleFactor()}; },
                          [&](const Arc& a) -> Shape {
                              Arc r;
                              r.center = t.apply(a.center);
                              r.radius = a.radius * t.scaleFactor();
                              const double s = angleOf(t.applyVector(polar(1.0, a.startAngle)));
                              const double e = angleOf(t.applyVector(polar(1.0, a.endAngle)));
                              // A mirror reverses the direction of travel, so the
                              // counter-clockwise arc now runs from the old end.
                              r.startAngle = normalizeAngle(t.isMirroring() ? e : s);
                              r.endAngle = normalizeAngle(t.isMirroring() ? s : e);
                              return r;
                          },
                          [&](const Polyline& pl) -> Shape {
                              Polyline r;
                              r.closed = pl.closed;
                              r.points.reserve(pl.points.size());
                              for (Vec2 p : pl.points)
                                  r.points.push_back(t.apply(p));
                              return r;
                          },
                      },
                      shape);
}

void collectSnapPoints(const Shape& shape, unsigned mask, std::vector<SnapPoint>& out)
{
    auto add = [&](Vec2 p, SnapType type) {
        if (mask & type)
            out.push_back({p, type});
    };
    std::visit(Overloaded{
                   [&](const Line& l) {
                       add(l.a, SnapEndpoint);
                       add(l.b, SnapEndpoint);
                       add(midpoint(l.a, l.b), SnapMidpoint);
                   },
                   [&](const Circle& c) {
                       add(c.center, SnapCenter);
                       for (int k = 0; k < 4; ++k)
                           add(c.center + polar(c.radius, k * kPi / 2.0), SnapQuadrant);
                   },
                   [&](const Arc& a) {
                       add(a.startPoint(), SnapEndpoint);
                       add(a.endPoint(), SnapEndpoint);
                       add(a.center + polar(a.radius, a.startAngle + a.sweep() / 2.0), SnapMidpoint);
                       add(a.center, SnapCenter);
                       for (int k = 0; k < 4; ++k) {
                           const double ang = k * kPi / 2.0;
                           if (angleInSweep(ang, a.startAngle, a.endAngle))
                               add(a.center + polar(a.radius, ang), SnapQuadrant);
                       }
                   },
                   [&](const Polyline& pl) {
                       const size_t n = pl.points.size();
                       for (Vec2 p : pl.points)
                           add(p, SnapEndpoint);
                       for (size_t i = 0; i + 1 < n; ++i)
                           add(midpoint(pl.points[i], pl.points[i + 1]), SnapMidpoint);
                       if (pl.closed && n > 2)
                           add(midpoint(pl.points[n - 1], pl.points[0]), SnapMidpoint);
                   },
               },
               shape);
}

std::vector<Vec2> intersections(const Shape& a, const Shape& b)
{
    std::vector<Vec2> out;
    const auto pa = pieces(a);
    const auto pb = pieces(b);
    for (const Piece& p : pa) {
        for (const Piece& q : pb) {
            for (Vec2 v : intersectPieces(p, q)) {
                const bool dup = std::any_of(out.begin(), out.end(), [&](Vec2 w) { return almostEqual(v, w, 1e-9); });
                if (!dup)
                    out.push_back(v);
            }
        }
    }
    return out;
}

bool crossesBox(const Shape& shape, const BBox& box)
{
    const BBox ext = boundingBox(shape);
    if (!ext.intersects(box))
        return false;
    if (box.contains(ext))
        return true;
    const Polyline border{{box.min, {box.max.x, box.min.y}, box.max, {box.min.x, box.max.y}}, true};
    if (!intersections(shape, border).empty())
        return true;
    // No edge crossing: the shape is either fully inside or fully outside.
    // Any point of the shape tells which.
    std::vector<SnapPoint> pts;
    collectSnapPoints(shape, SnapEndpoint | SnapQuadrant, pts);
    return !pts.empty() && box.contains(pts.front().point);
}

std::optional<Arc> arcFromThreePoints(Vec2 p1, Vec2 p2, Vec2 p3)
{
    const double d = 2.0 * (p1.x * (p2.y - p3.y) + p2.x * (p3.y - p1.y) + p3.x * (p1.y - p2.y));
    const double scale = std::max({distance(p1, p2), distance(p2, p3), distance(p1, p3)});
    if (std::abs(d) <= 1e-12 * scale * scale || scale < kEps)
        return std::nullopt;
    const double s1 = dot(p1, p1);
    const double s2 = dot(p2, p2);
    const double s3 = dot(p3, p3);
    Arc arc;
    arc.center = {(s1 * (p2.y - p3.y) + s2 * (p3.y - p1.y) + s3 * (p1.y - p2.y)) / d,
                  (s1 * (p3.x - p2.x) + s2 * (p1.x - p3.x) + s3 * (p2.x - p1.x)) / d};
    arc.radius = distance(arc.center, p1);
    const double a1 = normalizeAngle(angleOf(p1 - arc.center));
    const double a3 = normalizeAngle(angleOf(p3 - arc.center));
    const bool ccw = cross(p2 - p1, p3 - p2) > 0.0;
    arc.startAngle = ccw ? a1 : a3;
    arc.endAngle = ccw ? a3 : a1;
    return arc;
}

} // namespace cad
