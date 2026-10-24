// SPDX-License-Identifier: GPL-3.0-or-later
#include "editing.h"

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

std::vector<Vec2> withoutDuplicates(const std::vector<Vec2>& pts)
{
    std::vector<Vec2> out;
    for (Vec2 p : pts)
        if (out.empty() || !almostEqual(out.back(), p))
            out.push_back(p);
    return out;
}

Vec2 pointAt(const std::vector<Vec2>& pts, double u)
{
    const size_t i = std::min(static_cast<size_t>(std::max(0.0, std::floor(u))), pts.size() - 2);
    return lerp(pts[i], pts[i + 1], u - static_cast<double>(i));
}

std::vector<Vec2> subPolyline(const std::vector<Vec2>& pts, double u0, double u1)
{
    std::vector<Vec2> out{pointAt(pts, u0)};
    for (double k = std::floor(u0) + 1.0; k < u1 - kParamEps; k += 1.0)
        out.push_back(pts[static_cast<size_t>(k)]);
    out.push_back(pointAt(pts, u1));
    return withoutDuplicates(out);
}

// Parameter of the point of the polyline nearest to p; `segments` counts the
// segments to search (n-1 for an open polyline, n for a closed one).
double polylineParam(const std::vector<Vec2>& pts, size_t segments, Vec2 p)
{
    double best = 0.0;
    double bestDist = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < segments; ++i) {
        const Vec2 a = pts[i];
        const Vec2 b = pts[(i + 1) % pts.size()];
        const Vec2 d = b - a;
        const double len2 = dot(d, d);
        const double t = len2 > 0.0 ? std::clamp(dot(p - a, d) / len2, 0.0, 1.0) : 0.0;
        const double dist = distance(p, a + d * t);
        if (dist < bestDist) {
            bestDist = dist;
            best = static_cast<double>(i) + t;
        }
    }
    return best;
}

bool hasLength(const std::vector<Vec2>& pts)
{
    return pts.size() >= 2;
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

std::optional<Shape> offsetPolyline(const Polyline& pl, double dist, Vec2 side)
{
    const std::vector<Vec2> pts = withoutDuplicates(pl.points);
    const bool closed = pl.closed && pts.size() >= 3;
    if (pts.size() < 2)
        return std::nullopt;
    const size_t n = pts.size();
    const size_t segments = closed ? n : n - 1;

    // The side is decided on the segment nearest to the picked point.
    const size_t k = std::min(static_cast<size_t>(polylineParam(pts, segments, side)), segments - 1);
    const Vec2 ka = pts[k];
    const Vec2 kb = pts[(k + 1) % n];
    const double sign = cross(kb - ka, side - ka) >= 0.0 ? 1.0 : -1.0;

    std::vector<Vec2> origin(segments), dir(segments);
    for (size_t i = 0; i < segments; ++i) {
        const Vec2 a = pts[i];
        const Vec2 b = pts[(i + 1) % n];
        dir[i] = b - a;
        origin[i] = a + normalized(perpendicular(dir[i])) * (dist * sign);
    }
    // Each vertex moves to where the two offset segments meeting there cross.
    auto corner = [&](size_t before, size_t after) {
        if (const auto p = intersectLines(origin[before], dir[before], origin[after], dir[after]))
            return *p;
        return origin[after];
    };

    Polyline out;
    out.closed = closed;
    if (closed) {
        for (size_t i = 0; i < n; ++i)
            out.points.push_back(corner((i + segments - 1) % segments, i));
    } else {
        out.points.push_back(origin[0]);
        for (size_t i = 1; i < segments; ++i)
            out.points.push_back(corner(i - 1, i));
        out.points.push_back(origin[segments - 1] + dir[segments - 1]);
    }
    return out;
}

TrimResult trimPolyline(const Polyline& pl, const std::vector<Vec2>& cutPoints, Vec2 pick)
{
    const std::vector<Vec2> pts = withoutDuplicates(pl.points);
    if (!hasLength(pts))
        return {true, {}};
    const bool closed = pl.closed && pts.size() >= 3;
    const size_t n = pts.size();

    if (!closed) {
        const double last = static_cast<double>(n - 1);
        std::vector<double> cuts;
        for (Vec2 v : cutPoints) {
            const double u = polylineParam(pts, n - 1, v);
            if (u > kParamEps && u < last - kParamEps)
                cuts.push_back(u);
        }
        sortUnique(cuts);
        if (cuts.empty())
            return {true, {}};
        const auto [lo, hi] = bracket(cuts, polylineParam(pts, n - 1, pick), 0.0, last);
        TrimResult r{true, {}};
        if (lo > 0.0)
            r.pieces.push_back(Polyline{subPolyline(pts, 0.0, lo), false});
        if (hi < last)
            r.pieces.push_back(Polyline{subPolyline(pts, hi, last), false});
        std::erase_if(r.pieces, [](const Shape& s) { return std::get<Polyline>(s).points.size() < 2; });
        return r;
    }

    const double total = static_cast<double>(n);
    std::vector<double> cuts;
    for (Vec2 v : cutPoints) {
        double u = polylineParam(pts, n, v);
        if (u >= total - kParamEps)
            u = 0.0;
        cuts.push_back(u);
    }
    sortUnique(cuts);
    if (cuts.empty())
        return {true, {}};
    if (cuts.size() == 1)
        return {false, {}};  // one cut cannot open a closed shape into a piece
    const double p = polylineParam(pts, n, pick);
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
    std::vector<Vec2> laps = pts;
    laps.insert(laps.end(), pts.begin(), pts.end());
    laps.push_back(pts.front());
    const double end = before > after ? before : before + total;
    return {true, {Polyline{subPolyline(laps, after, end), false}}};
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

    if (const auto* pl = std::get_if<Polyline>(&shape)) {
        std::vector<Vec2> pts = withoutDuplicates(pl->points);
        if (pl->closed || pts.size() < 2)
            return std::nullopt;
        const size_t n = pts.size();
        const bool atEnd = distance(pick, pts[n - 1]) <= distance(pick, pts[0]);
        const Vec2 from = atEnd ? pts[n - 1] : pts[0];
        const Vec2 dir = normalized(atEnd ? pts[n - 1] - pts[n - 2] : pts[0] - pts[1]);
        const auto hit = firstHit(from, dir, reach, boundaries);
        if (!hit)
            return std::nullopt;
        (atEnd ? pts[n - 1] : pts[0]) = from + dir * *hit;
        return Polyline{pts, false};
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

} // namespace cad
