// SPDX-License-Identifier: GPL-3.0-or-later
#include "building.h"

#include "hatch.h"

#include <algorithm>
#include <functional>
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
    // `walls`: those of the story, so a gable end is as thick as the wall under it.
    void roof(const Roof& r, const std::vector<Wall>& walls = {});
    void stair(const Stair& s);

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

namespace {

// The thickness of the wall whose outer face runs along the footprint edge
// a-b (`inward` points into the building), or 0 if no wall does.
double gableThickness(Vec2 a, Vec2 b, Vec2 inward, const std::vector<Wall>& walls)
{
    const double len = distance(a, b);
    const Vec2 d = (b - a) * (1.0 / len);
    double best = 0.0;
    for (const Wall& w : walls) {
        const double wl = wallLength(w);
        if (wl < kEps || std::abs(cross(normalized(w.end - w.start), d)) > 1e-3)
            continue;
        // Both faces of the wall, as distances inwards from the edge.
        const Vec2 n = wallNormal(w);
        const double axis = dot(w.start - a, inward);
        const double side = dot(n, inward);
        const double f0 = axis + side * wallLeftOffset(w);
        const double f1 = axis + side * wallRightOffset(w);
        const double outer = std::min(f0, f1);
        if (std::abs(outer) > std::max(1.0, 0.05 * w.thickness))
            continue;
        // It must run along the edge, not just on its line.
        const double s0 = dot(w.start - a, d), s1 = dot(w.end - a, d);
        if (std::max(s0, s1) < 0.1 * len || std::min(s0, s1) > 0.9 * len)
            continue;
        best = std::max(best, std::abs(f1 - f0));
    }
    return best;
}

} // namespace

void ModelBuilder::roof(const Roof& r, const std::vector<Wall>& walls)
{
    const auto g = roofGeometry(r);
    if (!g || r.thickness <= kEps)
        return;
    const bool flat = g->flat();
    const double rise = flat ? r.thickness : r.thickness / std::cos(r.pitch);
    auto under = [&](Vec2 p) { return g->height(p); };

    for (const RoofFace& f : g->faces) {
        std::vector<Vec3> bottom, top;
        for (Vec2 v : f.polygon) {
            bottom.push_back(Vec3(v, f.plane.at(v)));
            top.push_back(Vec3(v, f.plane.at(v) + rise));
        }
        for (size_t k = 1; k + 1 < top.size(); ++k) {
            m_.triangles.push_back({{bottom[0], bottom[k], bottom[k + 1]}, Model3D::Material::Roof});
            m_.triangles.push_back({{top[0], top[k], top[k + 1]}, Model3D::Material::Roof});
        }
    }
    for (const auto& l : g->lines)
        edge(Vec3(l[0], under(l[0]) + rise), Vec3(l[1], under(l[1]) + rise));

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
            edge(Vec3(p, under(p) + rise), Vec3(q, under(q) + rise));
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
        if (distance(a, b) < kEps)
            continue;
        const Vec2 inward = normalized(perpendicular(b - a)) * (polygonArea(g->footprint) > 0.0 ? 1.0 : -1.0);
        const double thickness = gableThickness(a, b, inward, walls);
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
            if (f.plane.at(p) - r.base < kEps && f.plane.at(q) - r.base < kEps)
                continue;
            if (thickness <= kEps) {
                quad(Vec3(p, r.base), Vec3(q, r.base), Vec3(q, f.plane.at(q)), Vec3(p, f.plane.at(p)));
                continue;
            }
            // A solid as thick as the wall under it, so a section cuts it full.
            const Vec2 pi = p + inward * thickness;
            const Vec2 qi = q + inward * thickness;
            const Vec3 o[4] = {Vec3(p, r.base), Vec3(q, r.base), Vec3(q, std::max(r.base, f.plane.at(q))), Vec3(p, std::max(r.base, f.plane.at(p)))};
            const Vec3 in[4] = {Vec3(pi, r.base), Vec3(qi, r.base), Vec3(qi, std::max(r.base, f.plane.at(qi))),
                                Vec3(pi, std::max(r.base, f.plane.at(pi)))};
            quad(o[0], o[1], o[2], o[3]);
            quad(in[0], in[1], in[2], in[3]);
            for (int k = 0; k < 4; ++k) {
                const int n2 = (k + 1) % 4;
                quad(o[k], o[n2], in[n2], in[k]);
            }
        }
    }
}

// Each step is a solid block from the floor of the story to its tread. The
// edges are those of the whole stair: the steps' sides and the floor lines
// they share are left out.
void ModelBuilder::stair(const Stair& s)
{
    const auto steps = stairSteps(s);
    const double tol = 1e-6 * std::max({s.width, s.going, 1.0});
    auto touches = [&](const StairStep& step, Vec2 p) {
        const auto& q = step.polygon;
        for (size_t k = 0; k < q.size(); ++k) {
            const Vec2 d = q[(k + 1) % q.size()] - q[k];
            if (cross(d, p - q[k]) / length(d) < -tol)
                return false;
        }
        return true;
    };
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& p = steps[i].polygon;
        const double top = steps[i].top;
        polygon(p, top);
        polygon(p, 0.0);
        for (size_t k = 0; k < p.size(); ++k) {
            const Vec2 a = p[k];
            const Vec2 b = p[(k + 1) % p.size()];
            quad(Vec3(a, 0.0), Vec3(b, 0.0), Vec3(b, top), Vec3(a, top));
            edge(Vec3(a, top), Vec3(b, top));
            // Down from the step below, or from the floor; nothing where a
            // higher step stands on that corner.
            double from = 0.0;
            bool hidden = false;
            bool shared = false;
            for (size_t j = 0; j < steps.size(); ++j) {
                if (j == i)
                    continue;
                if (touches(steps[j], a)) {
                    if (steps[j].top > top)
                        hidden = true;
                    else
                        from = std::max(from, steps[j].top);
                }
                shared = shared || touches(steps[j], midpoint(a, b));
            }
            if (!hidden)
                edge(Vec3(a, from), Vec3(a, top));
            if (!shared)
                edge(Vec3(a, 0.0), Vec3(b, 0.0));
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

namespace {

// Corners of a footprint without repeats, counter-clockwise. `flipped` tells
// whether they were reversed, so edge i of the result is edge n - 2 - i of
// the input (mod n).
std::vector<Vec2> roofCorners(const Polyline& footprint)
{
    std::vector<Vec2> pts;
    for (Vec2 p : footprint.points)
        if (pts.empty() || !almostEqual(p, pts.back(), 1e-9))
            pts.push_back(p);
    if (pts.size() >= 2 && almostEqual(pts.front(), pts.back(), 1e-9))
        pts.pop_back();
    return pts;
}

double extent(const std::vector<Vec2>& pts)
{
    double scale = 0.0;
    for (Vec2 p : pts)
        scale = std::max(scale, distance(p, pts[0]));
    return std::max(scale, 1e-9);
}

bool isConvex(const std::vector<Vec2>& pts, double turn, double scale)
{
    const size_t n = pts.size();
    for (size_t k = 0; k < n; ++k) {
        const Vec2 e1 = pts[(k + 1) % n] - pts[k];
        const Vec2 e2 = pts[(k + 2) % n] - pts[(k + 1) % n];
        if (turn * cross(e1, e2) < -1e-9 * scale * scale)
            return false;
    }
    return true;
}

bool insidePolygon(const std::vector<Vec2>& poly, Vec2 p)
{
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2 a = poly[i];
        const Vec2 b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (b.x - a.x) * (p.y - a.y) / (b.y - a.y))
            in = !in;
    }
    return in;
}

// Inside a counter-clockwise convex polygon, give or take `tol`.
bool insideConvex(const std::vector<Vec2>& poly, Vec2 p, double tol)
{
    for (size_t k = 0; k < poly.size(); ++k) {
        const Vec2 a = poly[k];
        const Vec2 d = poly[(k + 1) % poly.size()] - a;
        const double len = length(d);
        if (len > 0.0 && cross(d, p - a) / len < -tol)
            return false;
    }
    return true;
}

// The polygon grown by `d` on every side, with mitred corners.
std::vector<Vec2> grown(const std::vector<Vec2>& pts, double turn, double d)
{
    const size_t n = pts.size();
    std::vector<Vec2> out;
    for (size_t k = 0; k < n; ++k) {
        const size_t prev = (k + n - 1) % n;
        const Vec2 dp = normalized(pts[k] - pts[prev]);
        const Vec2 dk = normalized(pts[(k + 1) % n] - pts[k]);
        const FaceLine a{pts[prev] - perpendicular(dp) * turn * d, dp};
        const FaceLine b{pts[k] - perpendicular(dk) * turn * d, dk};
        out.push_back(intersectLines(a, b).value_or(pts[k] - perpendicular(dk) * turn * d));
    }
    return out;
}

bool samePlane(const RoofPlane& a, const RoofPlane& b, double scale)
{
    return length(a.grad - b.grad) < 1e-9 && std::abs(a.c - b.c) < 1e-6 * scale;
}

// A convex roof over counter-clockwise corners.
RoofPiece makePiece(std::vector<Vec2> pts, std::vector<bool> gables, const Roof& r)
{
    RoofPiece piece;
    const size_t n = pts.size();
    const double scale = extent(pts);
    const double slope = std::tan(r.pitch);
    piece.outline = grown(pts, 1.0, r.overhang);
    for (size_t i = 0; i < n; ++i) {
        if (i < gables.size() && gables[i])
            continue;
        const Vec2 inward = perpendicular(normalized(pts[(i + 1) % n] - pts[i]));
        const RoofPlane plane{inward * slope, r.base - slope * dot(pts[i], inward)};
        // Two edges on the same line share one plane.
        if (std::none_of(piece.planes.begin(), piece.planes.end(), [&](const RoofPlane& q) { return samePlane(q, plane, scale); })) {
            piece.planes.push_back(plane);
            piece.edges.push_back(static_cast<int>(i));
        }
    }
    piece.footprint = std::move(pts);
    piece.gables = std::move(gables);
    return piece;
}

// The direction of the longest edge.
Vec2 mainDirection(const std::vector<Vec2>& pts)
{
    size_t longest = 0;
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i)
        if (distance(pts[i], pts[(i + 1) % n]) > distance(pts[longest], pts[(longest + 1) % n]))
            longest = i;
    return normalized(pts[(longest + 1) % n] - pts[longest]);
}

// The largest rectangles inside a polygon whose edges all meet at right
// angles (counter-clockwise, in the frame of its longest edge). Empty when
// the polygon has other angles.
std::vector<std::vector<Vec2>> largestRectangles(const std::vector<Vec2>& pts)
{
    const Vec2 u = mainDirection(pts);
    const Vec2 v = perpendicular(u);
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        const Vec2 d = normalized(pts[(i + 1) % n] - pts[i]);
        if (std::abs(cross(d, u)) > 1e-9 && std::abs(dot(d, u)) > 1e-9)
            return {};
    }
    const double tol = 1e-9 * extent(pts);
    std::vector<Vec2> local;
    std::vector<double> xs, ys;
    for (Vec2 p : pts) {
        local.push_back({dot(p, u), dot(p, v)});
        xs.push_back(local.back().x);
        ys.push_back(local.back().y);
    }
    auto unique = [&](std::vector<double>& c) {
        std::sort(c.begin(), c.end());
        c.erase(std::unique(c.begin(), c.end(), [&](double a, double b) { return b - a < tol; }), c.end());
    };
    unique(xs);
    unique(ys);
    const size_t nx = xs.size() - 1;
    const size_t ny = ys.size() - 1;
    // Cells inside, with sums for quick checks of whole blocks.
    std::vector<int> sum((nx + 1) * (ny + 1), 0);
    auto at = [&](size_t i, size_t j) -> int& { return sum[i * (ny + 1) + j]; };
    for (size_t i = 0; i < nx; ++i)
        for (size_t j = 0; j < ny; ++j) {
            const bool in = insidePolygon(local, {(xs[i] + xs[i + 1]) / 2.0, (ys[j] + ys[j + 1]) / 2.0});
            at(i + 1, j + 1) = (in ? 1 : 0) + at(i, j + 1) + at(i + 1, j) - at(i, j);
        }
    // Cells i0..i1-1 by j0..j1-1 all inside.
    auto full = [&](size_t i0, size_t i1, size_t j0, size_t j1) {
        return at(i1, j1) - at(i0, j1) - at(i1, j0) + at(i0, j0) == static_cast<int>((i1 - i0) * (j1 - j0));
    };
    std::vector<std::vector<Vec2>> out;
    for (size_t i0 = 0; i0 < nx; ++i0)
        for (size_t i1 = i0 + 1; i1 <= nx; ++i1)
            for (size_t j0 = 0; j0 < ny; ++j0)
                for (size_t j1 = j0 + 1; j1 <= ny; ++j1) {
                    if (!full(i0, i1, j0, j1))
                        continue;
                    if ((i0 > 0 && full(i0 - 1, i1, j0, j1)) || (i1 < nx && full(i0, i1 + 1, j0, j1))
                        || (j0 > 0 && full(i0, i1, j0 - 1, j1)) || (j1 < ny && full(i0, i1, j0, j1 + 1)))
                        continue;
                    auto P = [&](double x, double y) { return u * x + v * y; };
                    out.push_back({P(xs[i0], ys[j0]), P(xs[i1], ys[j0]), P(xs[i1], ys[j1]), P(xs[i0], ys[j1])});
                }
    return out;
}

// Length along a-b covered by footprint edge c-d lying on the same line.
double overlap(Vec2 a, Vec2 b, Vec2 c, Vec2 d, double tol)
{
    const Vec2 dir = normalized(b - a);
    if (std::abs(cross(dir, normalized(d - c))) > 1e-9 || std::abs(cross(dir, c - a)) > tol)
        return 0.0;
    const double len = distance(a, b);
    const double t0 = std::min(dot(c - a, dir), dot(d - a, dir));
    const double t1 = std::max(dot(c - a, dir), dot(d - a, dir));
    return std::min(len, t1) - std::max(0.0, t0);
}

// The parts of convex `poly` outside the convex region where no function is
// negative. Being outside takes more than `eps`, so where a function is zero
// all over (two faces on one plane) the region keeps the polygon.
std::vector<std::vector<Vec2>> subtractRegion(const std::vector<Vec2>& poly,
                                              const std::vector<std::function<double(Vec2)>>& region, double minArea,
                                              double eps)
{
    std::vector<Vec2> inside = poly;
    for (const auto& h : region)
        if (inside.size() >= 3)
            inside = clip(inside, h);
    if (inside.size() < 3 || std::abs(polygonArea(inside)) < minArea)
        return {poly};
    std::vector<std::vector<Vec2>> out;
    std::vector<Vec2> rest = poly;
    for (const auto& h : region) {
        auto outside = clip(rest, [&](Vec2 p) { return -h(p) - eps; });
        if (outside.size() >= 3 && std::abs(polygonArea(outside)) >= minArea)
            out.push_back(std::move(outside));
        rest = clip(rest, h);
        if (rest.size() < 3)
            break;
    }
    return out;
}

// Joins segments that continue each other on one line.
std::vector<std::array<Vec2, 2>> mergeSegments(std::vector<std::array<Vec2, 2>> lines, double tol)
{
    for (bool merged = true; merged;) {
        merged = false;
        for (size_t i = 0; i < lines.size() && !merged; ++i)
            for (size_t j = i + 1; j < lines.size() && !merged; ++j) {
                auto& a = lines[i];
                const auto& b = lines[j];
                const Vec2 da = normalized(a[1] - a[0]);
                if (std::abs(cross(da, normalized(b[1] - b[0]))) > 1e-9)
                    continue;
                for (int ea = 0; ea < 2 && !merged; ++ea)
                    for (int eb = 0; eb < 2 && !merged; ++eb)
                        if (almostEqual(a[ea], b[eb], tol)) {
                            a = {a[1 - ea], b[1 - eb]};
                            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(j));
                            merged = true;
                        }
            }
    }
    return lines;
}

} // namespace

double RoofPiece::height(Vec2 p, double base) const
{
    if (planes.empty())
        return base;
    double z = std::numeric_limits<double>::infinity();
    for (const RoofPlane& q : planes)
        z = std::min(z, q.at(p));
    return z;
}

bool RoofPiece::contains(Vec2 p) const { return insideConvex(outline, p, 1e-6 * extent(outline)); }

double RoofGeometry::height(Vec2 p) const
{
    double best = -std::numeric_limits<double>::infinity();
    double any = -std::numeric_limits<double>::infinity();
    for (const RoofPiece& piece : pieces) {
        const double z = piece.height(p, base);
        any = std::max(any, z);
        if (piece.contains(p))
            best = std::max(best, z);
    }
    if (std::isfinite(best))
        return best;
    return std::isfinite(any) ? any : base;
}

bool RoofGeometry::flat() const
{
    return std::all_of(pieces.begin(), pieces.end(), [](const RoofPiece& p) { return p.planes.empty(); });
}

std::optional<RoofGeometry> roofGeometry(const Roof& r)
{
    std::vector<Vec2> pts = roofCorners(r.footprint);
    if (pts.size() < 3)
        return std::nullopt;
    const size_t n = pts.size();
    const double area = polygonArea(pts);
    const double scale = extent(pts);
    if (std::abs(area) < 1e-9 * scale * scale)
        return std::nullopt;
    // The pieces are built counter-clockwise; the edges keep their indices.
    std::vector<Vec2> ccw = pts;
    std::vector<bool> gables(n);
    for (size_t i = 0; i < n; ++i)
        gables[i] = r.gable(i);
    if (area < 0.0) {
        std::reverse(ccw.begin(), ccw.end());
        // Edge i of the reversed corners is edge n - 2 - i of the original.
        std::vector<bool> g(n);
        for (size_t i = 0; i < n; ++i)
            g[i] = gables[(2 * n - 2 - i) % n];
        gables = g;
    }

    RoofGeometry g;
    g.footprint = pts;
    g.base = r.base;
    g.outline = grown(pts, area > 0.0 ? 1.0 : -1.0, r.overhang);
    const double tol = 1e-6 * scale;
    if (isConvex(ccw, 1.0, scale)) {
        g.pieces.push_back(makePiece(ccw, gables, r));
    } else {
        const auto rects = largestRectangles(ccw);
        if (rects.empty())
            return std::nullopt;
        for (const auto& rect : rects) {
            // A side is a gable end when the footprint edges it lies on are.
            std::vector<bool> sides(4, false);
            for (size_t s = 0; s < 4; ++s) {
                bool covered = false;
                bool allGables = true;
                for (size_t k = 0; k < n; ++k) {
                    if (overlap(rect[s], rect[(s + 1) % 4], ccw[k], ccw[(k + 1) % n], tol) > tol) {
                        covered = true;
                        allGables = allGables && gables[k];
                    }
                }
                sides[s] = covered && allGables;
            }
            g.pieces.push_back(makePiece(rect, sides, r));
        }
    }

    // Planes shared by several pieces become exactly the same, so ties are
    // exact: the first piece keeps the face.
    std::vector<RoofPlane> shared;
    for (RoofPiece& piece : g.pieces)
        for (RoofPlane& q : piece.planes) {
            const auto same = std::find_if(shared.begin(), shared.end(), [&](const RoofPlane& o) { return samePlane(o, q, scale); });
            if (same != shared.end())
                q = *same;
            else
                shared.push_back(q);
        }

    // Each piece's faces, less where another piece is higher.
    const double minArea = 1e-8 * scale * scale;
    for (size_t a = 0; a < g.pieces.size(); ++a) {
        const RoofPiece& pa = g.pieces[a];
        std::vector<RoofFace> faces;
        if (pa.planes.empty()) {
            faces.push_back({pa.outline, -1, static_cast<int>(a), RoofPlane{{0, 0}, r.base}});
        } else {
            for (size_t i = 0; i < pa.planes.size(); ++i) {
                std::vector<Vec2> face = pa.outline;
                for (size_t j = 0; j < pa.planes.size() && face.size() >= 3; ++j)
                    if (j != i)
                        face = clip(face, [&](Vec2 p) { return pa.planes[j].at(p) - pa.planes[i].at(p); });
                if (face.size() >= 3 && std::abs(polygonArea(face)) > minArea)
                    faces.push_back({face, pa.edges[i], static_cast<int>(a), pa.planes[i]});
            }
        }
        for (const RoofFace& f : faces) {
            std::vector<std::vector<Vec2>> parts{f.polygon};
            for (size_t b = 0; b < g.pieces.size(); ++b) {
                if (b == a)
                    continue;
                const RoofPiece& pb = g.pieces[b];
                std::vector<std::function<double(Vec2)>> region;
                const auto& o = pb.outline;
                for (size_t k = 0; k < o.size(); ++k) {
                    const Vec2 p0 = o[k];
                    const Vec2 d = normalized(o[(k + 1) % o.size()] - p0);
                    region.push_back([p0, d](Vec2 p) { return cross(d, p - p0); });
                }
                const double bias = b < a ? 0.0 : -1e-12 * scale;
                const RoofPlane own = f.plane;
                if (pb.planes.empty())
                    region.push_back([&, own, bias](Vec2 p) { return r.base - own.at(p) + bias; });
                for (const RoofPlane& q : pb.planes)
                    region.push_back([q, own, bias](Vec2 p) { return q.at(p) - own.at(p) + bias; });
                std::vector<std::vector<Vec2>> next;
                for (const auto& part : parts)
                    for (auto& rest : subtractRegion(part, region, minArea, 1e-12 * scale))
                        next.push_back(std::move(rest));
                parts = std::move(next);
            }
            for (auto& part : parts)
                g.faces.push_back({std::move(part), f.edge, f.piece, f.plane});
        }
    }

    // Ridges, hips and valleys: the face edges with a face on another plane
    // beyond them. Edges are split where other faces have corners, so each
    // piece has one neighbour.
    std::vector<std::array<Vec2, 2>> lines;
    for (size_t fi = 0; fi < g.faces.size(); ++fi) {
        const auto& poly = g.faces[fi].polygon;
        for (size_t k = 0; k < poly.size(); ++k) {
            const Vec2 a = poly[k];
            const Vec2 b = poly[(k + 1) % poly.size()];
            const double len = distance(a, b);
            if (len < tol)
                continue;
            std::vector<double> cuts{0.0, 1.0};
            for (const RoofFace& other : g.faces)
                for (Vec2 v : other.polygon)
                    if (distanceToSegment(v, a, b) < tol)
                        cuts.push_back(std::clamp(dot(v - a, b - a) / (len * len), 0.0, 1.0));
            std::sort(cuts.begin(), cuts.end());
            for (size_t c = 0; c + 1 < cuts.size(); ++c) {
                if ((cuts[c + 1] - cuts[c]) * len < tol)
                    continue;
                const Vec2 p = a + (b - a) * cuts[c];
                const Vec2 q = a + (b - a) * cuts[c + 1];
                const Vec2 outward = -perpendicular((b - a) / len);
                const Vec2 probe = midpoint(p, q) + outward * (1e-4 * scale);
                for (size_t gi = 0; gi < g.faces.size(); ++gi) {
                    if (gi == fi || !insideConvex(g.faces[gi].polygon, probe, 0.0))
                        continue;
                    const bool seen = std::any_of(lines.begin(), lines.end(), [&](const auto& l) {
                        return (almostEqual(l[0], p, tol) && almostEqual(l[1], q, tol))
                            || (almostEqual(l[0], q, tol) && almostEqual(l[1], p, tol));
                    });
                    if (!seen && !samePlane(g.faces[gi].plane, g.faces[fi].plane, scale))
                        lines.push_back({p, q});
                    break;
                }
            }
        }
    }
    g.lines = mergeSegments(std::move(lines), tol);
    return g;
}

double roofHeight(const Roof& r, Vec2 p)
{
    const auto g = roofGeometry(r);
    return g ? g->height(p) : r.base;
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
    for (const auto& l : g->lines)
        out.push_back(Line{l[0], l[1]});
    return out;
}

namespace {

// The layout of a stair in its own frame: x along the first flight, y to the
// side it turns to, the origin in the middle of the first step's front edge.
struct StairLayout {
    struct Rect {
        double x0, x1, y0, y1;
        int level;  // risers climbed to reach it
    };
    std::vector<Rect> steps;
    std::vector<Vec2> walk;  // walking line
};

StairLayout stairLayout(const Stair& s)
{
    StairLayout out;
    const int n = std::max(s.risers, 2);
    const double g = s.going;
    const double w = s.width;
    if (s.kind == Stair::Kind::Straight) {
        for (int k = 1; k < n; ++k)
            out.steps.push_back({(k - 1) * g, k * g, -w / 2.0, w / 2.0, k});
        out.walk = {{0, 0}, {(n - 1) * g, 0}};
        return out;
    }
    // Half the risers before the landing.
    const int n1 = n / 2;
    const int n2 = n - n1;
    for (int k = 1; k < n1; ++k)
        out.steps.push_back({(k - 1) * g, k * g, -w / 2.0, w / 2.0, k});
    const double x1 = (n1 - 1) * g;
    if (s.kind == Stair::Kind::L) {
        out.steps.push_back({x1, x1 + w, -w / 2.0, w / 2.0, n1});
        for (int j = 1; j < n2; ++j)
            out.steps.push_back({x1, x1 + w, w / 2.0 + (j - 1) * g, w / 2.0 + j * g, n1 + j});
        out.walk = {{0, 0}, {x1 + w / 2.0, 0}, {x1 + w / 2.0, w / 2.0 + (n2 - 1) * g}};
    } else {
        out.steps.push_back({x1, x1 + w, -w / 2.0, 1.5 * w, n1});
        for (int j = 1; j < n2; ++j)
            out.steps.push_back({x1 - j * g, x1 - (j - 1) * g, w / 2.0, 1.5 * w, n1 + j});
        out.walk = {{0, 0}, {x1 + w / 2.0, 0}, {x1 + w / 2.0, w}, {x1 - (n2 - 1) * g, w}};
    }
    return out;
}

} // namespace

std::vector<StairStep> stairSteps(const Stair& s)
{
    const Vec2 u = polar(1.0, s.angle);
    const Vec2 v = perpendicular(u) * (s.turnRight ? -1.0 : 1.0);
    auto P = [&](double x, double y) { return s.start + u * x + v * y; };
    const double rise = s.height / std::max(s.risers, 2);
    std::vector<StairStep> out;
    for (const auto& r : stairLayout(s).steps) {
        std::vector<Vec2> poly{P(r.x0, r.y0), P(r.x1, r.y0), P(r.x1, r.y1), P(r.x0, r.y1)};
        if (s.turnRight)
            std::reverse(poly.begin(), poly.end());
        out.push_back({std::move(poly), r.level * rise});
    }
    return out;
}

std::vector<Shape> stairPlan(const Stair& s)
{
    std::vector<Shape> out;
    for (const StairStep& step : stairSteps(s))
        out.push_back(Polyline{step.polygon, true});
    const Vec2 u = polar(1.0, s.angle);
    const Vec2 v = perpendicular(u) * (s.turnRight ? -1.0 : 1.0);
    std::vector<Vec2> walk;
    for (Vec2 p : stairLayout(s).walk)
        walk.push_back(s.start + u * p.x + v * p.y);
    out.push_back(Polyline{walk, false});
    // A dot where it starts, an arrow where it arrives.
    const double size = std::min(s.width / 4.0, 1.5 * s.going);
    out.push_back(Circle{walk.front(), size / 4.0});
    const Vec2 end = walk.back();
    const Vec2 back = normalized(walk[walk.size() - 2] - end);
    const double a = angleOf(back);
    out.push_back(Line{end, end + polar(size, a + degToRad(25.0))});
    out.push_back(Line{end, end + polar(size, a - degToRad(25.0))});
    return out;
}

std::vector<bool> roofGables(const std::vector<Vec2>& footprint, RoofStyle style)
{
    const size_t n = footprint.size();
    std::vector<bool> gables(n, style == RoofStyle::Flat);
    if (n < 3 || style == RoofStyle::Hip || style == RoofStyle::Flat)
        return gables;
    size_t longest = 0;
    for (size_t i = 0; i < n; ++i)
        if (distance(footprint[i], footprint[(i + 1) % n]) > distance(footprint[longest], footprint[(longest + 1) % n]))
            longest = i;
    if (style == RoofStyle::Shed) {
        for (size_t i = 0; i < n; ++i)
            gables[i] = i != longest;
        return gables;
    }
    std::vector<Vec2> ccw = footprint;
    const double area = polygonArea(ccw);
    if (area < 0.0)
        std::reverse(ccw.begin(), ccw.end());
    const double scale = extent(ccw);
    const Vec2 dir = mainDirection(ccw);
    if (isConvex(ccw, 1.0, scale)) {
        // Slopes on the longest edge and those parallel to it.
        for (size_t i = 0; i < n; ++i) {
            const Vec2 d = normalized(footprint[(i + 1) % n] - footprint[i]);
            gables[i] = std::abs(cross(d, dir)) > 1e-6;
        }
        return gables;
    }
    // Each wing slopes on its long sides: the ends of the wings are gables.
    const double tol = 1e-6 * scale;
    std::vector<bool> shortSide(n, false), longSide(n, false);
    for (const auto& rect : largestRectangles(ccw)) {
        const double w = distance(rect[0], rect[1]);
        const double h = distance(rect[1], rect[2]);
        // The rectangles follow the longest edge: sides 0 and 2 run along it.
        const bool alongIsLong = w >= h - tol;
        for (size_t s = 0; s < 4; ++s) {
            const bool isLong = (s % 2 == 0) == alongIsLong;
            for (size_t k = 0; k < n; ++k)
                if (overlap(rect[s], rect[(s + 1) % 4], ccw[k], ccw[(k + 1) % n], tol) > tol)
                    (isLong ? longSide : shortSide)[k] = true;
        }
    }
    for (size_t k = 0; k < n; ++k) {
        const size_t original = area < 0.0 ? (2 * n - 2 - k) % n : k;
        gables[original] = shortSide[k] && !longSide[k];
    }
    return gables;
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
        std::vector<Roof> roofs;
        Model3D part;
        ModelBuilder builder(part);
        for (const auto& [id, e] : doc.entities()) {
            if (e.story != story.id || !doc.isLayerVisible(e))
                continue;
            // Building elements in blocks are built too.
            doc.forEachPart(e, [&](const Entity& p) {
                if (!doc.isLayerVisible(p))
                    return;
                if (const auto* w = std::get_if<Wall>(&p.shape))
                    walls.push_back(*w);
                else if (const auto* sl = std::get_if<Slab>(&p.shape))
                    builder.slab(*sl);
                else if (const auto* r = std::get_if<Roof>(&p.shape))
                    roofs.push_back(*r);
                else if (const auto* st = std::get_if<Stair>(&p.shape))
                    builder.stair(*st);
            });
        }
        const std::vector<WallFaces> faces = joinWalls(walls);
        for (size_t i = 0; i < walls.size(); ++i)
            builder.wall(walls[i], faces[i]);
        for (const Roof& r : roofs)
            builder.roof(r, walls);
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
