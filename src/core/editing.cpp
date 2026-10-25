// SPDX-License-Identifier: GPL-3.0-or-later
#include "editing.h"

#include "building.h"

#include <algorithm>

namespace cad {

namespace {

// Parameters closer than this to a curve's end do not cut it.
constexpr double kParamEps = 1e-9;

Vec2 lerp(Vec2 a, Vec2 b, double t)
{
    return a + (b - a) * t;
}

std::optional<Vec2> intersectLines(Vec2 p1, Vec2 d1, Vec2 p2, Vec2 d2)
{
    const double den = cross(d1, d2);
    if (std::abs(den) <= 1e-12 * length(d1) * length(d2))
        return std::nullopt;
    return p1 + d1 * (cross(p2 - p1, d2) / den);
}

// A line or circle (the carrier of an arc) for the fillet construction.
struct Carrier {
    bool isLine = true;
    Vec2 a, dir;           // line: point and unit direction
    Vec2 center;           // circle
    double radius = 0.0;

    Vec2 foot(Vec2 p) const
    {
        if (isLine)
            return a + dir * dot(p - a, dir);
        Vec2 u = normalized(p - center);
        if (length(u) < kEps)
            u = {1.0, 0.0};
        return center + u * radius;
    }
};

std::optional<Carrier> carrierOf(const Shape& s)
{
    Carrier c;
    if (const auto* l = std::get_if<Line>(&s)) {
        c.a = l->a;
        c.dir = normalized(l->b - l->a);
        if (length(c.dir) < kEps)
            return std::nullopt;
        return c;
    }
    c.isLine = false;
    if (const auto* a = std::get_if<Arc>(&s)) {
        c.center = a->center;
        c.radius = a->radius;
        return c;
    }
    if (const auto* ci = std::get_if<Circle>(&s)) {
        c.center = ci->center;
        c.radius = ci->radius;
        return c;
    }
    return std::nullopt;
}

// The curves at distance r from a carrier: two parallels or two circles.
std::vector<Carrier> offsetsOf(const Carrier& c, double r)
{
    std::vector<Carrier> out;
    if (c.isLine) {
        for (double s : {1.0, -1.0}) {
            Carrier o = c;
            o.a = c.a + perpendicular(c.dir) * (s * r);
            out.push_back(o);
            if (r <= kEps)
                break;
        }
        return out;
    }
    for (double rr : {c.radius + r, std::abs(c.radius - r)}) {
        if (rr <= kEps || (!out.empty() && std::abs(out.back().radius - rr) <= kEps))
            continue;
        Carrier o = c;
        o.radius = rr;
        out.push_back(o);
    }
    return out;
}

std::vector<Vec2> intersectCarriers(const Carrier& p, const Carrier& q)
{
    if (p.isLine && q.isLine) {
        if (const auto v = intersectLines(p.a, p.dir, q.a, q.dir))
            return {*v};
        return {};
    }
    if (!p.isLine && !q.isLine)
        return intersectCircles(p.center, p.radius, q.center, q.radius);
    const Carrier& l = p.isLine ? p : q;
    const Carrier& c = p.isLine ? q : p;
    // Infinite line: a segment far longer than the circle.
    const double reach = distance(l.a, c.center) + c.radius + 1.0;
    const Vec2 mid = l.foot(c.center);
    return intersectSegmentCircle(mid - l.dir * reach, mid + l.dir * reach, c.center, c.radius);
}

double lineParam(const Line& l, Vec2 p)
{
    const Vec2 d = l.b - l.a;
    return dot(p - l.a, d) / dot(d, d);
}

// Sorts and drops parameters that are equal within kParamEps.
void sortUnique(std::vector<double>& v)
{
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end(), [](double a, double b) { return std::abs(a - b) <= kParamEps; }), v.end());
}

// Nearest cuts on each side of `pick` in [lo, hi]: returns {before, after}.
std::pair<double, double> bracket(const std::vector<double>& cuts, double pick, double lo, double hi)
{
    for (double c : cuts) {
        if (c < pick)
            lo = std::max(lo, c);
        else
            hi = std::min(hi, c);
    }
    return {lo, hi};
}

// --- Polylines are parametrised as u = segment index + position on it.

// Copy without repeated consecutive vertices (zero-length segments).
Polyline cleaned(const Polyline& pl)
{
    Polyline out;
    out.closed = pl.closed;
    for (size_t i = 0; i < pl.points.size(); ++i) {
        if (!out.points.empty() && almostEqual(out.points.back(), pl.points[i])) {
            // The zero-length segment goes; the vertex keeps the next segment's bulge.
            out.setBulge(out.points.size() - 1, pl.bulge(i));
            continue;
        }
        out.points.push_back(pl.points[i]);
        out.setBulge(out.points.size() - 1, pl.bulge(i));
    }
    if (out.closed && out.points.size() > 1 && almostEqual(out.points.back(), out.points.front())) {
        out.points.pop_back();
        out.bulges.resize(std::min(out.bulges.size(), out.points.size()));
    }
    return out;
}

Vec2 pointAt(const Polyline& pl, double u)
{
    const size_t i = std::min(static_cast<size_t>(std::max(0.0, std::floor(u))), pl.segmentCount() - 1);
    return segmentPoint(pl, i, u - static_cast<double>(i));
}

// The part of an open polyline between parameters u0 < u1.
Polyline subPolyline(const Polyline& pl, double u0, double u1)
{
    Polyline out;
    const size_t last = pl.segmentCount() - 1;
    const size_t i0 = std::min(static_cast<size_t>(std::floor(u0)), last);
    const size_t i1 = std::min(static_cast<size_t>(std::max(std::ceil(u1) - 1.0, 0.0)), last);
    for (size_t i = i0; i <= i1; ++i) {
        const double t0 = i == i0 ? u0 - static_cast<double>(i) : 0.0;
        const double t1 = i == i1 ? u1 - static_cast<double>(i) : 1.0;
        if (t1 - t0 <= kParamEps && !(i == i0 && i == i1))
            continue;
        out.points.push_back(segmentPoint(pl, i, t0));
        const double b = pl.bulge(i);
        out.setBulge(out.points.size() - 1, b == 0.0 ? 0.0 : bulgeOfSweep(4.0 * std::atan(b) * (t1 - t0)));
    }
    out.points.push_back(pointAt(pl, u1));
    out = cleaned(out);
    out.bulges.resize(std::min(out.bulges.size(), out.points.size() - 1));
    return out;
}

// Parameter of the point of the polyline nearest to p.
double polylineParam(const Polyline& pl, Vec2 p)
{
    double best = 0.0;
    double bestDist = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < pl.segmentCount(); ++i) {
        const double t = segmentParam(pl, i, p);
        const double dist = distance(p, segmentPoint(pl, i, t));
        if (dist < bestDist) {
            bestDist = dist;
            best = static_cast<double>(i) + t;
        }
    }
    return best;
}

bool hasLength(const Polyline& pl)
{
    return pl.points.size() >= 2;
}

// Length of a ray long enough to cross everything in the drawing.
double rayLength(const Shape& shape, const std::vector<Shape>& boundaries)
{
    BBox box = boundingBox(shape);
    for (const Shape& b : boundaries)
        box.add(boundingBox(b));
    return 2.0 * (box.width() + box.height()) + 1.0;
}

// Distance along `dir` from `from` to the first boundary crossed.
std::optional<double> firstHit(Vec2 from, Vec2 dir, double reach, const std::vector<Shape>& boundaries)
{
    const Line ray{from, from + dir * reach};
    std::optional<double> best;
    for (const Shape& b : boundaries) {
        for (Vec2 v : intersections(ray, b)) {
            const double d = dot(v - from, dir);
            if (d > kEps && (!best || d < *best))
                best = d;
        }
    }
    return best;
}

std::optional<Shape> offsetPolyline(const Polyline& source, double dist, Vec2 side)
{
    const Polyline pl = cleaned(source);
    const size_t n = pl.points.size();
    if (n < 2)
        return std::nullopt;
    const bool closed = pl.closed && n >= 3;
    const size_t segments = closed ? n : n - 1;
    Polyline src = pl;
    src.closed = closed;

    // The side is decided on the segment nearest to the picked point.
    const double u = polylineParam(src, side);
    const size_t k = std::min(static_cast<size_t>(u), segments - 1);
    const double tk = u - static_cast<double>(k);
    const double sign = cross(segmentTangent(src, k, tk), side - segmentPoint(src, k, tk)) >= 0.0 ? 1.0 : -1.0;
    const double left = dist * sign;  // offset towards the left of the travel direction

    // Each segment offset on its own: lines shift, arcs change radius.
    struct Seg {
        Carrier carrier;
        Vec2 a, b;  // naive offset ends
        double sweep = 0.0;
    };
    std::vector<Seg> segs(segments);
    for (size_t i = 0; i < segments; ++i) {
        const Vec2 a = src.points[i];
        const Vec2 b = src.points[(i + 1) % n];
        Seg& s = segs[i];
        const double bulge = src.bulge(i);
        if (std::abs(bulge) <= 1e-12) {
            const Vec2 off = normalized(perpendicular(b - a)) * left;
            s.carrier.a = a + off;
            s.carrier.dir = normalized(b - a);
            s.a = a + off;
            s.b = b + off;
            continue;
        }
        const Arc arc = arcFromBulge(a, b, bulge);
        s.sweep = 4.0 * std::atan(bulge);
        // The centre is on the left of a counter-clockwise arc.
        const double r = arc.radius + (bulge > 0.0 ? -left : left);
        if (r <= kEps)
            return std::nullopt;
        s.carrier.isLine = false;
        s.carrier.center = arc.center;
        s.carrier.radius = r;
        s.a = arc.center + normalized(a - arc.center) * r;
        s.b = arc.center + normalized(b - arc.center) * r;
    }

    // Vertices go where consecutive offset segments meet, nearest to the naive point.
    auto corner = [&](size_t before, size_t after) {
        const Vec2 naive = segs[after].a;
        if (almostEqual(segs[before].b, naive, 1e-9))
            return naive;
        std::optional<Vec2> best;
        for (Vec2 v : intersectCarriers(segs[before].carrier, segs[after].carrier))
            if (!best || distance(v, naive) < distance(*best, naive))
                best = v;
        return best.value_or(naive);
    };

    Polyline out;
    out.closed = closed;
    if (closed) {
        for (size_t i = 0; i < n; ++i)
            out.points.push_back(corner((i + segments - 1) % segments, i));
    } else {
        out.points.push_back(segs[0].a);
        for (size_t i = 1; i < segments; ++i)
            out.points.push_back(corner(i - 1, i));
        out.points.push_back(segs[segments - 1].b);
    }
    // Arc segments keep their centre: the bulge follows the new end angles.
    for (size_t i = 0; i < segments; ++i) {
        if (segs[i].sweep == 0.0)
            continue;
        const Vec2 c = segs[i].carrier.center;
        const double d = angleOf(out.points[(i + 1) % out.points.size()] - c) - angleOf(out.points[i] - c);
        double sweep = segs[i].sweep > 0.0 ? normalizeAngle(d) : -normalizeAngle(-d);
        if (std::abs(sweep) < 1e-12)
            sweep = segs[i].sweep;  // full turn
        out.setBulge(i, bulgeOfSweep(sweep));
    }
    return out;
}

TrimResult trimPolyline(const Polyline& source, const std::vector<Vec2>& cutPoints, Vec2 pick)
{
    Polyline pl = cleaned(source);
    if (!hasLength(pl))
        return {true, {}};
    pl.closed = pl.closed && pl.points.size() >= 3;
    const size_t n = pl.points.size();

    if (!pl.closed) {
        const double last = static_cast<double>(n - 1);
        std::vector<double> cuts;
        for (Vec2 v : cutPoints) {
            const double u = polylineParam(pl, v);
            if (u > kParamEps && u < last - kParamEps)
                cuts.push_back(u);
        }
        sortUnique(cuts);
        if (cuts.empty())
            return {true, {}};
        const auto [lo, hi] = bracket(cuts, polylineParam(pl, pick), 0.0, last);
        TrimResult r{true, {}};
        if (lo > 0.0)
            r.pieces.push_back(subPolyline(pl, 0.0, lo));
        if (hi < last)
            r.pieces.push_back(subPolyline(pl, hi, last));
        std::erase_if(r.pieces, [](const Shape& s) { return std::get<Polyline>(s).points.size() < 2; });
        return r;
    }

    const double total = static_cast<double>(n);
    std::vector<double> cuts;
    for (Vec2 v : cutPoints) {
        double u = polylineParam(pl, v);
        if (u >= total - kParamEps)
            u = 0.0;
        cuts.push_back(u);
    }
    sortUnique(cuts);
    if (cuts.empty())
        return {true, {}};
    if (cuts.size() == 1)
        return {false, {}};  // one cut cannot open a closed shape into a piece
    const double p = polylineParam(pl, pick);
    // Cyclic neighbours of the pick: the kept part runs from `after` round to `before`.
    double before = cuts.back(), after = cuts.front();
    for (double c : cuts) {
        if (c < p)
            before = c;
        else {
            after = c;
            break;
        }
    }
    // Two laps of the vertices let the kept part run past the first vertex.
    Polyline laps;
    for (int lap = 0; lap < 2; ++lap)
        for (size_t i = 0; i < n; ++i) {
            laps.points.push_back(pl.points[i]);
            laps.setBulge(laps.points.size() - 1, pl.bulge(i));
        }
    laps.points.push_back(pl.points.front());
    const double end = before > after ? before : before + total;
    return {true, {subPolyline(laps, after, end)}};
}

} // namespace

std::optional<Shape> offsetShape(const Shape& shape, double dist, Vec2 sidePoint)
{
    if (dist <= kEps)
        return std::nullopt;
    if (const auto* l = std::get_if<Line>(&shape)) {
        const Vec2 n = normalized(perpendicular(l->b - l->a));
        if (length(n) < kEps)
            return std::nullopt;
        const Vec2 off = n * (dot(sidePoint - l->a, n) >= 0.0 ? dist : -dist);
        return Line{l->a + off, l->b + off};
    }
    // Circles and arcs grow when the side point is outside, shrink otherwise.
    auto offsetRadius = [&](Vec2 center, double radius) {
        return distance(center, sidePoint) >= radius ? radius + dist : radius - dist;
    };
    if (const auto* c = std::get_if<Circle>(&shape)) {
        const double r = offsetRadius(c->center, c->radius);
        if (r <= kEps)
            return std::nullopt;
        return Circle{c->center, r};
    }
    if (const auto* a = std::get_if<Arc>(&shape)) {
        Arc r = *a;
        r.radius = offsetRadius(a->center, a->radius);
        if (r.radius <= kEps)
            return std::nullopt;
        return r;
    }
    if (const auto* pl = std::get_if<Polyline>(&shape))
        return offsetPolyline(*pl, dist, sidePoint);
    return std::nullopt;  // texts and dimensions
}

TrimResult trimShape(const Shape& shape, const std::vector<Shape>& boundaries, Vec2 pick)
{
    std::vector<Vec2> cutPoints;
    for (const Shape& b : boundaries)
        for (Vec2 v : intersections(shape, b))
            cutPoints.push_back(v);

    if (const auto* l = std::get_if<Line>(&shape)) {
        std::vector<double> cuts;
        for (Vec2 v : cutPoints) {
            const double t = lineParam(*l, v);
            if (t > kParamEps && t < 1.0 - kParamEps)
                cuts.push_back(t);
        }
        sortUnique(cuts);
        if (cuts.empty())
            return {true, {}};
        const auto [lo, hi] = bracket(cuts, lineParam(*l, pick), 0.0, 1.0);
        TrimResult r{true, {}};
        if (lo > 0.0)
            r.pieces.push_back(Line{l->a, lerp(l->a, l->b, lo)});
        if (hi < 1.0)
            r.pieces.push_back(Line{lerp(l->a, l->b, hi), l->b});
        return r;
    }

    if (const auto* c = std::get_if<Circle>(&shape)) {
        std::vector<double> angles;
        for (Vec2 v : cutPoints)
            angles.push_back(normalizeAngle(angleOf(v - c->center)));
        sortUnique(angles);
        if (angles.size() >= 2 && angles.back() - angles.front() >= kTwoPi - kParamEps)
            angles.pop_back();  // 0 and 2*pi are the same cut
        if (angles.empty())
            return {true, {}};
        if (angles.size() == 1)
            return {false, {}};
        const double p = normalizeAngle(angleOf(pick - c->center));
        double before = angles.back(), after = angles.front();
        for (double a : angles) {
            if (a < p)
                before = a;
            else {
                after = a;
                break;
            }
        }
        return {true, {Arc{c->center, c->radius, after, before}}};
    }

    if (const auto* a = std::get_if<Arc>(&shape)) {
        const double sweep = a->sweep();
        auto param = [&](Vec2 v) { return normalizeAngle(angleOf(v - a->center) - a->startAngle); };
        std::vector<double> cuts;
        for (Vec2 v : cutPoints) {
            const double s = param(v);
            if (s > kParamEps && s < sweep - kParamEps)
                cuts.push_back(s);
        }
        sortUnique(cuts);
        if (cuts.empty())
            return {true, {}};
        double p = param(pick);
        if (p > sweep)  // pick just outside the arc: use the nearer end
            p = (p - sweep < kTwoPi - p) ? sweep : 0.0;
        const auto [lo, hi] = bracket(cuts, p, 0.0, sweep);
        TrimResult r{true, {}};
        if (lo > 0.0)
            r.pieces.push_back(Arc{a->center, a->radius, a->startAngle, normalizeAngle(a->startAngle + lo)});
        if (hi < sweep)
            r.pieces.push_back(Arc{a->center, a->radius, normalizeAngle(a->startAngle + hi), a->endAngle});
        return r;
    }

    if (const auto* pl = std::get_if<Polyline>(&shape))
        return trimPolyline(*pl, cutPoints, pick);
    return {false, {}};  // texts and dimensions are not trimmed
}

std::optional<Shape> extendShape(const Shape& shape, const std::vector<Shape>& boundaries, Vec2 pick)
{
    const double reach = rayLength(shape, boundaries);

    if (const auto* l = std::get_if<Line>(&shape)) {
        const bool atEnd = distance(pick, l->b) <= distance(pick, l->a);
        const Vec2 from = atEnd ? l->b : l->a;
        const Vec2 dir = normalized(atEnd ? l->b - l->a : l->a - l->b);
        const auto hit = firstHit(from, dir, reach, boundaries);
        if (!hit)
            return std::nullopt;
        const Vec2 p = from + dir * *hit;
        return atEnd ? Line{l->a, p} : Line{p, l->b};
    }

    if (const auto* a = std::get_if<Arc>(&shape)) {
        const bool atEnd = distance(pick, a->endPoint()) <= distance(pick, a->startPoint());
        // Room left on the circle before the arc would close on itself.
        const double room = kTwoPi - a->sweep();
        std::optional<double> best;
        for (const Shape& b : boundaries) {
            for (Vec2 v : intersections(Circle{a->center, a->radius}, b)) {
                const double ang = angleOf(v - a->center);
                const double delta = atEnd ? normalizeAngle(ang - a->endAngle) : normalizeAngle(a->startAngle - ang);
                if (delta > kParamEps && delta < room - kParamEps && (!best || delta < *best))
                    best = delta;
            }
        }
        if (!best)
            return std::nullopt;
        Arc r = *a;
        if (atEnd)
            r.endAngle = normalizeAngle(a->endAngle + *best);
        else
            r.startAngle = normalizeAngle(a->startAngle - *best);
        return r;
    }

    if (const auto* source = std::get_if<Polyline>(&shape)) {
        Polyline pl = cleaned(*source);
        if (pl.closed || pl.points.size() < 2)
            return std::nullopt;
        const size_t n = pl.points.size();
        const bool atEnd = distance(pick, pl.points[n - 1]) <= distance(pick, pl.points[0]);
        const size_t seg = atEnd ? n - 2 : 0;
        Vec2& end = atEnd ? pl.points[n - 1] : pl.points[0];
        const double bulge = pl.bulge(seg);
        if (std::abs(bulge) <= 1e-12) {
            const Vec2 dir = normalized(atEnd ? pl.points[n - 1] - pl.points[n - 2] : pl.points[0] - pl.points[1]);
            const auto hit = firstHit(end, dir, reach, boundaries);
            if (!hit)
                return std::nullopt;
            end = end + dir * *hit;
            return pl;
        }
        // An arc segment extends along its circle, like an arc.
        const Arc arc = arcFromBulge(pl.points[seg], pl.points[seg + 1], bulge);
        const auto longer = extendShape(arc, boundaries, end);
        if (!longer)
            return std::nullopt;
        const Arc& r = std::get<Arc>(*longer);
        const bool startMoved = !almostEqual(r.startPoint(), arc.startPoint(), 1e-9);
        end = startMoved ? r.startPoint() : r.endPoint();
        pl.setBulge(seg, bulgeOfSweep(bulge > 0.0 ? r.sweep() : -r.sweep()));
        return pl;
    }

    return std::nullopt;  // circles have no ends
}

std::optional<FilletResult> filletLines(const Line& first, Vec2 pickFirst, const Line& second, Vec2 pickSecond,
                                        double radius, FilletError* error)
{
    auto fail = [&](FilletError e) -> std::optional<FilletResult> {
        if (error)
            *error = e;
        return std::nullopt;
    };
    if (error)
        *error = FilletError::None;

    const Vec2 d1 = first.b - first.a;
    const Vec2 d2 = second.b - second.a;
    const auto corner = intersectLines(first.a, d1, second.a, d2);
    if (!corner)
        return fail(FilletError::Parallel);
    const Vec2 p = *corner;

    // Directions from the corner towards the part of each line to keep.
    const Vec2 u1 = normalized(d1) * (dot(pickFirst - p, d1) >= 0.0 ? 1.0 : -1.0);
    const Vec2 u2 = normalized(d2) * (dot(pickSecond - p, d2) >= 0.0 ? 1.0 : -1.0);
    const Vec2 far1 = dot(first.a - p, u1) >= dot(first.b - p, u1) ? first.a : first.b;
    const Vec2 far2 = dot(second.a - p, u2) >= dot(second.b - p, u2) ? second.a : second.b;

    if (radius <= kEps)
        return FilletResult{Line{far1, p}, Line{far2, p}, std::nullopt};

    const double angle = std::acos(std::clamp(dot(u1, u2), -1.0, 1.0));
    if (angle < 1e-9 || angle > kPi - 1e-9)
        return fail(FilletError::Parallel);
    const double half = angle / 2.0;
    const double tangent = radius / std::tan(half);
    if (tangent > dot(far1 - p, u1) + kEps || tangent > dot(far2 - p, u2) + kEps)
        return fail(FilletError::RadiusTooLarge);

    const Vec2 t1 = p + u1 * tangent;
    const Vec2 t2 = p + u2 * tangent;
    const Vec2 center = p + normalized(u1 + u2) * (radius / std::sin(half));
    const double a1 = normalizeAngle(angleOf(t1 - center));
    const double a2 = normalizeAngle(angleOf(t2 - center));
    // The fillet is always the short way round (less than half a turn).
    const bool ccw = normalizeAngle(a2 - a1) <= kPi;
    Arc arc{center, radius, ccw ? a1 : a2, ccw ? a2 : a1};
    return FilletResult{Line{far1, t1}, Line{far2, t2}, arc};
}

namespace {

// Corner of two lines and, for each, the direction from the corner towards
// the part to keep and the far end of that part.
struct Corner {
    Vec2 point;
    Vec2 u1, u2;
    Vec2 far1, far2;
};

std::optional<Corner> cornerOf(const Line& first, Vec2 pickFirst, const Line& second, Vec2 pickSecond)
{
    const Vec2 d1 = first.b - first.a;
    const Vec2 d2 = second.b - second.a;
    const auto p = intersectLines(first.a, d1, second.a, d2);
    if (!p)
        return std::nullopt;
    Corner c;
    c.point = *p;
    c.u1 = normalized(d1) * (dot(pickFirst - *p, d1) >= 0.0 ? 1.0 : -1.0);
    c.u2 = normalized(d2) * (dot(pickSecond - *p, d2) >= 0.0 ? 1.0 : -1.0);
    c.far1 = dot(first.a - *p, c.u1) >= dot(first.b - *p, c.u1) ? first.a : first.b;
    c.far2 = dot(second.a - *p, c.u2) >= dot(second.b - *p, c.u2) ? second.a : second.b;
    return c;
}

// Arc trimmed or extended to end at angle `at`, keeping the side of `pick`.
Arc arcTo(const Arc& arc, double at, Vec2 pick)
{
    const double ap = angleOf(pick - arc.center);
    const Arc keepEnd{arc.center, arc.radius, normalizeAngle(at), arc.endAngle};
    const Arc keepStart{arc.center, arc.radius, arc.startAngle, normalizeAngle(at)};
    const bool inEnd = angleInSweep(ap, keepEnd.startAngle, keepEnd.endAngle);
    const bool inStart = angleInSweep(ap, keepStart.startAngle, keepStart.endAngle);
    if (inEnd != inStart)
        return inEnd ? keepEnd : keepStart;
    // The tangent point is off the arc: both pieces contain the pick, and the
    // shorter one is the arc extended to it.
    return keepEnd.sweep() <= keepStart.sweep() ? keepEnd : keepStart;
}

Line lineTo(const Line& line, Vec2 t, Vec2 pick)
{
    const Vec2 d = line.b - line.a;
    const Vec2 u = d * (dot(pick - t, d) >= 0.0 ? 1.0 : -1.0);
    const Vec2 far = dot(line.a - t, u) >= dot(line.b - t, u) ? line.a : line.b;
    return Line{far, t};
}

Shape cutAt(const Shape& s, Vec2 t, Vec2 pick)
{
    if (const auto* l = std::get_if<Line>(&s))
        return lineTo(*l, t, pick);
    if (const auto* a = std::get_if<Arc>(&s))
        return arcTo(*a, angleOf(t - a->center), pick);
    return s;  // circles stay whole
}

} // namespace

std::optional<CurveFilletResult> filletCurves(const Shape& first, Vec2 pickFirst, const Shape& second, Vec2 pickSecond,
                                              double radius, FilletError* error)
{
    if (error)
        *error = FilletError::None;
    if (const auto* l1 = std::get_if<Line>(&first))
        if (const auto* l2 = std::get_if<Line>(&second)) {
            const auto r = filletLines(*l1, pickFirst, *l2, pickSecond, radius, error);
            if (!r)
                return std::nullopt;
            return CurveFilletResult{r->first, r->second, r->arc};
        }

    const auto c1 = carrierOf(first);
    const auto c2 = carrierOf(second);
    if (!c1 || !c2) {
        if (error)
            *error = FilletError::Parallel;
        return std::nullopt;
    }

    struct Candidate {
        Vec2 center, t1, t2;
        double score;
    };
    std::optional<Candidate> best;
    for (const Carrier& o1 : offsetsOf(*c1, radius)) {
        for (const Carrier& o2 : offsetsOf(*c2, radius)) {
            for (Vec2 center : intersectCarriers(o1, o2)) {
                const Vec2 t1 = c1->foot(center);
                const Vec2 t2 = c2->foot(center);
                if (radius > kEps && (std::abs(distance(center, t1) - radius) > 1e-6 * (1.0 + radius)
                                      || std::abs(distance(center, t2) - radius) > 1e-6 * (1.0 + radius)))
                    continue;
                const double score = distance(t1, pickFirst) + distance(t2, pickSecond);
                if (!best || score < best->score)
                    best = Candidate{center, t1, t2, score};
            }
        }
    }
    if (!best) {
        if (error)
            *error = radius > kEps ? FilletError::RadiusTooLarge : FilletError::Parallel;
        return std::nullopt;
    }

    CurveFilletResult r{cutAt(first, best->t1, pickFirst), cutAt(second, best->t2, pickSecond), std::nullopt};
    if (radius > kEps && !almostEqual(best->t1, best->t2)) {
        const double a1 = normalizeAngle(angleOf(best->t1 - best->center));
        const double a2 = normalizeAngle(angleOf(best->t2 - best->center));
        const bool ccw = normalizeAngle(a2 - a1) <= kPi;
        r.arc = Arc{best->center, radius, ccw ? a1 : a2, ccw ? a2 : a1};
    }
    return r;
}

std::optional<ChamferResult> chamferLines(const Line& first, Vec2 pickFirst, const Line& second, Vec2 pickSecond,
                                          double dist1, double dist2, FilletError* error)
{
    if (error)
        *error = FilletError::None;
    const auto c = cornerOf(first, pickFirst, second, pickSecond);
    if (!c) {
        if (error)
            *error = FilletError::Parallel;
        return std::nullopt;
    }
    if (dist1 <= kEps && dist2 <= kEps)
        return ChamferResult{Line{c->far1, c->point}, Line{c->far2, c->point}, std::nullopt};
    if (dist1 > dot(c->far1 - c->point, c->u1) + kEps || dist2 > dot(c->far2 - c->point, c->u2) + kEps) {
        if (error)
            *error = FilletError::RadiusTooLarge;
        return std::nullopt;
    }
    const Vec2 e1 = c->point + c->u1 * dist1;
    const Vec2 e2 = c->point + c->u2 * dist2;
    return ChamferResult{Line{c->far1, e1}, Line{c->far2, e2}, Line{e1, e2}};
}

std::optional<double> chamferDistanceFromAngle(const Line& first, Vec2 pickFirst, const Line& second, Vec2 pickSecond,
                                               double dist1, double angle)
{
    const auto c = cornerOf(first, pickFirst, second, pickSecond);
    if (!c)
        return std::nullopt;
    // Triangle corner, e1, e2: the angle at the corner is between the lines,
    // the one at e1 is the chamfer angle; the sine rule gives the side on line 2.
    const double atCorner = std::acos(std::clamp(dot(c->u1, c->u2), -1.0, 1.0));
    const double atE2 = kPi - atCorner - angle;
    if (angle <= 0.0 || atE2 <= 1e-9)
        return std::nullopt;
    return dist1 * std::sin(angle) / std::sin(atE2);
}

namespace {

// Cuts each corner of `pl` between two straight segments. `cut` gets the
// incoming and outgoing directions and returns the distances from the corner
// along each, or nothing to leave the corner; `bulgeFor` gives the bulge of
// the new segment from the turn (signed deflection angle).
template <class Cut, class BulgeFor>
PolylineCornersResult cutCorners(const Polyline& source, Cut cut, BulgeFor bulgeFor)
{
    const Polyline pl = cleaned(source);
    PolylineCornersResult r;
    const size_t n = pl.points.size();
    const bool closed = pl.closed && n >= 3;
    r.polyline.closed = pl.closed;
    for (size_t i = 0; i < n; ++i) {
        const bool corner = closed || (i > 0 && i + 1 < n);
        const size_t prev = (i + n - 1) % n;
        const size_t next = (i + 1) % n;
        auto keep = [&] {
            r.polyline.points.push_back(pl.points[i]);
            r.polyline.setBulge(r.polyline.points.size() - 1, pl.bulge(i));
        };
        if (!corner || std::abs(pl.bulge(prev)) > 1e-12 || std::abs(pl.bulge(i)) > 1e-12) {
            keep();
            continue;
        }
        const Vec2 p = pl.points[i];
        const double lenIn = distance(pl.points[prev], p);
        const double lenOut = distance(p, pl.points[next]);
        const Vec2 dIn = normalized(p - pl.points[prev]);
        const Vec2 dOut = normalized(pl.points[next] - p);
        const double turn = std::atan2(cross(dIn, dOut), dot(dIn, dOut));
        if (std::abs(turn) < 1e-9) {
            keep();  // straight through: nothing to cut
            continue;
        }
        const auto d = cut(dIn, dOut, std::abs(turn));
        if (!d) {
            keep();
            ++r.skipped;
            continue;
        }
        // A segment shared by two corners gives each at most half its length.
        const double roomIn = (closed || prev > 0) ? lenIn / 2.0 : lenIn;
        const double roomOut = (closed || next + 1 < n) ? lenOut / 2.0 : lenOut;
        if (d->first > roomIn + kEps || d->second > roomOut + kEps) {
            keep();
            ++r.skipped;
            continue;
        }
        r.polyline.points.push_back(p - dIn * d->first);
        r.polyline.setBulge(r.polyline.points.size() - 1, bulgeFor(turn));
        r.polyline.points.push_back(p + dOut * d->second);
        r.polyline.setBulge(r.polyline.points.size() - 1, pl.bulge(i));
        ++r.changed;
    }
    r.polyline = cleaned(r.polyline);
    return r;
}

} // namespace

PolylineCornersResult filletPolyline(const Polyline& pl, double radius)
{
    if (radius <= kEps)
        return {cleaned(pl), 0, 0};
    return cutCorners(
        pl,
        [&](Vec2, Vec2, double deflection) -> std::optional<std::pair<double, double>> {
            const double t = radius * std::tan(deflection / 2.0);
            return std::pair{t, t};
        },
        // The fillet turns as much as the corner did.
        [](double turn) { return bulgeOfSweep(turn); });
}

PolylineCornersResult chamferPolyline(const Polyline& pl, double dist1, double dist2, std::optional<double> angle)
{
    if (dist1 <= kEps && (angle || dist2 <= kEps))
        return {cleaned(pl), 0, 0};
    return cutCorners(
        pl,
        [&](Vec2, Vec2, double deflection) -> std::optional<std::pair<double, double>> {
            if (!angle)
                return std::pair{dist1, dist2};
            // Sine rule in the triangle cut off: the corner's inner angle is
            // pi - deflection, the chamfer leaves the first segment at `angle`.
            const double opposite = deflection - *angle;
            if (*angle <= 0.0 || opposite <= 1e-9)
                return std::nullopt;
            return std::pair{dist1, dist1 * std::sin(*angle) / std::sin(opposite)};
        },
        [](double) { return 0.0; });
}

Shape stretchShape(const Shape& shape, const BBox& window, Vec2 offset)
{
    auto move = [&](Vec2 p) { return window.contains(p) ? p + offset : p; };
    if (const auto* l = std::get_if<Line>(&shape))
        return Line{move(l->a), move(l->b)};
    if (const auto* pl = std::get_if<Polyline>(&shape)) {
        Polyline r = *pl;
        for (Vec2& p : r.points)
            p = move(p);
        return r;
    }
    if (const auto* c = std::get_if<Circle>(&shape))
        return Circle{move(c->center), c->radius};
    if (const auto* a = std::get_if<Arc>(&shape)) {
        const Vec2 s = a->startPoint();
        const Vec2 e = a->endPoint();
        const bool sIn = window.contains(s);
        const bool eIn = window.contains(e);
        if (sIn && eIn)
            return transformed(*a, Transform::translation(offset));
        if (!sIn && !eIn)
            return *a;
        const Vec2 mid = a->center + polar(a->radius, a->startAngle + a->sweep() / 2.0);
        if (const auto r = arcFromThreePoints(move(s), mid, move(e)))
            return *r;
        return *a;
    }
    if (const auto* t = std::get_if<Text>(&shape)) {
        Text r = *t;
        r.position = move(t->position);
        return r;
    }
    if (const auto* h = std::get_if<Hatch>(&shape)) {
        Hatch r = *h;
        for (Polyline& loop : r.loops)
            loop = std::get<Polyline>(stretchShape(loop, window, offset));
        return r;
    }
    if (const auto* sl = std::get_if<Slab>(&shape)) {
        Slab r = *sl;
        for (Polyline& loop : r.loops)
            loop = std::get<Polyline>(stretchShape(loop, window, offset));
        return r;
    }
    if (const auto* roof = std::get_if<Roof>(&shape)) {
        Roof r = *roof;
        r.footprint = std::get<Polyline>(stretchShape(roof->footprint, window, offset));
        return r;
    }
    if (const auto* st = std::get_if<Stair>(&shape)) {
        Stair r = *st;
        r.start = move(st->start);
        return r;
    }
    if (const auto* w = std::get_if<Wall>(&shape)) {
        Wall r = *w;
        r.start = move(w->start);
        r.end = move(w->end);
        if (window.contains(w->start) && window.contains(w->end))
            return r;
        // Doors and windows stay where they are in the drawing, inside the wall.
        const double len = wallLength(r);
        for (Opening& o : r.openings) {
            const double t = wallParam(r, wallPoint(*w, o.position));
            o.position = std::clamp(t, std::min(o.width / 2.0, len / 2.0), std::max(len - o.width / 2.0, len / 2.0));
        }
        return r;
    }
    if (const auto* d = std::get_if<Dimension>(&shape)) {
        Dimension r = *d;
        const bool radial = d->kind == Dimension::Kind::Radius || d->kind == Dimension::Kind::Diameter;
        if (radial && window.contains(d->p1)) {
            // Moving the centre moves the whole circle being measured.
            r.p1 = d->p1 + offset;
            r.p2 = d->p2 + offset;
        } else {
            r.p1 = move(d->p1);
            r.p2 = move(d->p2);
        }
        r.location = move(d->location);
        return r;
    }
    return shape;
}

namespace {

Vec2 arcMidpoint(const Arc& a)
{
    return a.center + polar(a.radius, a.startAngle + a.sweep() / 2.0);
}

Vec2 segmentMidpoint(const Polyline& pl, size_t i)
{
    const Shape seg = polylineSegment(pl, i);
    if (const auto* a = std::get_if<Arc>(&seg))
        return arcMidpoint(*a);
    const Line& l = std::get<Line>(seg);
    return (l.a + l.b) * 0.5;
}

// The bulge of the arc from `a` through `m` to `b`; 0 when they are in line.
std::optional<double> bulgeThrough(Vec2 a, Vec2 m, Vec2 b)
{
    const auto arc = arcFromThreePoints(a, m, b);
    if (!arc)
        return std::nullopt;
    const bool ccw = cross(m - a, b - a) > 0.0;
    return ccw ? bulgeOfSweep(arc->sweep()) : -bulgeOfSweep(arc->sweep());
}

} // namespace

std::vector<Vec2> gripPoints(const Shape& shape)
{
    if (const auto* l = std::get_if<Line>(&shape))
        return {l->a, l->b, (l->a + l->b) * 0.5};
    if (const auto* c = std::get_if<Circle>(&shape))
        return {c->center, c->center + Vec2{c->radius, 0}, c->center + Vec2{0, c->radius}, c->center - Vec2{c->radius, 0},
                c->center - Vec2{0, c->radius}};
    if (const auto* a = std::get_if<Arc>(&shape))
        return {a->center, a->startPoint(), a->endPoint(), arcMidpoint(*a)};
    if (const auto* pl = std::get_if<Polyline>(&shape)) {
        std::vector<Vec2> grips = pl->points;
        for (size_t i = 0; i < pl->segmentCount(); ++i)
            grips.push_back(segmentMidpoint(*pl, i));
        return grips;
    }
    if (const auto* t = std::get_if<Text>(&shape))
        return {t->position};
    if (const auto* d = std::get_if<Dimension>(&shape))
        return {d->p1, d->p2, d->location};
    if (const auto* w = std::get_if<Wall>(&shape))
        return {w->start, w->end, (w->start + w->end) * 0.5};
    return {};
}

std::optional<Shape> moveGrip(const Shape& shape, size_t index, Vec2 to)
{
    const std::vector<Vec2> grips = gripPoints(shape);
    if (index >= grips.size())
        return std::nullopt;
    const Vec2 delta = to - grips[index];

    if (auto l = std::get_if<Line>(&shape)) {
        Line r = *l;
        if (index == 0)
            r.a = to;
        else if (index == 1)
            r.b = to;
        else
            return transformed(shape, Transform::translation(delta));
        if (almostEqual(r.a, r.b))
            return std::nullopt;
        return r;
    }
    if (const auto* c = std::get_if<Circle>(&shape)) {
        if (index == 0)
            return transformed(shape, Transform::translation(delta));
        const double radius = distance(c->center, to);
        if (radius < kEps)
            return std::nullopt;
        return Circle{c->center, radius};
    }
    if (const auto* a = std::get_if<Arc>(&shape)) {
        if (index == 0)
            return transformed(shape, Transform::translation(delta));
        Vec2 pts[3] = {a->startPoint(), arcMidpoint(*a), a->endPoint()};
        pts[index == 1 ? 0 : index == 2 ? 2 : 1] = to;
        const auto arc = arcFromThreePoints(pts[0], pts[1], pts[2]);
        if (!arc)
            return std::nullopt;
        return *arc;
    }
    if (const auto* pl = std::get_if<Polyline>(&shape)) {
        Polyline r = *pl;
        const size_t n = pl->points.size();
        if (index < n) {
            r.points[index] = to;
            return r;
        }
        const size_t seg = index - n;
        const size_t next = (seg + 1) % n;
        if (std::abs(pl->bulge(seg)) < kEps) {
            // A straight segment moves parallel to itself.
            r.points[seg] = r.points[seg] + delta;
            r.points[next] = r.points[next] + delta;
            return r;
        }
        const auto bulge = bulgeThrough(pl->points[seg], to, pl->points[next]);
        if (!bulge)
            return std::nullopt;
        r.setBulge(seg, *bulge);
        return r;
    }
    if (const auto* t = std::get_if<Text>(&shape)) {
        Text r = *t;
        r.position = to;
        return r;
    }
    if (const auto* d = std::get_if<Dimension>(&shape)) {
        Dimension r = *d;
        (index == 0 ? r.p1 : index == 1 ? r.p2 : r.location) = to;
        return r;
    }
    if (const auto* w = std::get_if<Wall>(&shape)) {
        if (index == 2)
            return transformed(shape, Transform::translation(delta));
        Wall r = *w;
        (index == 0 ? r.start : r.end) = to;
        const double len = distance(r.start, r.end);
        if (len < kEps)
            return std::nullopt;
        // Doors and windows stay where they are along the wall, within its new length.
        const Vec2 oldDir = normalized(w->end - w->start);
        const Vec2 newDir = (r.end - r.start) * (1.0 / len);
        for (Opening& o : r.openings) {
            const Vec2 centre = w->start + oldDir * o.position;
            o.position = std::clamp(dot(centre - r.start, newDir), o.width / 2.0, std::max(o.width / 2.0, len - o.width / 2.0));
        }
        return r;
    }
    return std::nullopt;
}

double shapeLength(const Shape& shape)
{
    if (const auto* l = std::get_if<Line>(&shape))
        return distance(l->a, l->b);
    if (const auto* c = std::get_if<Circle>(&shape))
        return kTwoPi * c->radius;
    if (const auto* a = std::get_if<Arc>(&shape))
        return a->radius * a->sweep();
    if (const auto* pl = std::get_if<Polyline>(&shape)) {
        double total = 0.0;
        for (size_t i = 0; i < pl->segmentCount(); ++i)
            total += shapeLength(polylineSegment(*pl, i));
        return total;
    }
    return 0.0;
}

double polylineArea(const Polyline& pl)
{
    const size_t n = pl.points.size();
    if (n < 2)
        return 0.0;
    double area = 0.0;
    for (size_t i = 0; i < n; ++i)
        area += cross(pl.points[i], pl.points[(i + 1) % n]) / 2.0;
    // Each arc adds (or removes) the circular segment between it and its
    // chord: a counter-clockwise arc lies to the right of its chord.
    for (size_t i = 0; i < pl.segmentCount(); ++i) {
        const Shape seg = polylineSegment(pl, i);
        if (const auto* a = std::get_if<Arc>(&seg)) {
            const double theta = a->sweep();
            const double segment = a->radius * a->radius / 2.0 * (theta - std::sin(theta));
            area += pl.bulge(i) > 0.0 ? segment : -segment;
        }
    }
    return std::abs(area);
}

std::optional<double> shapeArea(const Shape& shape)
{
    if (const auto* c = std::get_if<Circle>(&shape))
        return kPi * c->radius * c->radius;
    if (const auto* pl = std::get_if<Polyline>(&shape))
        if (pl->closed)
            return polylineArea(*pl);
    return std::nullopt;
}

std::vector<Shape> explodeShape(const Shape& shape)
{
    std::vector<Shape> out;
    if (const auto* pl = std::get_if<Polyline>(&shape)) {
        for (size_t i = 0; i < pl->segmentCount(); ++i)
            out.push_back(polylineSegment(*pl, i));
    } else if (const auto* d = std::get_if<Dimension>(&shape)) {
        const DimensionGeometry g = dimensionGeometry(*d);
        for (const Line& l : g.lines)
            out.push_back(l);
        for (const auto& tri : g.arrows) {
            Hatch h;
            h.pattern = "SOLID";
            h.loops.push_back(Polyline({tri[0], tri[1], tri[2]}, true));
            out.push_back(h);
        }
        out.push_back(g.label);
    }
    return out;
}

namespace {

Polyline reversed(const Polyline& pl)
{
    Polyline r;
    r.points.assign(pl.points.rbegin(), pl.points.rend());
    const size_t segments = pl.points.size() - 1;
    for (size_t i = 0; i < segments; ++i)
        r.setBulge(i, -pl.bulge(segments - 1 - i));
    return r;
}

// Appends `next`, whose first point is the last of `chain`.
void append(Polyline& chain, const Polyline& next)
{
    const size_t offset = chain.points.size() - 1;
    for (size_t i = 1; i < next.points.size(); ++i)
        chain.points.push_back(next.points[i]);
    for (size_t i = 0; i + 1 < next.points.size(); ++i)
        chain.setBulge(offset + i, next.bulge(i));
}

std::optional<Polyline> chainPiece(const Shape& shape)
{
    if (const auto* l = std::get_if<Line>(&shape))
        return Polyline({l->a, l->b});
    if (const auto* a = std::get_if<Arc>(&shape))
        return Polyline({a->startPoint(), a->endPoint()}, false, {bulgeOfSweep(a->sweep())});
    if (const auto* pl = std::get_if<Polyline>(&shape))
        if (!pl->closed && pl->points.size() >= 2)
            return *pl;
    return std::nullopt;
}

// Removes the vertices between straight segments in a row.
Polyline simplified(const Polyline& pl)
{
    Polyline r;
    r.closed = pl.closed;
    const size_t n = pl.points.size();
    for (size_t i = 0; i < n; ++i) {
        const bool interior = pl.closed || (i > 0 && i + 1 < n);
        if (interior && !r.points.empty()) {
            const size_t prev = (i + n - 1) % n;
            const Vec2 a = r.points.back();
            const Vec2 b = pl.points[i];
            const Vec2 c = pl.points[(i + 1) % n];
            const bool straight = std::abs(pl.bulge(prev)) < kEps && std::abs(pl.bulge(i)) < kEps;
            if (straight && std::abs(cross(normalized(b - a), normalized(c - b))) < 1e-9 && dot(b - a, c - b) > 0.0)
                continue;
        }
        if (!r.points.empty())
            r.setBulge(r.points.size() - 1, pl.bulge((i + n - 1) % n));
        r.points.push_back(pl.points[i]);
    }
    if (pl.closed)
        r.setBulge(r.points.size() - 1, pl.bulge(n - 1));
    return r;
}

} // namespace

std::vector<JoinedShape> joinShapes(const std::vector<Shape>& shapes, double tolerance)
{
    std::vector<JoinedShape> out;
    std::vector<Polyline> pieces;
    std::vector<size_t> sources;
    for (size_t i = 0; i < shapes.size(); ++i) {
        if (auto p = chainPiece(shapes[i])) {
            pieces.push_back(*p);
            sources.push_back(i);
        }
    }
    std::vector<bool> used(pieces.size(), false);
    for (size_t start = 0; start < pieces.size(); ++start) {
        if (used[start])
            continue;
        used[start] = true;
        Polyline chain = pieces[start];
        std::vector<size_t> from{sources[start]};
        for (bool grew = true; grew;) {
            grew = false;
            for (size_t j = 0; j < pieces.size(); ++j) {
                if (used[j])
                    continue;
                const Polyline& p = pieces[j];
                if (almostEqual(chain.points.back(), p.points.front(), tolerance)) {
                    append(chain, p);
                } else if (almostEqual(chain.points.back(), p.points.back(), tolerance)) {
                    append(chain, reversed(p));
                } else if (almostEqual(chain.points.front(), p.points.back(), tolerance)) {
                    Polyline r = p;
                    append(r, chain);
                    chain = r;
                } else if (almostEqual(chain.points.front(), p.points.front(), tolerance)) {
                    Polyline r = reversed(p);
                    append(r, chain);
                    chain = r;
                } else {
                    continue;
                }
                used[j] = grew = true;
                from.push_back(sources[j]);
            }
        }
        if (from.size() < 2)
            continue;
        if (chain.points.size() > 2 && almostEqual(chain.points.front(), chain.points.back(), tolerance)) {
            chain.points.pop_back();
            chain.closed = true;
        }
        chain = simplified(chain);
        if (!chain.closed && chain.points.size() == 2 && std::abs(chain.bulge(0)) < kEps)
            out.push_back({Line{chain.points[0], chain.points[1]}, from});
        else
            out.push_back({chain, from});
    }
    return out;
}

Polyline ellipsePolyline(Vec2 center, Vec2 major, double ratio)
{
    // 16 arcs through points of the ellipse: its area is right to 1 part in 10000.
    constexpr int kSegments = 16;
    const Vec2 minor = perpendicular(major) * ratio;
    auto at = [&](double t) { return center + major * std::cos(t) + minor * std::sin(t); };
    Polyline pl;
    pl.closed = true;
    for (int i = 0; i < kSegments; ++i) {
        const double t0 = kTwoPi * i / kSegments;
        const double t1 = kTwoPi * (i + 1) / kSegments;
        pl.points.push_back(at(t0));
        pl.setBulge(static_cast<size_t>(i), bulgeThrough(at(t0), at((t0 + t1) / 2.0), at(t1)).value_or(0.0));
    }
    return pl;
}

Polyline splinePolyline(const std::vector<Vec2>& fit, bool closed)
{
    Polyline pl;
    pl.closed = closed;
    const size_t n = fit.size();
    if (n < 3) {
        pl.points = fit;
        return pl;
    }
    // Ends of an open curve are mirrored, so it starts and ends on its points.
    auto point = [&](long i) -> Vec2 {
        const long count = static_cast<long>(n);
        if (closed)
            return fit[static_cast<size_t>(((i % count) + count) % count)];
        if (i < 0)
            return fit[0] * 2.0 - fit[1];
        if (i >= count)
            return fit[n - 1] * 2.0 - fit[n - 2];
        return fit[static_cast<size_t>(i)];
    };
    constexpr int kSteps = 12;
    const long spans = static_cast<long>(closed ? n : n - 1);
    for (long s = 0; s < spans; ++s) {
        const Vec2 p0 = point(s - 1), p1 = point(s), p2 = point(s + 1), p3 = point(s + 2);
        // Centripetal parameters: no loops or cusps on uneven spacing.
        const double t0 = 0.0;
        const double t1 = t0 + std::sqrt(std::max(distance(p0, p1), kEps));
        const double t2 = t1 + std::sqrt(std::max(distance(p1, p2), kEps));
        const double t3 = t2 + std::sqrt(std::max(distance(p2, p3), kEps));
        for (int k = 0; k < kSteps; ++k) {
            const double t = t1 + (t2 - t1) * k / kSteps;
            const Vec2 a1 = p0 * ((t1 - t) / (t1 - t0)) + p1 * ((t - t0) / (t1 - t0));
            const Vec2 a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
            const Vec2 a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
            const Vec2 b1 = a1 * ((t2 - t) / (t2 - t0)) + a2 * ((t - t0) / (t2 - t0));
            const Vec2 b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
            pl.points.push_back(b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1)));
        }
    }
    if (!closed)
        pl.points.push_back(fit.back());
    return pl;
}

} // namespace cad
