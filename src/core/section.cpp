// SPDX-License-Identifier: GPL-3.0-or-later
#include "section.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace cad {

namespace {

// The plane is moved this far towards the viewer, so that it never passes
// exactly through a vertex: the cut then never has degenerate pieces.
constexpr double kPlaneShift = 0.0137;
// Hidden lines are tested at points this far apart (drawing units), and at
// least at this many points per edge.
constexpr double kSampleStep = 50.0;
constexpr int kMaxSamples = 400;

struct Frame {
    Vec2 origin, right, ahead;

    explicit Frame(const SectionPlane& p)
    {
        ahead = normalized(p.direction);
        right = Vec2{ahead.y, -ahead.x};
        origin = p.origin;
    }
    // x to the right, y up, depth ahead.
    double x(Vec3 v) const { return dot(Vec2{v.x, v.y} - origin, right); }
    double depth(Vec3 v) const { return dot(Vec2{v.x, v.y} - origin, ahead) - kPlaneShift; }
    Vec2 project(Vec3 v) const { return {x(v), v.z}; }
};

// Rounded point, so pieces computed from the same mesh edge meet exactly.
struct Key {
    long long x, y;
    bool operator<(const Key& o) const { return x != o.x ? x < o.x : y < o.y; }
};
Key keyOf(Vec2 p)
{
    return {std::llround(p.x * 1000.0), std::llround(p.y * 1000.0)};
}

// The point where the edge a-b crosses the plane, computed the same way
// whichever triangle the edge belongs to.
Vec2 crossing(const Frame& f, Vec3 a, Vec3 b)
{
    if (b.x < a.x || (b.x == a.x && (b.y < a.y || (b.y == a.y && b.z < a.z))))
        std::swap(a, b);
    const double da = f.depth(a);
    const double db = f.depth(b);
    const double t = da / (da - db);
    return f.project(a + (b - a) * t);
}

// Joins the cut segments into polylines; closed where they come back.
std::vector<Polyline> chain(const std::vector<std::array<Vec2, 2>>& segments)
{
    std::map<Key, std::vector<size_t>> at;
    for (size_t i = 0; i < segments.size(); ++i)
        for (const Vec2& p : segments[i])
            at[keyOf(p)].push_back(i);
    std::vector<bool> used(segments.size(), false);
    auto next = [&](Vec2 p) -> std::optional<size_t> {
        for (size_t i : at[keyOf(p)])
            if (!used[i])
                return i;
        return std::nullopt;
    };
    std::vector<Polyline> out;
    for (size_t start = 0; start < segments.size(); ++start) {
        if (used[start])
            continue;
        used[start] = true;
        std::vector<Vec2> pts{segments[start][0], segments[start][1]};
        // Forward from the end, then backward from the start.
        for (int pass = 0; pass < 2; ++pass) {
            while (auto i = next(pts.back())) {
                used[*i] = true;
                const auto& s = segments[*i];
                pts.push_back(keyOf(s[0]) < keyOf(pts.back()) || keyOf(pts.back()) < keyOf(s[0]) ? s[0] : s[1]);
            }
            std::reverse(pts.begin(), pts.end());
        }
        bool closed = pts.size() > 3 && !(keyOf(pts.front()) < keyOf(pts.back())) && !(keyOf(pts.back()) < keyOf(pts.front()));
        if (closed)
            pts.pop_back();
        // Drop the points in the middle of straight runs.
        std::vector<Vec2> clean;
        const size_t n = pts.size();
        for (size_t i = 0; i < n; ++i) {
            const bool end = !closed && (i == 0 || i + 1 == n);
            const Vec2 prev = pts[(i + n - 1) % n];
            const Vec2 next2 = pts[(i + 1) % n];
            if (end || std::abs(cross(pts[i] - prev, next2 - pts[i])) > 1e-6 * length(prev - pts[i]) * length(next2 - pts[i]) + 1e-9)
                clean.push_back(pts[i]);
        }
        if (clean.size() >= 2)
            out.emplace_back(std::move(clean), closed && clean.size() >= 3);
    }
    return out;
}

// Triangles in view coordinates, in a grid, to find which ones hide a point.
class Occluders {
public:
    struct Tri {
        std::array<Vec2, 3> p;
        std::array<double, 3> depth;
    };

    Occluders(const Frame& f, const Model3D& model)
    {
        for (const auto& t : model.triangles) {
            Tri tri;
            double maxDepth = -1e300;
            for (int i = 0; i < 3; ++i) {
                tri.p[i] = f.project(t.v[i]);
                tri.depth[i] = f.depth(t.v[i]);
                maxDepth = std::max(maxDepth, tri.depth[i]);
            }
            // Behind the viewer, or seen edge on.
            if (maxDepth <= 0.0 || std::abs(cross(tri.p[1] - tri.p[0], tri.p[2] - tri.p[0])) < 1e-6)
                continue;
            tris_.push_back(tri);
            for (const Vec2& p : tri.p)
                box_.add(p);
        }
        if (tris_.empty())
            return;
        cell_ = std::max({box_.width(), box_.height(), 1.0}) / 96.0;
        cols_ = static_cast<int>(box_.width() / cell_) + 1;
        rows_ = static_cast<int>(box_.height() / cell_) + 1;
        grid_.resize(static_cast<size_t>(cols_) * rows_);
        for (size_t i = 0; i < tris_.size(); ++i) {
            BBox b;
            for (const Vec2& p : tris_[i].p)
                b.add(p);
            for (int r = row(b.min.y); r <= row(b.max.y); ++r)
                for (int c = col(b.min.x); c <= col(b.max.x); ++c)
                    grid_[static_cast<size_t>(r) * cols_ + c].push_back(static_cast<int>(i));
        }
    }

    // The solid parts cut by the plane hide what is behind them: the model
    // has no faces there.
    void addCut(const std::vector<Polyline>& cut)
    {
        for (const Polyline& p : cut) {
            if (!p.closed)
                continue;
            BBox b;
            for (const Vec2& v : p.points)
                b.add(v);
            cuts_.push_back({&p, b});
        }
    }

    // True if a triangle lies between the plane and the point at `depth`.
    bool hidden(Vec2 p, double depth) const
    {
        for (const auto& [poly, b] : cuts_)
            if (depth > 1.0 && b.contains(p) && insideFar(poly->points, p))
                return true;
        if (tris_.empty() || !box_.contains(p))
            return false;
        const double tolerance = 1.0 + depth * 1e-6;
        for (int i : grid_[static_cast<size_t>(row(p.y)) * cols_ + col(p.x)]) {
            const Tri& t = tris_[i];
            const double area = cross(t.p[1] - t.p[0], t.p[2] - t.p[0]);
            const double w0 = cross(t.p[1] - p, t.p[2] - p) / area;
            const double w1 = cross(t.p[2] - p, t.p[0] - p) / area;
            const double w2 = 1.0 - w0 - w1;
            // On the outline counts: an edge lined up with a nearer face is
            // drawn by that face's edge. The edge's own faces are not nearer.
            constexpr double inside = -1e-9;
            if (w0 < inside || w1 < inside || w2 < inside)
                continue;
            const double d = w0 * t.depth[0] + w1 * t.depth[1] + w2 * t.depth[2];
            if (d > 0.0 && d < depth - tolerance)
                return true;
        }
        return false;
    }

private:
    // Inside the polygon and not on its outline.
    static bool insideFar(const std::vector<Vec2>& poly, Vec2 p)
    {
        bool inside = false;
        const size_t n = poly.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const Vec2 a = poly[j], b = poly[i];
            const Vec2 ab = b - a;
            const double len2 = dot(ab, ab);
            const double t = len2 > 0.0 ? std::clamp(dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
            if (length(p - (a + ab * t)) < 1.0)
                return false;
            if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (p.y - a.y) / (b.y - a.y) * ab.x)
                inside = !inside;
        }
        return inside;
    }

    int col(double x) const { return std::clamp(static_cast<int>((x - box_.min.x) / cell_), 0, cols_ - 1); }
    int row(double y) const { return std::clamp(static_cast<int>((y - box_.min.y) / cell_), 0, rows_ - 1); }

    std::vector<Tri> tris_;
    BBox box_;
    double cell_ = 1.0;
    int cols_ = 1, rows_ = 1;
    std::vector<std::vector<int>> grid_;
    std::vector<std::pair<const Polyline*, BBox>> cuts_;
};

// Clips the segment a-b (with depths) to depth >= 0 and to the x range.
bool clip(Vec2& a, Vec2& b, double& da, double& db, double from, double to)
{
    auto cut = [&](double va, double vb, double limit, bool keepAbove) {
        // Parameters t in [0,1] where the segment is on the kept side.
        const bool inA = keepAbove ? va >= limit : va <= limit;
        const bool inB = keepAbove ? vb >= limit : vb <= limit;
        if (!inA && !inB)
            return false;
        if (inA && inB)
            return true;
        const double t = (limit - va) / (vb - va);
        const Vec2 p = a + (b - a) * t;
        const double dp = da + (db - da) * t;
        if (inA) {
            b = p;
            db = dp;
        } else {
            a = p;
            da = dp;
        }
        return true;
    };
    return cut(da, db, 0.0, true) && cut(a.x, b.x, from, true) && cut(a.x, b.x, to, false);
}

} // namespace

double sectionX(const SectionPlane& plane, Vec2 p)
{
    return Frame(plane).x(Vec3{p, 0.0});
}

SectionDrawing sectionDrawing(const Model3D& model, const SectionPlane& plane)
{
    const Frame f(plane);
    const double from = plane.from.value_or(-1e300);
    const double to = plane.to.value_or(1e300);
    SectionDrawing out;

    // The cut: where each triangle crosses the plane.
    std::vector<std::array<Vec2, 2>> segments;
    for (const auto& t : model.triangles) {
        std::array<double, 3> d;
        for (int i = 0; i < 3; ++i)
            d[i] = f.depth(t.v[i]);
        std::vector<Vec2> pts;
        for (int i = 0; i < 3; ++i) {
            const int j = (i + 1) % 3;
            if ((d[i] < 0.0) != (d[j] < 0.0))
                pts.push_back(crossing(f, t.v[i], t.v[j]));
        }
        if (pts.size() == 2 && keyOf(pts[0]) < keyOf(pts[1]))
            segments.push_back({pts[0], pts[1]});
        else if (pts.size() == 2 && keyOf(pts[1]) < keyOf(pts[0]))
            segments.push_back({pts[1], pts[0]});
    }
    for (Polyline& p : chain(segments)) {
        // A section line cuts only between its ends.
        BBox b;
        for (const Vec2& v : p.points)
            b.add(v);
        if (b.max.x < from || b.min.x > to)
            continue;
        if (b.min.x < from || b.max.x > to) {
            // Partly outside: keep the pieces of its sides that are inside.
            const size_t n = p.points.size();
            for (size_t i = 0; i + (p.closed ? 0 : 1) < n; ++i) {
                Vec2 a = p.points[i], c = p.points[(i + 1) % n];
                double da = 1.0, dc = 1.0;
                if (clip(a, c, da, dc, from, to) && length(c - a) > 1e-6)
                    out.cut.push_back(Polyline({a, c}));
            }
            continue;
        }
        out.cut.push_back(std::move(p));
    }

    // Beyond: the visible parts of the edges ahead of the plane.
    Occluders occluders(f, model);
    occluders.addCut(out.cut);
    std::map<std::pair<Key, Key>, bool> seen;
    BBox all;
    // Nearest first: of edges that look the same, the nearest is drawn.
    std::vector<const std::array<Vec3, 2>*> edges;
    for (const auto& e : model.edges)
        edges.push_back(&e);
    auto nearest = [&](const std::array<Vec3, 2>* e) { return std::min(f.depth((*e)[0]), f.depth((*e)[1])); };
    std::stable_sort(edges.begin(), edges.end(), [&](auto* x, auto* y) { return nearest(x) < nearest(y); });
    for (const auto* edge : edges) {
        const auto& e = *edge;
        Vec2 a = f.project(e[0]), b = f.project(e[1]);
        double da = f.depth(e[0]), db = f.depth(e[1]);
        if (!clip(a, b, da, db, from, to))
            continue;
        const double len = length(b - a);
        if (len < 1e-6)
            continue;
        Key ka = keyOf(a), kb = keyOf(b);
        if (kb < ka)
            std::swap(ka, kb);
        if (!seen.emplace(std::make_pair(ka, kb), true).second)
            continue;
        const int n = std::clamp(static_cast<int>(std::ceil(len / kSampleStep)), 2, kMaxSamples);
        // Visible runs of the samples' intervals.
        int runStart = -1;
        auto close = [&](int end) {
            if (runStart < 0)
                return;
            out.beyond.push_back(Line{a + (b - a) * (double(runStart) / n), a + (b - a) * (double(end) / n)});
            runStart = -1;
        };
        for (int i = 0; i < n; ++i) {
            const double t = (i + 0.5) / n;
            const bool visible = !occluders.hidden(a + (b - a) * t, da + (db - da) * t);
            if (visible && runStart < 0)
                runStart = i;
            else if (!visible)
                close(i);
        }
        close(n);
    }
    for (const Polyline& p : out.cut)
        for (const Vec2& v : p.points)
            all.add(v);
    for (const Line& l : out.beyond) {
        all.add(l.a);
        all.add(l.b);
    }
    if (all.valid()) {
        const double margin = std::max(all.width() * 0.05, 500.0);
        out.ground = Line{{std::max(all.min.x - margin, from), 0.0}, {std::min(all.max.x + margin, to), 0.0}};
    }
    return out;
}

} // namespace cad
