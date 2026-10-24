// SPDX-License-Identifier: GPL-3.0-or-later
#include "hatch.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <numeric>
#include <unordered_map>

namespace cad {

namespace {

HatchPatternLine pat(double angleDeg, double ox, double oy, double dx, double dy, std::vector<double> dashes = {})
{
    return {degToRad(angleDeg), {ox, oy}, {dx, dy}, std::move(dashes)};
}

// Sizes from AutoCAD's metric pattern file (acadiso.pat), in millimetres.
const std::map<std::string, std::vector<HatchPatternLine>>& patterns()
{
    static const std::map<std::string, std::vector<HatchPatternLine>> table = {
        {"SOLID", {}},
        {"ANSI31", {pat(45, 0, 0, 0, 3.175)}},
        {"ANSI32", {pat(45, 0, 0, 0, 9.525), pat(45, 4.490128, 0, 0, 9.525)}},
        {"ANSI33", {pat(45, 0, 0, 0, 6.35), pat(45, 4.490128, 0, 0, 6.35, {3.175, -1.5875})}},
        {"ANSI34",
         {pat(45, 0, 0, 0, 19.05), pat(45, 4.490128, 0, 0, 19.05), pat(45, 8.980256, 0, 0, 19.05),
          pat(45, 13.470384, 0, 0, 19.05)}},
        {"ANSI37", {pat(45, 0, 0, 0, 3.175), pat(135, 0, 0, 0, 3.175)}},
        {"ANSI38", {pat(45, 0, 0, 0, 3.175), pat(135, 0, 0, 6.35, 3.175, {7.9375, -4.7625})}},
        {"LINE", {pat(0, 0, 0, 0, 3.175)}},
        {"NET", {pat(0, 0, 0, 0, 3.175), pat(90, 0, 0, 0, 3.175)}},
        {"DASH", {pat(0, 0, 0, 3.175, 3.175, {3.175, -3.175})}},
        {"DOTS", {pat(0, 0, 0, 0.79375, 1.5875, {0, -1.5875})}},
        {"SQUARE", {pat(0, 0, 0, 0, 3.175, {3.175, -3.175}), pat(90, 0, 0, 0, 3.175, {3.175, -3.175})}},
        {"BRICK",
         {pat(0, 0, 0, 0, 6.35), pat(90, 0, 0, 0, 12.7, {6.35, -6.35}), pat(90, 6.35, 0, 0, 12.7, {-6.35, 6.35})}},
        {"EARTH",
         {pat(0, 0, 0, 6.35, 6.35, {6.35, -6.35}), pat(0, 0, 2.38125, 6.35, 6.35, {6.35, -6.35}),
          pat(0, 0, 4.7625, 6.35, 6.35, {6.35, -6.35}), pat(90, 0.79375, 5.55625, 6.35, 6.35, {6.35, -6.35}),
          pat(90, 3.175, 5.55625, 6.35, 6.35, {6.35, -6.35}), pat(90, 5.55625, 5.55625, 6.35, 6.35, {6.35, -6.35})}},
        {"HONEY",
         {pat(0, 0, 0, 4.7625, 2.74963, {3.175, -6.35}), pat(120, 0, 0, 4.7625, 2.74963, {3.175, -6.35}),
          pat(60, 3.175, 0, 4.7625, 2.74963, {3.175, -6.35})}},
        {"CROSS", {pat(0, 0, 0, 6.35, 6.35, {3.175, -9.525}), pat(90, 1.5875, -1.5875, 6.35, 6.35, {3.175, -9.525})}},
        {"ZIGZAG", {pat(0, 0, 0, 3.175, 3.175, {3.175, -3.175}), pat(90, 3.175, 0, 3.175, 3.175, {3.175, -3.175})}},
    };
    return table;
}

bool insidePolygons(const std::vector<std::vector<Vec2>>& polys, Vec2 p)
{
    bool inside = false;
    for (const auto& poly : polys) {
        const size_t n = poly.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const Vec2 a = poly[j], b = poly[i];
            if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (b.x - a.x) * (p.y - a.y) / (b.y - a.y))
                inside = !inside;
        }
    }
    return inside;
}

BBox boxOf(const std::vector<std::vector<Vec2>>& polys)
{
    BBox box;
    for (const auto& poly : polys)
        for (Vec2 p : poly)
            box.add(p);
    return box;
}

} // namespace

const std::vector<HatchPatternLine>* hatchPattern(const std::string& name)
{
    const auto it = patterns().find(name);
    return it == patterns().end() ? nullptr : &it->second;
}

std::vector<std::string> hatchPatternNames()
{
    std::vector<std::string> names;
    for (const auto& [name, lines] : patterns())
        names.push_back(name);
    return names;
}

Hatch makeHatch(std::vector<Polyline> loops, const std::string& name, double scale, double angle)
{
    Hatch h;
    h.loops = std::move(loops);
    for (Polyline& pl : h.loops)
        pl.closed = true;
    h.pattern = name;
    if (const auto* lines = hatchPattern(name))
        h.lines = *lines;
    h.scale = scale;
    h.angle = angle;
    return h;
}

std::vector<std::vector<Vec2>> flattenLoops(const std::vector<Polyline>& loops, double maxStep)
{
    std::vector<std::vector<Vec2>> out;
    for (const Polyline& pl : loops) {
        const size_t n = pl.points.size();
        if (n < 2)
            continue;
        std::vector<Vec2> poly;
        for (size_t i = 0; i < n; ++i) {
            const Vec2 from = pl.points[i];
            const Vec2 to = pl.points[(i + 1) % n];
            poly.push_back(from);
            const double b = pl.bulge(i);
            if (std::abs(b) < 1e-12 || almostEqual(from, to))
                continue;
            const Arc arc = arcFromBulge(from, to, b);
            const double sweep = 4.0 * std::atan(b);
            const double a0 = angleOf(from - arc.center);
            const int steps = std::max(2, static_cast<int>(std::ceil(std::abs(sweep) / maxStep)));
            for (int k = 1; k < steps; ++k)
                poly.push_back(arc.center + polar(arc.radius, a0 + sweep * k / steps));
        }
        if (poly.size() >= 3)
            out.push_back(std::move(poly));
    }
    return out;
}

bool insideHatch(const Hatch& hatch, Vec2 p)
{
    return insidePolygons(flattenLoops(hatch.loops), p);
}

double hatchSpacing(const Hatch& hatch)
{
    double spacing = 0.0;
    for (const HatchPatternLine& l : hatch.lines) {
        const double d = std::abs(l.offset.y) * hatch.scale;
        if (d > 0.0 && (spacing == 0.0 || d < spacing))
            spacing = d;
    }
    return spacing;
}

std::vector<Line> hatchLines(const Hatch& hatch, const BBox& clip, size_t maxSegments, bool* tooDense)
{
    if (tooDense)
        *tooDense = false;
    std::vector<Line> out;
    if (hatch.solid() || hatch.scale <= 0.0)
        return out;
    const auto polys = flattenLoops(hatch.loops);
    const BBox area = boxOf(polys);
    if (!area.valid() || !area.intersects(clip))
        return out;
    BBox box;
    box.min = {std::max(area.min.x, clip.min.x), std::max(area.min.y, clip.min.y)};
    box.max = {std::min(area.max.x, clip.max.x), std::min(area.max.y, clip.max.y)};
    const double size = std::max({box.width(), box.height(), 1e-12});

    std::vector<std::pair<Vec2, Vec2>> edges;
    for (const auto& poly : polys)
        for (size_t i = 0; i < poly.size(); ++i)
            edges.push_back({poly[i], poly[(i + 1) % poly.size()]});

    const Transform rot = Transform::rotation({0, 0}, hatch.angle);
    const Vec2 corners[4] = {box.min, {box.max.x, box.min.y}, box.max, {box.min.x, box.max.y}};
    auto dense = [&] {
        if (tooDense)
            *tooDense = true;
        out.clear();
        return out;
    };

    std::vector<double> ts;
    for (const HatchPatternLine& pl : hatch.lines) {
        const Vec2 u = polar(1.0, hatch.angle + pl.angle);
        const Vec2 n = perpendicular(u);
        const Vec2 o = hatch.origin + rot.applyVector(pl.origin * hatch.scale);
        const double dx = pl.offset.x * hatch.scale;
        const double dy = pl.offset.y * hatch.scale;
        if (std::abs(dy) < 1e-12 * size)
            continue;
        const Vec2 step = u * dx + n * dy;
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (Vec2 c : corners) {
            const double k = dot(c - o, n) / dy;
            lo = std::min(lo, k);
            hi = std::max(hi, k);
        }
        const auto k0 = static_cast<long long>(std::floor(lo));
        const auto k1 = static_cast<long long>(std::ceil(hi));
        if (static_cast<double>(k1 - k0) + static_cast<double>(out.size()) > static_cast<double>(maxSegments))
            return dense();

        std::vector<double> dashes;
        double period = 0.0;
        for (double d : pl.dashes) {
            dashes.push_back(d * hatch.scale);
            period += std::abs(d) * hatch.scale;
        }
        const bool continuous = dashes.empty() || period < 1e-12 * size;

        for (long long k = k0; k <= k1; ++k) {
            const Vec2 p = o + step * static_cast<double>(k);
            // Part of the line inside the clip box.
            double tmin = -std::numeric_limits<double>::infinity(), tmax = -tmin;
            bool miss = false;
            for (int axis = 0; axis < 2; ++axis) {
                const double pu = axis == 0 ? u.x : u.y;
                const double pp = axis == 0 ? p.x : p.y;
                const double bmin = axis == 0 ? box.min.x : box.min.y;
                const double bmax = axis == 0 ? box.max.x : box.max.y;
                if (std::abs(pu) < 1e-15) {
                    miss = miss || pp < bmin || pp > bmax;
                    continue;
                }
                const double t1 = (bmin - pp) / pu, t2 = (bmax - pp) / pu;
                tmin = std::max(tmin, std::min(t1, t2));
                tmax = std::min(tmax, std::max(t1, t2));
            }
            if (miss || tmin > tmax)
                continue;

            ts.clear();
            for (const auto& [a, b] : edges) {
                const double sa = dot(a - p, n);
                const double sb = dot(b - p, n);
                // A vertex exactly on the line counts as above it, so it is
                // crossed once or not at all.
                if ((sa >= 0.0) != (sb >= 0.0))
                    ts.push_back(dot(a - p, u) + dot(b - a, u) * sa / (sa - sb));
            }
            std::sort(ts.begin(), ts.end());
            for (size_t i = 0; i + 1 < ts.size(); i += 2) {
                const double s0 = std::max(ts[i], tmin);
                const double s1 = std::min(ts[i + 1], tmax);
                if (s1 < s0)
                    continue;
                if (continuous) {
                    out.push_back(Line{p + u * s0, p + u * s1});
                } else {
                    double pos = std::floor(s0 / period) * period;
                    while (pos <= s1) {
                        for (double d : dashes) {
                            if (d > 0.0) {
                                const double a = std::max(pos, s0), b = std::min(pos + d, s1);
                                if (b > a)
                                    out.push_back(Line{p + u * a, p + u * b});
                            } else if (d == 0.0 && pos >= s0 && pos <= s1) {
                                out.push_back(Line{p + u * pos, p + u * pos});
                            }
                            pos += std::abs(d);
                        }
                        if (out.size() > maxSegments)
                            return dense();
                    }
                }
            }
            if (out.size() > maxSegments)
                return dense();
        }
    }
    return out;
}

std::vector<std::array<Vec2, 4>> hatchTrapezoids(const Hatch& hatch)
{
    std::vector<std::array<Vec2, 4>> out;
    const auto polys = flattenLoops(hatch.loops);
    std::vector<std::pair<Vec2, Vec2>> edges;
    std::vector<double> ys;
    for (const auto& poly : polys) {
        for (size_t i = 0; i < poly.size(); ++i) {
            const Vec2 a = poly[i], b = poly[(i + 1) % poly.size()];
            ys.push_back(a.y);
            if (a.y != b.y)
                edges.push_back(a.y < b.y ? std::pair{a, b} : std::pair{b, a});
        }
    }
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    auto xAt = [](const std::pair<Vec2, Vec2>& e, double y) {
        return e.first.x + (e.second.x - e.first.x) * (y - e.first.y) / (e.second.y - e.first.y);
    };
    std::vector<const std::pair<Vec2, Vec2>*> crossing;
    for (size_t s = 0; s + 1 < ys.size(); ++s) {
        const double y0 = ys[s], y1 = ys[s + 1];
        const double ym = (y0 + y1) / 2.0;
        crossing.clear();
        for (const auto& e : edges)
            if (e.first.y < ym && e.second.y > ym)
                crossing.push_back(&e);
        std::sort(crossing.begin(), crossing.end(), [&](auto* a, auto* b) { return xAt(*a, ym) < xAt(*b, ym); });
        for (size_t i = 0; i + 1 < crossing.size(); i += 2) {
            const auto& l = *crossing[i];
            const auto& r = *crossing[i + 1];
            out.push_back({Vec2{xAt(l, y0), y0}, Vec2{xAt(r, y0), y0}, Vec2{xAt(r, y1), y1}, Vec2{xAt(l, y1), y1}});
        }
    }
    return out;
}

// ---------------------------------------------------------------- boundary

namespace {

// The boundary search works on straight pieces: arcs are cut into short
// chords that remember their circle, so the arcs can be rebuilt at the end.
struct Circle2 {
    Vec2 center;
    double radius = 0.0;
};

struct Piece {
    Vec2 a, b;
    int circle = -1;  // index into the circles, -1 for a straight piece
    std::vector<std::pair<double, Vec2>> cuts;
};

class BoundaryFinder {
public:
    explicit BoundaryFinder(const std::vector<Shape>& shapes)
    {
        for (const Shape& s : shapes)
            addShape(s);
        BBox box;
        for (const Piece& p : pieces_) {
            box.add(p.a);
            box.add(p.b);
        }
        if (box.valid())
            tol_ = std::max({box.width(), box.height(), 1.0}) * 1e-9;
    }

    std::vector<Polyline> find(Vec2 point)
    {
        if (pieces_.empty())
            return {};
        cutPieces();
        buildGraph();
        pruneDangling();
        sortAround();
        traceFaces();

        // The smallest closed region around the point.
        int best = -1;
        for (size_t f = 0; f < faces_.size(); ++f) {
            const Face& face = faces_[f];
            if (face.area > tol_ * tol_ && insidePolygons({face.polygon}, point)
                && (best < 0 || face.area < faces_[static_cast<size_t>(best)].area))
                best = static_cast<int>(f);
        }
        if (best < 0)
            return {};
        const Face& region = faces_[static_cast<size_t>(best)];
        // Islands: separate groups of objects inside the region, by their
        // outline. As AutoCAD's default "outer" island detection, whatever
        // lies inside an island stays empty, so nested islands are left out.
        std::vector<const Face*> islands;
        for (const Face& face : faces_) {
            if (face.area < 0.0 && face.component != region.component
                && insidePolygons({region.polygon}, face.polygon.front()))
                islands.push_back(&face);
        }
        std::vector<Polyline> loops{toPolyline(region)};
        for (const Face* island : islands) {
            const bool nested = std::any_of(islands.begin(), islands.end(), [&](const Face* other) {
                return other != island && insidePolygons({other->polygon}, island->polygon.front());
            });
            if (!nested)
                loops.push_back(toPolyline(*island));
        }
        return loops;
    }

private:
    struct Edge {
        int from, to, circle;
        bool alive = true;
    };
    struct Face {
        std::vector<int> halfEdges;
        std::vector<Vec2> polygon;
        double area = 0.0;
        int component = 0;
    };

    void addChords(Vec2 center, double radius, double start, double sweep)
    {
        const int index = static_cast<int>(circles_.size());
        circles_.push_back({center, radius});
        const int steps = std::max(2, static_cast<int>(std::ceil(std::abs(sweep) / (kPi / 90.0))));
        Vec2 prev = center + polar(radius, start);
        for (int k = 1; k <= steps; ++k) {
            const Vec2 next = center + polar(radius, start + sweep * k / steps);
            pieces_.push_back({prev, next, index, {}});
            prev = next;
        }
    }

    void addLine(Vec2 a, Vec2 b)
    {
        if (!almostEqual(a, b))
            pieces_.push_back({a, b, -1, {}});
    }

    void addShape(const Shape& s)
    {
        if (const auto* l = std::get_if<Line>(&s)) {
            addLine(l->a, l->b);
        } else if (const auto* c = std::get_if<Circle>(&s)) {
            if (c->radius > kEps)
                addChords(c->center, c->radius, 0.0, kTwoPi);
        } else if (const auto* a = std::get_if<Arc>(&s)) {
            if (a->radius > kEps)
                addChords(a->center, a->radius, a->startAngle, a->sweep());
        } else if (const auto* pl = std::get_if<Polyline>(&s)) {
            for (size_t i = 0; i < pl->segmentCount(); ++i) {
                const Vec2 from = pl->points[i];
                const Vec2 to = pl->points[(i + 1) % pl->points.size()];
                const double b = pl->bulge(i);
                if (std::abs(b) < 1e-12 || almostEqual(from, to)) {
                    addLine(from, to);
                } else {
                    const Arc arc = arcFromBulge(from, to, b);
                    addChords(arc.center, arc.radius, angleOf(from - arc.center), 4.0 * std::atan(b));
                }
            }
        }
    }

    // Moves an intersection found on chords onto the true curves.
    Vec2 refine(const Piece& p, const Piece& q, Vec2 x) const
    {
        if (p.circle < 0 && q.circle < 0)
            return x;
        if (p.circle >= 0 && q.circle >= 0) {
            if (p.circle == q.circle)
                return x;
            const Circle2& c1 = circles_[static_cast<size_t>(p.circle)];
            const Circle2& c2 = circles_[static_cast<size_t>(q.circle)];
            return nearestOf(intersectCircles(c1.center, c1.radius, c2.center, c2.radius), x, p, q);
        }
        const Piece& line = p.circle < 0 ? p : q;
        const Piece& chord = p.circle < 0 ? q : p;
        const Circle2& c = circles_[static_cast<size_t>(chord.circle)];
        const Vec2 d = line.b - line.a;
        // The line extended, as the true crossing can lie just past the chord's.
        const auto hits = intersectSegmentCircle(line.a - d, line.b + d, c.center, c.radius);
        return nearestOf(hits, x, p, q);
    }

    static Vec2 nearestOf(const std::vector<Vec2>& candidates, Vec2 x, const Piece& p, const Piece& q)
    {
        const double reach = std::min(distance(p.a, p.b), distance(q.a, q.b));
        Vec2 best = x;
        double bestDist = reach;
        for (Vec2 c : candidates) {
            if (distance(c, x) < bestDist) {
                bestDist = distance(c, x);
                best = c;
            }
        }
        return best;
    }

    static double paramOn(const Piece& p, Vec2 x)
    {
        const Vec2 d = p.b - p.a;
        return std::clamp(dot(x - p.a, d) / dot(d, d), 0.0, 1.0);
    }

    void cutPieces()
    {
        for (Piece& p : pieces_) {
            p.cuts.push_back({0.0, p.a});
            p.cuts.push_back({1.0, p.b});
        }
        // Sweep along x so only pieces whose extents overlap are compared.
        std::vector<size_t> order(pieces_.size());
        std::iota(order.begin(), order.end(), 0);
        auto minX = [&](size_t i) { return std::min(pieces_[i].a.x, pieces_[i].b.x); };
        auto maxX = [&](size_t i) { return std::max(pieces_[i].a.x, pieces_[i].b.x); };
        std::sort(order.begin(), order.end(), [&](size_t i, size_t j) { return minX(i) < minX(j); });
        for (size_t oi = 0; oi < order.size(); ++oi) {
            const size_t i = order[oi];
            for (size_t oj = oi + 1; oj < order.size() && minX(order[oj]) <= maxX(i) + tol_; ++oj) {
                const size_t j = order[oj];
                Piece& p = pieces_[i];
                Piece& q = pieces_[j];
                if (std::min(p.a.y, p.b.y) > std::max(q.a.y, q.b.y) + tol_
                    || std::min(q.a.y, q.b.y) > std::max(p.a.y, p.b.y) + tol_)
                    continue;
                for (Vec2 x : intersectSegments(p.a, p.b, q.a, q.b)) {
                    const Vec2 r = refine(p, q, x);
                    p.cuts.push_back({paramOn(p, r), r});
                    q.cuts.push_back({paramOn(q, r), r});
                }
                // Ends lying on the other piece (T joints, overlaps).
                for (Vec2 e : {q.a, q.b})
                    if (distanceToSegment(e, p.a, p.b) <= tol_)
                        p.cuts.push_back({paramOn(p, e), e});
                for (Vec2 e : {p.a, p.b})
                    if (distanceToSegment(e, q.a, q.b) <= tol_)
                        q.cuts.push_back({paramOn(q, e), e});
            }
        }
    }

    int vertexAt(Vec2 p)
    {
        const double cell = tol_ * 4.0;
        const auto kx = static_cast<std::int64_t>(std::floor(p.x / cell));
        const auto ky = static_cast<std::int64_t>(std::floor(p.y / cell));
        for (std::int64_t dx = -1; dx <= 1; ++dx) {
            for (std::int64_t dy = -1; dy <= 1; ++dy) {
                const auto it = grid_.find(key(kx + dx, ky + dy));
                if (it == grid_.end())
                    continue;
                for (int v : it->second)
                    if (distance(vertices_[static_cast<size_t>(v)], p) <= tol_)
                        return v;
            }
        }
        const int v = static_cast<int>(vertices_.size());
        vertices_.push_back(p);
        grid_[key(kx, ky)].push_back(v);
        return v;
    }

    static std::uint64_t key(std::int64_t x, std::int64_t y)
    {
        return static_cast<std::uint64_t>(x) * 0x9E3779B97F4A7C15ull ^ static_cast<std::uint64_t>(y);
    }

    void buildGraph()
    {
        std::map<std::tuple<int, int, int>, bool> seen;
        for (Piece& p : pieces_) {
            std::sort(p.cuts.begin(), p.cuts.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            int prev = -1;
            for (const auto& [t, pos] : p.cuts) {
                const int v = vertexAt(pos);
                if (prev >= 0 && v != prev) {
                    const auto k = std::make_tuple(std::min(prev, v), std::max(prev, v), p.circle);
                    if (!seen[k]) {
                        seen[k] = true;
                        edges_.push_back({prev, v, p.circle});
                    }
                }
                prev = v;
            }
        }
        out_.assign(vertices_.size(), {});
        for (size_t e = 0; e < edges_.size(); ++e) {
            out_[static_cast<size_t>(edges_[e].from)].push_back(static_cast<int>(2 * e));
            out_[static_cast<size_t>(edges_[e].to)].push_back(static_cast<int>(2 * e + 1));
        }
    }

    int tail(int he) const
    {
        const Edge& e = edges_[static_cast<size_t>(he / 2)];
        return he % 2 == 0 ? e.from : e.to;
    }
    int head(int he) const { return tail(he ^ 1); }

    // Open ends cannot enclose anything: drop them, repeatedly.
    void pruneDangling()
    {
        std::vector<int> degree(vertices_.size(), 0);
        for (const Edge& e : edges_) {
            ++degree[static_cast<size_t>(e.from)];
            ++degree[static_cast<size_t>(e.to)];
        }
        std::vector<int> stack;
        for (size_t v = 0; v < vertices_.size(); ++v)
            if (degree[v] == 1)
                stack.push_back(static_cast<int>(v));
        while (!stack.empty()) {
            const int v = stack.back();
            stack.pop_back();
            for (int he : out_[static_cast<size_t>(v)]) {
                Edge& e = edges_[static_cast<size_t>(he / 2)];
                if (!e.alive)
                    continue;
                e.alive = false;
                for (int w : {e.from, e.to})
                    if (--degree[static_cast<size_t>(w)] == 1)
                        stack.push_back(w);
            }
        }
        for (auto& list : out_)
            list.erase(std::remove_if(list.begin(), list.end(), [&](int he) { return !edges_[static_cast<size_t>(he / 2)].alive; }),
                       list.end());
    }

    void sortAround()
    {
        position_.assign(edges_.size() * 2, -1);
        for (size_t v = 0; v < out_.size(); ++v) {
            auto& list = out_[v];
            const Vec2 o = vertices_[v];
            std::sort(list.begin(), list.end(), [&](int a, int b) {
                return angleOf(vertices_[static_cast<size_t>(head(a))] - o) < angleOf(vertices_[static_cast<size_t>(head(b))] - o);
            });
            for (size_t i = 0; i < list.size(); ++i)
                position_[static_cast<size_t>(list[i])] = static_cast<int>(i);
        }
    }

    // Next half-edge of the face on the left of `he`: at its end, the edge
    // just clockwise from the way back, so the face stays on the left.
    int next(int he) const
    {
        const auto& list = out_[static_cast<size_t>(head(he))];
        const int i = position_[static_cast<size_t>(he ^ 1)];
        const int n = static_cast<int>(list.size());
        return list[static_cast<size_t>((i - 1 + n) % n)];
    }

    void traceFaces()
    {
        // Connected groups of objects.
        std::vector<int> parent(vertices_.size());
        std::iota(parent.begin(), parent.end(), 0);
        auto find = [&](int v) {
            while (parent[static_cast<size_t>(v)] != v)
                v = parent[static_cast<size_t>(v)] = parent[static_cast<size_t>(parent[static_cast<size_t>(v)])];
            return v;
        };
        for (const Edge& e : edges_)
            if (e.alive)
                parent[static_cast<size_t>(find(e.from))] = find(e.to);

        std::vector<bool> used(edges_.size() * 2, false);
        for (size_t start = 0; start < used.size(); ++start) {
            if (used[start] || !edges_[start / 2].alive)
                continue;
            Face face;
            int he = static_cast<int>(start);
            while (!used[static_cast<size_t>(he)] && face.halfEdges.size() <= used.size()) {
                used[static_cast<size_t>(he)] = true;
                face.halfEdges.push_back(he);
                face.polygon.push_back(vertices_[static_cast<size_t>(tail(he))]);
                he = next(he);
            }
            double area = 0.0;
            for (size_t i = 0; i < face.polygon.size(); ++i)
                area += cross(face.polygon[i], face.polygon[(i + 1) % face.polygon.size()]);
            face.area = area / 2.0;
            face.component = find(tail(face.halfEdges.front()));
            faces_.push_back(std::move(face));
        }
    }

    // The face's outline as a polyline, chords merged back into arcs and
    // collinear pieces into one segment.
    Polyline toPolyline(const Face& face) const
    {
        const auto& hes = face.halfEdges;
        const size_t n = hes.size();
        auto circleOf = [&](size_t i) { return edges_[static_cast<size_t>(hes[i % n] / 2)].circle; };
        // Start where a run of chords of one circle begins.
        size_t start = 0;
        bool found = false;
        for (size_t i = 0; i < n && !found; ++i) {
            if (circleOf(i) < 0 || circleOf(i) != circleOf(i + n - 1)) {
                start = i;
                found = true;
            }
        }
        Polyline pl;
        pl.closed = true;
        auto pos = [&](int v) { return vertices_[static_cast<size_t>(v)]; };
        size_t i = 0;
        while (i < n) {
            const size_t k = (start + i) % n;
            const int circle = circleOf(k);
            const Vec2 from = pos(tail(hes[k]));
            if (circle < 0) {
                // Skip a vertex where the outline goes straight on.
                if (!pl.points.empty() && pl.bulges.size() == pl.points.size() && pl.bulges.back() == 0.0) {
                    const Vec2 before = pl.points.back();
                    const Vec2 to = pos(head(hes[k]));
                    if (std::abs(cross(from - before, to - from)) <= tol_ * distance(before, to) && dot(from - before, to - from) > 0.0) {
                        ++i;
                        continue;
                    }
                }
                pl.points.push_back(from);
                pl.bulges.push_back(0.0);
                ++i;
                continue;
            }
            // A whole circle is split in two halves.
            const size_t limit = found ? n : n / 2;
            const Vec2 c = circles_[static_cast<size_t>(circle)].center;
            double sweep = 0.0;
            size_t j = i;
            while (j < n && j - i < limit && circleOf(start + j) == circle) {
                const size_t kk = (start + j) % n;
                const Vec2 a = pos(tail(hes[kk])) - c;
                const Vec2 b = pos(head(hes[kk])) - c;
                sweep += std::atan2(cross(a, b), dot(a, b));
                ++j;
            }
            pl.points.push_back(from);
            pl.bulges.push_back(bulgeOfSweep(sweep));
            i = j;
        }
        // The first vertex may also lie on a straight run that closes the loop.
        if (pl.points.size() > 3 && pl.bulges.back() == 0.0 && pl.bulges.front() == 0.0) {
            const Vec2 before = pl.points.back(), at = pl.points.front(), after = pl.points[1];
            if (std::abs(cross(at - before, after - at)) <= tol_ * distance(before, after) && dot(at - before, after - at) > 0.0) {
                pl.points.erase(pl.points.begin());
                pl.bulges.erase(pl.bulges.begin());
            }
        }
        return pl;
    }

    std::vector<Piece> pieces_;
    std::vector<Circle2> circles_;
    double tol_ = 1e-9;
    std::vector<Vec2> vertices_;
    std::unordered_map<std::uint64_t, std::vector<int>> grid_;
    std::vector<Edge> edges_;
    std::vector<std::vector<int>> out_;
    std::vector<int> position_;
    std::vector<Face> faces_;
};

} // namespace

std::vector<Polyline> findHatchBoundary(const std::vector<Shape>& shapes, Vec2 point)
{
    return BoundaryFinder(shapes).find(point);
}

} // namespace cad
