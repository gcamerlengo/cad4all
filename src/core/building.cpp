// SPDX-License-Identifier: GPL-3.0-or-later
#include "building.h"

#include "hatch.h"

#include <algorithm>
#include <limits>

namespace cad {

namespace {

constexpr double kHuge = std::numeric_limits<double>::max() / 4.0;

// Signed area, positive when counter-clockwise.
double polygonArea(const std::vector<Vec2>& poly)
{
    double a = 0.0;
    for (size_t k = 0; k < poly.size(); ++k)
        a += cross(poly[k], poly[(k + 1) % poly.size()]);
    return a / 2.0;
}

bool degenerate(const Wall& w) { return wallLength(w) < kEps || w.thickness <= kEps; }

double tolerance(const Wall& w) { return 1e-6 * std::max(1.0, w.thickness); }

struct FaceLine {
    Vec2 p, d;
};

std::optional<Vec2> intersectLines(const FaceLine& a, const FaceLine& b)
{
    const double den = cross(a.d, b.d);
    if (std::abs(den) < 1e-12)
        return std::nullopt;
    return a.p + a.d * (cross(b.p - a.p, b.d) / den);
}

FaceLine faceLine(const Wall& w, double offset) { return {wallPoint(w, 0.0, offset), wallDirection(w)}; }

Vec2 endPoint(const Wall& w, bool atStart) { return atStart ? w.start : w.end; }
// Direction leaving the joint along the wall.
Vec2 awayDirection(const Wall& w, bool atStart) { return atStart ? wallDirection(w) : -wallDirection(w); }
// The faces on the left and on the right when leaving the joint.
FaceLine awayLeft(const Wall& w, bool atStart) { return faceLine(w, atStart ? wallLeftOffset(w) : wallRightOffset(w)); }
FaceLine awayRight(const Wall& w, bool atStart) { return faceLine(w, atStart ? wallRightOffset(w) : wallLeftOffset(w)); }

// `onLeft` and `onRight` are the corners on the faces at the left and the
// right when leaving the joint.
void setEndCorners(WallFaces& f, bool atStart, Vec2 onLeft, Vec2 onRight)
{
    if (atStart) {
        f.corners[3] = onLeft;
        f.corners[0] = onRight;
    } else {
        f.corners[1] = onLeft;
        f.corners[2] = onRight;
    }
}

void markJoined(WallFaces& f, bool atStart, bool straight)
{
    (atStart ? f.startJoined : f.endJoined) = true;
    (atStart ? f.startStraight : f.endStraight) = straight;
}

bool sameLine(const FaceLine& a, const FaceLine& b, double tol) { return std::abs(cross(b.p - a.p, a.d)) <= tol; }

// Two walls whose ends meet at the same point.
void joinCorner(const Wall& wi, bool si, const Wall& wj, bool sj, WallFaces& fi)
{
    const Vec2 ai = awayDirection(wi, si);
    const Vec2 aj = awayDirection(wj, sj);
    const double tol = std::max(tolerance(wi), tolerance(wj));
    if (std::abs(cross(ai, aj)) < 1e-9) {
        // Collinear: one wall continues the other when their faces line up.
        if (dot(ai, aj) < 0.0 && sameLine(awayLeft(wi, si), awayRight(wj, sj), tol)
            && sameLine(awayRight(wi, si), awayLeft(wj, sj), tol))
            markJoined(fi, si, true);
        return;
    }
    const auto left = intersectLines(awayLeft(wi, si), awayRight(wj, sj));
    const auto right = intersectLines(awayRight(wi, si), awayLeft(wj, sj));
    if (!left || !right)
        return;
    // Very sharp corners would make endless spikes: leave them square.
    const Vec2 p = endPoint(wi, si);
    const double limit = 4.0 * std::max(wi.thickness, wj.thickness) + tol;
    if (distance(*left, p) > limit || distance(*right, p) > limit)
        return;
    setEndCorners(fi, si, *left, *right);
    markJoined(fi, si, false);
}

// Wall i ends against the side of wall j.
bool joinTee(const Wall& wi, bool si, const Wall& wj, WallFaces& fi, WallFaces& fj)
{
    const Vec2 nj = wallNormal(wj);
    const double side = dot(awayDirection(wi, si), nj);
    if (std::abs(side) < 0.1)
        return false;
    const FaceLine nearFace = faceLine(wj, side > 0.0 ? wallLeftOffset(wj) : wallRightOffset(wj));
    const auto left = intersectLines(awayLeft(wi, si), nearFace);
    const auto right = intersectLines(awayRight(wi, si), nearFace);
    if (!left || !right)
        return false;
    setEndCorners(fi, si, *left, *right);
    markJoined(fi, si, false);
    const double ta = wallParam(wj, *left);
    const double tb = wallParam(wj, *right);
    (side > 0.0 ? fj.leftGaps : fj.rightGaps).push_back({std::min(ta, tb), std::max(ta, tb)});
    return true;
}

bool insideWall(const Wall& w, Vec2 p, double tol)
{
    const double t = wallParam(w, p);
    const double o = dot(p - w.start, wallNormal(w));
    return t > tol && t < wallLength(w) - tol && o >= wallRightOffset(w) - tol && o <= wallLeftOffset(w) + tol;
}

// Parts of [from, to] outside the `gaps`.
std::vector<std::pair<double, double>> subtract(double from, double to, std::vector<std::pair<double, double>> gaps)
{
    std::sort(gaps.begin(), gaps.end());
    std::vector<std::pair<double, double>> out;
    double x = from;
    for (const auto& [a, b] : gaps) {
        if (b <= x)
            continue;
        if (a >= to)
            break;
        if (a > x)
            out.push_back({x, a});
        x = std::max(x, b);
    }
    if (to > x)
        out.push_back({x, to});
    return out;
}

std::vector<std::pair<double, double>> openingRanges(const Wall& w)
{
    std::vector<std::pair<double, double>> out;
    for (const Opening& o : w.openings)
        if (o.width > 0.0)
            out.push_back({openingStart(o), openingEnd(o)});
    return out;
}

// Keeps the part of a convex polygon between two distances along the wall axis.
// Keeps the part of a convex polygon where `inside` is not negative.
template <class F>
std::vector<Vec2> clip(const std::vector<Vec2>& in, F inside)
{
    std::vector<Vec2> out;
    for (size_t k = 0; k < in.size(); ++k) {
        const Vec2 a = in[k];
        const Vec2 b = in[(k + 1) % in.size()];
        const double fa = inside(a);
        const double fb = inside(b);
        if (fa >= 0.0)
            out.push_back(a);
        if ((fa >= 0.0) != (fb >= 0.0))
            out.push_back(a + (b - a) * (fa / (fa - fb)));
    }
    return out;
}

std::vector<Vec2> clipAlongWall(std::vector<Vec2> poly, const Wall& w, double tmin, double tmax)
{
    if (tmin > -kHuge)
        poly = clip(poly, [&](Vec2 p) { return wallParam(w, p) - tmin; });
    if (tmax < kHuge && !poly.empty())
        poly = clip(poly, [&](Vec2 p) { return tmax - wallParam(w, p); });
    return poly;
}

class ModelBuilder {
public:
    explicit ModelBuilder(Model3D& model) : m_(model) {}

    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Model3D::Material mat = Model3D::Material::Wall)
    {
        m_.triangles.push_back({{a, b, c}, mat});
        m_.triangles.push_back({{a, c, d}, mat});
    }

    void polygon(const std::vector<Vec2>& pts, double z, Model3D::Material mat = Model3D::Material::Wall)
    {
        for (size_t k = 1; k + 1 < pts.size(); ++k)
            m_.triangles.push_back({{Vec3(pts[0], z), Vec3(pts[k], z), Vec3(pts[k + 1], z)}, mat});
    }

    void edge(Vec3 a, Vec3 b)
    {
        if (length(b - a) > kEps)
            m_.edges.push_back({a, b});
    }

    void box(const Wall& w, double t0, double t1, double o0, double o1, double z0, double z1, Model3D::Material mat)
    {
        auto p = [&](double t, double o, double z) { return Vec3(wallPoint(w, t, o), z); };
        const Vec3 c[8] = {p(t0, o0, z0), p(t1, o0, z0), p(t1, o1, z0), p(t0, o1, z0),
                           p(t0, o0, z1), p(t1, o0, z1), p(t1, o1, z1), p(t0, o1, z1)};
        quad(c[0], c[1], c[2], c[3], mat);
        quad(c[4], c[5], c[6], c[7], mat);
        for (int k = 0; k < 4; ++k) {
            const int n = (k + 1) % 4;
            quad(c[k], c[n], c[n + 4], c[k + 4], mat);
            edge(c[k], c[n]);
            edge(c[k + 4], c[n + 4]);
            edge(c[k], c[k + 4]);
        }
    }

    void wall(const Wall& w, const WallFaces& f);
    void slab(const Slab& s);
    void roof(const Roof& r);

private:
    Model3D& m_;
};

struct Cut {
    double t0, t1, zb, zt;
    const Opening* opening;
};

void ModelBuilder::wall(const Wall& w, const WallFaces& f)
{
    const double h = w.height;
    if (degenerate(w) || h <= kEps)
        return;
    const double l = wallLeftOffset(w);
    const double r = wallRightOffset(w);

    std::vector<Cut> cuts;
    for (const Opening& o : w.openings) {
        const double zb = std::clamp(o.sill, 0.0, h);
        const double zt = std::clamp(o.sill + o.height, 0.0, h);
        if (o.width > kEps && zt - zb > kEps)
            cuts.push_back({openingStart(o), openingEnd(o), zb, zt, &o});
    }
    std::sort(cuts.begin(), cuts.end(), [](const Cut& a, const Cut& b) { return a.t0 < b.t0; });

    auto face = [&](double offset, Vec2 from, Vec2 to, const std::vector<std::pair<double, double>>& gaps) {
        const double ta = wallParam(w, from);
        const double tb = wallParam(w, to);
        if (tb - ta <= kEps)
            return;
        auto P = [&](double t, double z) { return Vec3(wallPoint(w, t, offset), z); };
        double x = ta;
        std::vector<std::pair<double, double>> bottomGaps = gaps;
        std::vector<std::pair<double, double>> topGaps = gaps;
        for (const Cut& c : cuts) {
            const double c0 = std::max(c.t0, ta);
            const double c1 = std::min(c.t1, tb);
            if (c1 - c0 <= kEps)
                continue;
            if (c0 > x)
                quad(P(x, 0), P(c0, 0), P(c0, h), P(x, h));
            if (c.zb > 0.0)
                quad(P(c0, 0), P(c1, 0), P(c1, c.zb), P(c0, c.zb));
            else
                bottomGaps.push_back({c0, c1});
            if (c.zt < h)
                quad(P(c0, c.zt), P(c1, c.zt), P(c1, h), P(c0, h));
            else
                topGaps.push_back({c0, c1});
            edge(P(c0, c.zb), P(c0, c.zt));
            edge(P(c1, c.zb), P(c1, c.zt));
            if (c.zb > 0.0)
                edge(P(c0, c.zb), P(c1, c.zb));
            if (c.zt < h)
                edge(P(c0, c.zt), P(c1, c.zt));
            x = std::max(x, c1);
        }
        if (tb > x)
            quad(P(x, 0), P(tb, 0), P(tb, h), P(x, h));
        for (const auto& [a, b] : subtract(ta, tb, bottomGaps))
            edge(P(a, 0), P(b, 0));
        for (const auto& [a, b] : subtract(ta, tb, topGaps))
            edge(P(a, h), P(b, h));
        if (!f.startStraight)
            edge(P(ta, 0), P(ta, h));
        if (!f.endStraight)
            edge(P(tb, 0), P(tb, h));
    };
    face(l, f.corners[3], f.corners[2], f.leftGaps);
    face(r, f.corners[0], f.corners[1], f.rightGaps);

    auto endFace = [&](Vec2 a, Vec2 b) {
        quad(Vec3(a, 0), Vec3(b, 0), Vec3(b, h), Vec3(a, h));
        edge(Vec3(a, 0), Vec3(b, 0));
        edge(Vec3(a, h), Vec3(b, h));
    };
    if (!f.startJoined)
        endFace(f.corners[0], f.corners[3]);
    if (!f.endJoined)
        endFace(f.corners[1], f.corners[2]);

    // Top and bottom, interrupted where an opening reaches them.
    auto cap = [&](double z, bool top) {
        std::vector<std::pair<double, double>> gaps;
        for (const Cut& c : cuts)
            if (top ? c.zt >= h : c.zb <= 0.0)
                gaps.push_back({c.t0, c.t1});
        const std::vector<Vec2> footprint(f.corners.begin(), f.corners.end());
        for (const auto& [a, b] : subtract(-kHuge, kHuge, gaps))
            polygon(clipAlongWall(footprint, w, a, b), z);
    };
    cap(h, true);
    cap(0.0, false);

    // Reveals around each opening, then the door leaf or the window pane.
    const double mid = (l + r) / 2.0;
    const double thick = l - r;
    for (const Cut& c : cuts) {
        auto L = [&](double t, double z) { return Vec3(wallPoint(w, t, l), z); };
        auto R = [&](double t, double z) { return Vec3(wallPoint(w, t, r), z); };
        quad(L(c.t0, c.zb), R(c.t0, c.zb), R(c.t0, c.zt), L(c.t0, c.zt));
        quad(L(c.t1, c.zb), R(c.t1, c.zb), R(c.t1, c.zt), L(c.t1, c.zt));
        if (c.zt < h)
            quad(L(c.t0, c.zt), L(c.t1, c.zt), R(c.t1, c.zt), R(c.t0, c.zt));
        if (c.zb > 0.0)
            quad(L(c.t0, c.zb), L(c.t1, c.zb), R(c.t1, c.zb), R(c.t0, c.zb));
        if (c.opening->kind == Opening::Kind::Door) {
            // Closed leaf, a little smaller than the opening so its faces do
            // not overlap the reveals.
            const double half = thick * 0.08;
            const double gap = thick * 0.01;
            box(w, c.t0 + gap, c.t1 - gap, mid - half, mid + half, c.zb, c.zt - gap, Model3D::Material::Wood);
        } else {
            auto G = [&](double t, double z) { return Vec3(wallPoint(w, t, mid), z); };
            quad(G(c.t0, c.zb), G(c.t1, c.zb), G(c.t1, c.zt), G(c.t0, c.zt), Model3D::Material::Glass);
            edge(G(c.t0, c.zb), G(c.t1, c.zb));
            edge(G(c.t1, c.zb), G(c.t1, c.zt));
            edge(G(c.t1, c.zt), G(c.t0, c.zt));
            edge(G(c.t0, c.zt), G(c.t0, c.zb));
        }
    }
}


void ModelBuilder::slab(const Slab& sl)
{
    const double top = sl.offset;
    const double bottom = sl.offset - sl.thickness;
    if (sl.thickness <= kEps || sl.loops.empty())
        return;
    Hatch area;
    area.loops = sl.loops;
    for (const auto& t : hatchTrapezoids(area)) {
        for (double z : {top, bottom}) {
            quad(Vec3(t[0], z), Vec3(t[1], z), Vec3(t[2], z), Vec3(t[3], z));
        }
    }
    for (const auto& poly : flattenLoops(sl.loops)) {
        const size_t n = poly.size();
        for (size_t k = 0; k < n; ++k) {
            const Vec2 a = poly[k];
            const Vec2 b = poly[(k + 1) % n];
            quad(Vec3(a, bottom), Vec3(b, bottom), Vec3(b, top), Vec3(a, top));
            edge(Vec3(a, top), Vec3(b, top));
            edge(Vec3(a, bottom), Vec3(b, bottom));
            // Vertical edges only at real corners, not along flattened arcs.
            const Vec2 before = normalized(a - poly[(k + n - 1) % n]);
            const Vec2 after = normalized(b - a);
            if (dot(before, after) < std::cos(degToRad(15.0)))
                edge(Vec3(a, bottom), Vec3(a, top));
        }
    }
}

void ModelBuilder::roof(const Roof& r)
{
    const auto g = roofGeometry(r);
    if (!g || r.thickness <= kEps)
        return;
    const bool flat = g->faces.size() == 1 && g->faces[0].edge < 0;
    const double rise = flat ? r.thickness : r.thickness / std::cos(r.pitch);
    auto under = [&](Vec2 p) { return roofHeight(r, p); };

    for (const RoofFace& f : g->faces) {
        std::vector<Vec3> bottom, top;
        for (Vec2 v : f.polygon) {
            bottom.push_back(Vec3(v, under(v)));
            top.push_back(Vec3(v, under(v) + rise));
        }
        for (size_t k = 1; k + 1 < top.size(); ++k) {
            m_.triangles.push_back({{bottom[0], bottom[k], bottom[k + 1]}, Model3D::Material::Roof});
            m_.triangles.push_back({{top[0], top[k], top[k + 1]}, Model3D::Material::Roof});
        }
        for (size_t k = 0; k < top.size(); ++k)
            edge(top[k], top[(k + 1) % top.size()]);
    }

    // The edge of the roof all around, split where the faces meet it.
    const size_t n = g->outline.size();
    for (size_t k = 0; k < n; ++k) {
        const Vec2 a = g->outline[k];
        const Vec2 b = g->outline[(k + 1) % n];
        const double len = distance(a, b);
        if (len < kEps)
            continue;
        std::vector<double> cuts{0.0, 1.0};
        for (const RoofFace& f : g->faces)
            for (Vec2 v : f.polygon)
                if (distanceToSegment(v, a, b) < 1e-6 * std::max(1.0, len))
                    cuts.push_back(std::clamp(dot(v - a, b - a) / (len * len), 0.0, 1.0));
        std::sort(cuts.begin(), cuts.end());
        for (size_t c = 0; c + 1 < cuts.size(); ++c) {
            if (cuts[c + 1] - cuts[c] < 1e-9)
                continue;
            const Vec2 p = a + (b - a) * cuts[c];
            const Vec2 q = a + (b - a) * cuts[c + 1];
            quad(Vec3(p, under(p)), Vec3(q, under(q)), Vec3(q, under(q) + rise), Vec3(p, under(p) + rise),
                 Model3D::Material::Roof);
            edge(Vec3(p, under(p)), Vec3(q, under(q)));
        }
        edge(Vec3(a, under(a)), Vec3(a, under(a) + rise));
    }

    // Gable ends: the wall goes up to the underside of the roof.
    const size_t m = g->footprint.size();
    for (size_t i = 0; i < m; ++i) {
        if (!r.gable(i) || flat)
            continue;
        const Vec2 a = g->footprint[i];
        const Vec2 b = g->footprint[(i + 1) % m];
        for (const RoofFace& f : g->faces) {
            // The part of the edge under this face.
            double t0 = 0.0, t1 = 1.0;
            const auto& poly = f.polygon;
            const double sign = polygonArea(poly) > 0.0 ? 1.0 : -1.0;
            for (size_t k = 0; k < poly.size() && t0 < t1; ++k) {
                const Vec2 p = poly[k];
                const Vec2 e = poly[(k + 1) % poly.size()] - p;
                // Inside: on the left of each edge of a counter-clockwise polygon.
                const double fa = sign * cross(e, a - p);
                const double fb = sign * cross(e, b - p);
                if (fa < 0.0 && fb < 0.0)
                    t1 = -1.0;
                else if (fa < 0.0)
                    t0 = std::max(t0, fa / (fa - fb));
                else if (fb < 0.0)
                    t1 = std::min(t1, fa / (fa - fb));
            }
            if (t1 - t0 < 1e-9)
                continue;
            const Vec2 p = a + (b - a) * t0;
            const Vec2 q = a + (b - a) * t1;
            if (under(p) - r.base < kEps && under(q) - r.base < kEps)
                continue;
            quad(Vec3(p, r.base), Vec3(q, r.base), Vec3(q, under(q)), Vec3(p, under(p)));
        }
    }
}

} // namespace

Vec2 wallDirection(const Wall& w) { return normalized(w.end - w.start); }

double wallLeftOffset(const Wall& w)
{
    switch (w.justification) {
    case Wall::Justification::Left:
        return 0.0;
    case Wall::Justification::Right:
        return w.thickness;
    case Wall::Justification::Center:
        break;
    }
    return w.thickness / 2.0;
}

double wallRightOffset(const Wall& w) { return wallLeftOffset(w) - w.thickness; }

double wallParam(const Wall& w, Vec2 p) { return dot(p - w.start, wallDirection(w)); }

Vec2 wallPoint(const Wall& w, double t, double offset) { return w.start + wallDirection(w) * t + wallNormal(w) * offset; }

std::optional<Opening> fitOpening(const Wall& w, Opening o)
{
    const double len = wallLength(w);
    if (o.width <= kEps || o.width > len + kEps)
        return std::nullopt;
    o.position = std::clamp(o.position, o.width / 2.0, len - o.width / 2.0);
    o.sill = std::clamp(o.sill, 0.0, w.height);
    o.height = std::min(o.height, w.height - o.sill);
    if (o.height <= kEps)
        return std::nullopt;
    const double eps = 1e-9 * std::max(1.0, len);
    for (const Opening& e : w.openings)
        if (openingStart(o) < openingEnd(e) - eps && openingEnd(o) > openingStart(e) + eps)
            return std::nullopt;
    return o;
}

WallFaces wallFaces(const Wall& w)
{
    WallFaces f;
    const double l = wallLeftOffset(w);
    const double r = wallRightOffset(w);
    const double len = wallLength(w);
    f.corners = {wallPoint(w, 0.0, r), wallPoint(w, len, r), wallPoint(w, len, l), wallPoint(w, 0.0, l)};
    return f;
}

std::vector<WallFaces> joinWalls(const std::vector<Wall>& walls)
{
    std::vector<WallFaces> out;
    out.reserve(walls.size());
    for (const Wall& w : walls)
        out.push_back(wallFaces(w));

    for (size_t i = 0; i < walls.size(); ++i) {
        const Wall& wi = walls[i];
        if (degenerate(wi))
            continue;
        for (bool atStart : {true, false}) {
            const Vec2 p = endPoint(wi, atStart);
            const double tol = tolerance(wi);
            std::vector<std::pair<size_t, bool>> meeting;
            for (size_t j = 0; j < walls.size(); ++j) {
                if (j == i || degenerate(walls[j]))
                    continue;
                for (bool js : {true, false})
                    if (distance(endPoint(walls[j], js), p) <= tol)
                        meeting.push_back({j, js});
            }
            if (meeting.size() == 1) {
                joinCorner(wi, atStart, walls[meeting[0].first], meeting[0].second, out[i]);
                continue;
            }
            if (!meeting.empty())
                continue;  // three or more walls: left square
            for (size_t j = 0; j < walls.size(); ++j) {
                if (j != i && !degenerate(walls[j]) && insideWall(walls[j], p, tol)
                    && joinTee(wi, atStart, walls[j], out[i], out[j]))
                    break;
            }
        }
    }
    return out;
}

std::map<EntityId, WallFaces> joinedWalls(const Document& doc)
{
    std::map<StoryId, std::pair<std::vector<EntityId>, std::vector<Wall>>> byStory;
    for (const auto& [id, e] : doc.entities()) {
        if (const auto* w = std::get_if<Wall>(&e.shape)) {
            auto& [ids, walls] = byStory[e.story];
            ids.push_back(id);
            walls.push_back(*w);
        }
    }
    std::map<EntityId, WallFaces> out;
    for (const auto& [story, group] : byStory) {
        const std::vector<WallFaces> faces = joinWalls(group.second);
        for (size_t i = 0; i < faces.size(); ++i)
            out.emplace(group.first[i], faces[i]);
    }
    return out;
}

std::vector<Shape> wallPlan(const Wall& w, const WallFaces& f)
{
    std::vector<Shape> out;
    if (degenerate(w))
        return out;
    const double l = wallLeftOffset(w);
    const double r = wallRightOffset(w);
    const double len = wallLength(w);
    const auto openings = openingRanges(w);

    auto face = [&](double offset, Vec2 from, Vec2 to, std::vector<std::pair<double, double>> gaps) {
        gaps.insert(gaps.end(), openings.begin(), openings.end());
        for (const auto& [a, b] : subtract(wallParam(w, from), wallParam(w, to), gaps))
            out.push_back(Line{wallPoint(w, a, offset), wallPoint(w, b, offset)});
    };
    face(l, f.corners[3], f.corners[2], f.leftGaps);
    face(r, f.corners[0], f.corners[1], f.rightGaps);
    if (!f.startJoined)
        out.push_back(Line{f.corners[0], f.corners[3]});
    if (!f.endJoined)
        out.push_back(Line{f.corners[1], f.corners[2]});

    for (const Opening& o : w.openings) {
        const double t0 = std::clamp(openingStart(o), 0.0, len);
        const double t1 = std::clamp(openingEnd(o), 0.0, len);
        if (t1 - t0 <= kEps)
            continue;
        out.push_back(Line{wallPoint(w, t0, l), wallPoint(w, t0, r)});
        out.push_back(Line{wallPoint(w, t1, l), wallPoint(w, t1, r)});
        if (o.kind == Opening::Kind::Door) {
            const double hinge = o.hingeAtEnd ? t1 : t0;
            const double other = o.hingeAtEnd ? t0 : t1;
            const double side = o.swingRight ? r : l;
            const Vec2 out90 = o.swingRight ? -wallNormal(w) : wallNormal(w);
            const double width = t1 - t0;
            const Vec2 pivot = wallPoint(w, hinge, side);
            const Vec2 closed = wallPoint(w, other, side);
            out.push_back(Line{pivot, pivot + out90 * width});
            const double a = angleOf(out90);
            const double b = angleOf(closed - pivot);
            // Quarter circle from the open leaf to the closed position.
            if (cross(out90, closed - pivot) > 0.0)
                out.push_back(Arc{pivot, width, normalizeAngle(a), normalizeAngle(b)});
            else
                out.push_back(Arc{pivot, width, normalizeAngle(b), normalizeAngle(a)});
        } else {
            const double mid = (l + r) / 2.0;
            for (double off : {l, mid, r})
                out.push_back(Line{wallPoint(w, t0, off), wallPoint(w, t1, off)});
        }
    }
    return out;
}

Polyline wallOutline(const WallFaces& f)
{
    return Polyline{{f.corners.begin(), f.corners.end()}, true};
}

std::vector<std::vector<Vec2>> wallSolidParts(const Wall& w, const WallFaces& f)
{
    std::vector<std::vector<Vec2>> out;
    if (degenerate(w))
        return out;
    const std::vector<Vec2> footprint(f.corners.begin(), f.corners.end());
    for (const auto& [a, b] : subtract(-kHuge, kHuge, openingRanges(w))) {
        auto part = clipAlongWall(footprint, w, a, b);
        if (part.size() >= 3)
            out.push_back(std::move(part));
    }
    return out;
}

std::optional<RoofGeometry> roofGeometry(const Roof& r)
{
    // Corners without repeats; the edge indices of `gables` refer to these.
    std::vector<Vec2> pts = r.footprint.points;
    if (pts.size() >= 2 && almostEqual(pts.front(), pts.back(), 1e-9))
        pts.pop_back();
    if (pts.size() < 3)
        return std::nullopt;
    const size_t n = pts.size();
    const double area = polygonArea(pts);
    if (std::abs(area) < kEps)
        return std::nullopt;
    const double turn = area > 0.0 ? 1.0 : -1.0;
    double scale = 0.0;
    for (Vec2 p : pts)
        scale = std::max(scale, distance(p, pts[0]));
    for (size_t k = 0; k < n; ++k) {
        const Vec2 e1 = pts[(k + 1) % n] - pts[k];
        const Vec2 e2 = pts[(k + 2) % n] - pts[(k + 1) % n];
        if (length(e1) < kEps || turn * cross(e1, e2) < -1e-9 * scale * scale)
            return std::nullopt;  // repeated corner or not convex
    }

    RoofGeometry g;
    g.footprint = pts;
    std::vector<Vec2> inward(n);
    for (size_t i = 0; i < n; ++i)
        inward[i] = perpendicular(normalized(pts[(i + 1) % n] - pts[i])) * turn;
    for (size_t k = 0; k < n; ++k) {
        const size_t prev = (k + n - 1) % n;
        const FaceLine a{pts[prev] - inward[prev] * r.overhang, normalized(pts[k] - pts[prev])};
        const FaceLine b{pts[k] - inward[k] * r.overhang, normalized(pts[(k + 1) % n] - pts[k])};
        g.outline.push_back(intersectLines(a, b).value_or(pts[k] - inward[k] * r.overhang));
    }

    std::vector<size_t> sloped;
    for (size_t i = 0; i < n; ++i)
        if (!r.gable(i))
            sloped.push_back(i);
    if (sloped.empty()) {
        g.faces.push_back({g.outline, -1});
        return g;
    }
    // Each face is where its edge is the nearest of the sloping ones: with
    // one pitch for all, that is where its plane is the lowest.
    auto dist = [&](size_t i, Vec2 p) { return dot(p - pts[i], inward[i]); };
    for (size_t i : sloped) {
        std::vector<Vec2> face = g.outline;
        for (size_t j : sloped) {
            if (j == i || face.size() < 3)
                continue;
            // Two edges on the same line share one face: the first keeps it.
            const bool sameLine = dot(inward[i], inward[j]) > 1.0 - 1e-12 && std::abs(dist(j, pts[i])) < 1e-9 * scale;
            if (sameLine) {
                if (j < i)
                    face.clear();
                continue;
            }
            face = clip(face, [&](Vec2 p) { return dist(j, p) - dist(i, p); });
        }
        if (face.size() >= 3 && std::abs(polygonArea(face)) > 1e-12 * scale * scale)
            g.faces.push_back({face, static_cast<int>(i)});
    }
    return g;
}

double roofHeight(const Roof& r, Vec2 p)
{
    const auto g = roofGeometry(r);
    if (!g)
        return r.base;
    const size_t n = g->footprint.size();
    const double turn = polygonArea(g->footprint) > 0.0 ? 1.0 : -1.0;
    double best = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < n; ++i) {
        if (r.gable(i))
            continue;
        const Vec2 a = g->footprint[i];
        const Vec2 inward = perpendicular(normalized(g->footprint[(i + 1) % n] - a)) * turn;
        best = std::min(best, dot(p - a, inward));
    }
    if (!std::isfinite(best))
        return r.base;
    return r.base + std::tan(r.pitch) * best;
}

std::vector<Shape> roofPlan(const Roof& r)
{
    std::vector<Shape> out;
    const auto g = roofGeometry(r);
    if (!g) {
        // Not a valid roof: the footprint shows what is wrong.
        Polyline p = r.footprint;
        p.closed = true;
        out.push_back(p);
        return out;
    }
    out.push_back(Polyline{g->outline, true});
    // Ridges and hips: the face edges inside the outline, once each.
    const size_t n = g->outline.size();
    auto onOutline = [&](Vec2 p) {
        for (size_t k = 0; k < n; ++k)
            if (distanceToSegment(p, g->outline[k], g->outline[(k + 1) % n]) < 1e-6 * std::max(1.0, r.overhang + 1.0))
                return true;
        return false;
    };
    std::vector<Line> inner;
    for (const RoofFace& f : g->faces) {
        for (size_t k = 0; k < f.polygon.size(); ++k) {
            const Line l{f.polygon[k], f.polygon[(k + 1) % f.polygon.size()]};
            if (distance(l.a, l.b) < 1e-9 || onOutline(midpoint(l.a, l.b)))
                continue;
            const bool seen = std::any_of(inner.begin(), inner.end(), [&](const Line& o) {
                return (almostEqual(o.a, l.a, 1e-6) && almostEqual(o.b, l.b, 1e-6))
                    || (almostEqual(o.a, l.b, 1e-6) && almostEqual(o.b, l.a, 1e-6));
            });
            if (!seen)
                inner.push_back(l);
        }
    }
    for (const Line& l : inner)
        out.push_back(l);
    return out;
}

std::pair<Vec3, Vec3> Model3D::bounds() const
{
    const double inf = std::numeric_limits<double>::infinity();
    Vec3 lo{inf, inf, inf};
    Vec3 hi{-inf, -inf, -inf};
    for (const Triangle& t : triangles) {
        for (const Vec3& v : t.v) {
            lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
            hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
        }
    }
    return {lo, hi};
}

Model3D buildingModel(const std::vector<Wall>& walls)
{
    Model3D model;
    ModelBuilder builder(model);
    const std::vector<WallFaces> faces = joinWalls(walls);
    for (size_t i = 0; i < walls.size(); ++i)
        builder.wall(walls[i], faces[i]);
    return model;
}

} // namespace cad

namespace cad {

Model3D buildingModel(const Document& doc)
{
    Model3D model;
    for (const Story& story : doc.stories()) {
        std::vector<Wall> walls;
        Model3D part;
        ModelBuilder builder(part);
        for (const auto& [id, e] : doc.entities()) {
            if (e.story != story.id || !doc.isLayerVisible(e))
                continue;
            if (const auto* w = std::get_if<Wall>(&e.shape))
                walls.push_back(*w);
            else if (const auto* sl = std::get_if<Slab>(&e.shape))
                builder.slab(*sl);
            else if (const auto* r = std::get_if<Roof>(&e.shape))
                builder.roof(*r);
        }
        const std::vector<WallFaces> faces = joinWalls(walls);
        for (size_t i = 0; i < walls.size(); ++i)
            builder.wall(walls[i], faces[i]);
        // Raised to the story.
        const Vec3 up{0.0, 0.0, story.elevation};
        for (auto& t : part.triangles) {
            for (Vec3& v : t.v)
                v = v + up;
            model.triangles.push_back(t);
        }
        for (auto& e : part.edges)
            model.edges.push_back({e[0] + up, e[1] + up});
    }
    return model;
}

} // namespace cad
