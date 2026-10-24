// SPDX-License-Identifier: GPL-3.0-or-later
#include "building.h"

#include <algorithm>
#include <limits>

namespace cad {

namespace {

constexpr double kHuge = std::numeric_limits<double>::max() / 4.0;

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
std::vector<Vec2> clipAlongWall(std::vector<Vec2> poly, const Wall& w, double tmin, double tmax)
{
    auto clip = [&](const std::vector<Vec2>& in, auto inside) {
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
    };
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

    void polygon(const std::vector<Vec2>& pts, double z)
    {
        for (size_t k = 1; k + 1 < pts.size(); ++k)
            m_.triangles.push_back({{Vec3(pts[0], z), Vec3(pts[k], z), Vec3(pts[k + 1], z)}, Model3D::Material::Wall});
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
    std::vector<EntityId> ids;
    std::vector<Wall> walls;
    for (const auto& [id, e] : doc.entities()) {
        if (const auto* w = std::get_if<Wall>(&e.shape)) {
            ids.push_back(id);
            walls.push_back(*w);
        }
    }
    const std::vector<WallFaces> faces = joinWalls(walls);
    std::map<EntityId, WallFaces> out;
    for (size_t i = 0; i < ids.size(); ++i)
        out.emplace(ids[i], faces[i]);
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
    std::vector<Wall> walls;
    for (const auto& [id, e] : doc.entities())
        if (const auto* w = std::get_if<Wall>(&e.shape); w && doc.isVisible(e))
            walls.push_back(*w);
    return buildingModel(walls);
}

} // namespace cad
