// SPDX-License-Identifier: GPL-3.0-or-later
#include "planrecognition.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <utility>

namespace cad {

PlanRecognitionOptions PlanRecognitionOptions::inUnits(double mmPerUnit) const
{
    PlanRecognitionOptions o = *this;
    const double k = 1.0 / mmPerUnit;
    for (double* v : {&o.minThickness, &o.maxThickness, &o.minOpening, &o.maxOpening, &o.wallHeight})
        *v *= k;
    for (Opening* op : {&o.door, &o.window}) {
        op->width *= k;
        op->height *= k;
        op->sill *= k;
    }
    return o;
}

namespace {

constexpr double kAngleTolerance = 1.0 * kPi / 180.0;
const double kSinAngleTolerance = std::sin(kAngleTolerance);

using Interval = std::pair<double, double>;

struct Segment {
    Vec2 a, b;
    Vec2 u;  // unit direction, folded so that parallel lines agree
    double angle = 0.0;
    double len = 0.0;
    std::vector<size_t> sources;
};

struct ArcPiece {
    Arc arc;
    size_t source = 0;
};

void addSegment(std::vector<Segment>& out, Vec2 a, Vec2 b, size_t source)
{
    const double len = distance(a, b);
    if (len < kEps)
        return;
    Vec2 u = (b - a) / len;
    double angle = angleOf(u);
    // Fold into [-tol, pi - tol), so that horizontal lines drawn either way
    // (or with a hair of noise) end up together.
    if (angle < -kAngleTolerance || angle >= kPi - kAngleTolerance) {
        angle += angle < 0.0 ? kPi : -kPi;
        u = -u;
        std::swap(a, b);
    }
    out.push_back({a, b, u, angle, len, {source}});
}

void collect(const std::vector<Shape>& shapes, std::vector<Segment>& segments, std::vector<ArcPiece>& arcs)
{
    for (size_t i = 0; i < shapes.size(); ++i) {
        const Shape& s = shapes[i];
        if (const auto* l = std::get_if<Line>(&s)) {
            addSegment(segments, l->a, l->b, i);
        } else if (const auto* a = std::get_if<Arc>(&s)) {
            arcs.push_back({*a, i});
        } else if (const auto* pl = std::get_if<Polyline>(&s)) {
            for (size_t k = 0; k < pl->segmentCount(); ++k) {
                const Shape part = polylineSegment(*pl, k);
                if (const auto* pline = std::get_if<Line>(&part))
                    addSegment(segments, pline->a, pline->b, i);
                else if (const auto* parc = std::get_if<Arc>(&part))
                    arcs.push_back({*parc, i});
            }
        }
    }
}

template <class T, class AngleOf>
std::vector<std::vector<size_t>> directionGroups(const std::vector<T>& items, AngleOf angleOfItem);

// Collinear segments that touch or overlap become one line: a face drawn in
// pieces (or a window sill continuing it) pairs as a whole.
std::vector<Segment> mergeCollinear(const std::vector<Segment>& raw, double tol)
{
    std::vector<Segment> out;
    for (const auto& group : directionGroups(raw, [](const Segment& s) { return s.angle; })) {
        const Vec2 u = raw[group.front()].u;
        const Vec2 n = perpendicular(u);
        std::vector<size_t> byOffset = group;
        auto offsetOf = [&](size_t k) { return dot(midpoint(raw[k].a, raw[k].b), n); };
        std::sort(byOffset.begin(), byOffset.end(), [&](size_t x, size_t y) { return offsetOf(x) < offsetOf(y); });
        for (size_t from = 0; from < byOffset.size();) {
            size_t to = from + 1;
            while (to < byOffset.size() && offsetOf(byOffset[to]) - offsetOf(byOffset[to - 1]) <= tol / 4.0)
                ++to;
            std::vector<size_t> line(byOffset.begin() + static_cast<std::ptrdiff_t>(from), byOffset.begin() + static_cast<std::ptrdiff_t>(to));
            std::sort(line.begin(), line.end(), [&](size_t x, size_t y) { return dot(raw[x].a, u) < dot(raw[y].a, u); });
            Segment cur = raw[line.front()];
            for (size_t k = 1; k < line.size(); ++k) {
                const Segment& next = raw[line[k]];
                if (dot(next.a, u) <= dot(cur.b, u) + tol && std::abs(cross(cur.u, next.u)) <= kSinAngleTolerance) {
                    if (dot(next.b, u) > dot(cur.b, u))
                        cur.b = next.b;
                    cur.sources.insert(cur.sources.end(), next.sources.begin(), next.sources.end());
                    continue;
                }
                out.push_back(cur);
                cur = next;
            }
            out.push_back(cur);
            from = to;
        }
    }
    for (Segment& s : out) {
        s.len = distance(s.a, s.b);
        s.u = (s.b - s.a) / s.len;
    }
    return out;
}

// Parts of [from, to] outside the `gaps`.
std::vector<Interval> subtract(double from, double to, std::vector<Interval> gaps)
{
    std::sort(gaps.begin(), gaps.end());
    std::vector<Interval> out;
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
    if (x < to)
        out.push_back({x, to});
    return out;
}

// Overlapping or touching intervals joined.
std::vector<Interval> unite(std::vector<Interval> v, double tol)
{
    std::sort(v.begin(), v.end());
    std::vector<Interval> out;
    for (const Interval& i : v) {
        if (!out.empty() && i.first <= out.back().second + tol)
            out.back().second = std::max(out.back().second, i.second);
        else
            out.push_back(i);
    }
    return out;
}

// Indices grouped by direction, each group sorted by angle.
template <class T, class AngleOf>
std::vector<std::vector<size_t>> directionGroups(const std::vector<T>& items, AngleOf angleOfItem)
{
    std::vector<size_t> order(items.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::sort(order.begin(), order.end(), [&](size_t x, size_t y) { return angleOfItem(items[x]) < angleOfItem(items[y]); });
    std::vector<std::vector<size_t>> groups;
    for (size_t k : order) {
        if (groups.empty() || angleOfItem(items[k]) - angleOfItem(items[groups.back().back()]) > kAngleTolerance / 2.0)
            groups.emplace_back();
        groups.back().push_back(k);
    }
    return groups;
}

// Two parallel lines that could be the faces of a wall `d` thick, overlapping
// over [s0, s1] along the first one.
struct Candidate {
    size_t i = 0, j = 0;
    double d = 0.0;
    double s0 = 0.0, s1 = 0.0;
};

void findCandidates(const std::vector<Segment>& segs, double minT, double maxT, double tol, std::vector<Candidate>& out)
{
    for (const auto& group : directionGroups(segs, [](const Segment& s) { return s.angle; })) {
        const Vec2 n = perpendicular(segs[group.front()].u);
        std::vector<std::pair<double, size_t>> byOffset;
        byOffset.reserve(group.size());
        for (size_t k : group)
            byOffset.push_back({dot(midpoint(segs[k].a, segs[k].b), n), k});
        std::sort(byOffset.begin(), byOffset.end());
        for (size_t x = 0; x < byOffset.size(); ++x) {
            const Segment& p = segs[byOffset[x].second];
            const Vec2 pn = perpendicular(p.u);
            for (size_t y = x + 1; y < byOffset.size() && byOffset[y].first - byOffset[x].first <= maxT + 2.0 * tol; ++y) {
                const Segment& q = segs[byOffset[y].second];
                if (std::abs(cross(p.u, q.u)) > kSinAngleTolerance)
                    continue;
                const double da = dot(q.a - p.a, pn);
                const double db = dot(q.b - p.a, pn);
                if (std::abs(da - db) > tol)
                    continue;
                const double d = std::abs(da + db) / 2.0;
                if (d < minT - tol || d > maxT + tol)
                    continue;
                const double ta = dot(q.a - p.a, p.u);
                const double tb = dot(q.b - p.a, p.u);
                const double s0 = std::max(0.0, std::min(ta, tb));
                const double s1 = std::min(p.len, std::max(ta, tb));
                // Longer than thick: the two end caps of a short wall are not a wall.
                if (s1 - s0 < 0.75 * d)
                    continue;
                out.push_back({byOffset[x].second, byOffset[y].second, d, s0, s1});
            }
        }
    }
}

// A straight stretch of wall between two faces: its axis and thickness.
struct Piece {
    Vec2 a, b;
    Vec2 u;
    double angle = 0.0;
    double thickness = 0.0;
    size_t i = 0, j = 0;  // the two faces
};

// The closest pairs first, so every stretch of line is the face of one wall only.
// `closestFirst`: the thinnest pairs first, else the longest; the longest
// keep a wall's faces from pairing with the glass of its windows.
std::vector<Piece> pairFaces(const std::vector<Segment>& segs, std::vector<Candidate> cands, std::vector<int>& faceUsed,
                             bool closestFirst)
{
    std::sort(cands.begin(), cands.end(), [closestFirst](const Candidate& x, const Candidate& y) {
        const double lx = x.s1 - x.s0;
        const double ly = y.s1 - y.s0;
        if (closestFirst ? x.d != y.d : lx != ly)
            return closestFirst ? x.d < y.d : lx > ly;
        return closestFirst ? lx > ly : x.d < y.d;
    });
    std::vector<std::vector<Interval>> used(segs.size());
    std::vector<Piece> pieces;
    for (const Candidate& c : cands) {
        const Segment& p = segs[c.i];
        const Segment& q = segs[c.j];
        std::vector<Interval> blocked = used[c.i];
        for (const auto& [x, y] : used[c.j]) {
            const double from = dot(q.a + q.u * x - p.a, p.u);
            const double to = dot(q.a + q.u * y - p.a, p.u);
            blocked.push_back({std::min(from, to), std::max(from, to)});
        }
        const Vec2 pn = perpendicular(p.u);
        const Vec2 toAxis = pn * ((dot(q.a - p.a, pn) > 0.0 ? 0.5 : -0.5) * c.d);
        for (const auto& [x, y] : subtract(c.s0, c.s1, blocked)) {
            if (y - x < 0.5 * c.d)
                continue;
            const Vec2 from = p.a + p.u * x;
            const Vec2 to = p.a + p.u * y;
            pieces.push_back({from + toAxis, to + toAxis, p.u, p.angle, c.d, c.i, c.j});
            used[c.i].push_back({x, y});
            const double qx = dot(from - q.a, q.u);
            const double qy = dot(to - q.a, q.u);
            used[c.j].push_back({std::min(qx, qy), std::max(qx, qy)});
            ++faceUsed[c.i];
            ++faceUsed[c.j];
        }
    }
    return pieces;
}

// A window frame or its glass pairs with a wall face into a thin "wall"
// filling the opening of a thicker one. Such pieces, held between two pieces
// of the same thicker wall and inside its thickness, are dropped and their
// lines freed, to be read as the window they are.
void dropFramesInOpenings(std::vector<Piece>& pieces, std::vector<int>& faceUsed, double tol)
{
    std::vector<char> drop(pieces.size(), 0);
    for (size_t k = 0; k < pieces.size(); ++k) {
        const Piece& p = pieces[k];
        const Vec2 n = perpendicular(p.u);
        const double op = dot(p.a, n);
        const double p0 = std::min(dot(p.a, p.u), dot(p.b, p.u));
        const double p1 = std::max(dot(p.a, p.u), dot(p.b, p.u));
        std::vector<std::pair<double, double>> before, after;  // offset and thickness of the walls each side
        for (const Piece& q : pieces) {
            if (q.thickness <= p.thickness + tol || std::abs(cross(q.u, p.u)) > kSinAngleTolerance)
                continue;
            const double oq = dot(q.a, n);
            if (op - p.thickness / 2.0 < oq - q.thickness / 2.0 - tol || op + p.thickness / 2.0 > oq + q.thickness / 2.0 + tol)
                continue;
            const double q0 = std::min(dot(q.a, p.u), dot(q.b, p.u));
            const double q1 = std::max(dot(q.a, p.u), dot(q.b, p.u));
            if (std::abs(q1 - p0) <= tol)
                before.push_back({oq, q.thickness});
            if (std::abs(q0 - p1) <= tol)
                after.push_back({oq, q.thickness});
        }
        for (const auto& [ob, tb] : before)
            for (const auto& [oa, ta] : after)
                if (std::abs(ob - oa) <= tol && std::abs(tb - ta) <= tol)
                    drop[k] = 1;
    }
    std::vector<Piece> kept;
    for (size_t k = 0; k < pieces.size(); ++k) {
        if (drop[k]) {
            --faceUsed[pieces[k].i];
            --faceUsed[pieces[k].j];
        } else {
            kept.push_back(pieces[k]);
        }
    }
    pieces = std::move(kept);
}

struct FoundOpening {
    Opening::Kind kind = Opening::Kind::Door;
    double s0 = 0.0, s1 = 0.0;
    bool swingRight = false;
    bool hingeAtEnd = false;
};

// The line of a wall: points are u * s + n * offset.
struct WallLine {
    Vec2 u, n;
    double offset = 0.0;
    double thickness = 0.0;
};

class Recognizer {
public:
    Recognizer(const std::vector<Shape>& shapes, const PlanRecognitionOptions& options)
        : opt_(options), tol_(0.05 * options.minThickness)
    {
        collect(shapes, raw_, arcs_);
        segs_ = mergeCollinear(raw_, tol_);
        faceUsed_.assign(segs_.size(), 0);
        arcUsed_.assign(arcs_.size(), 0);
        sourceUsed_.assign(shapes.size(), 0);
    }

    RecognizedPlan run()
    {
        std::vector<Candidate> cands;
        findCandidates(segs_, opt_.minThickness, opt_.maxThickness, tol_, cands);
        std::vector<Piece> pieces = pairFaces(segs_, std::move(cands), faceUsed_, false);
        dropFramesInOpenings(pieces, faceUsed_, tol_);
        for (size_t k = 0; k < segs_.size(); ++k)
            if (faceUsed_[k])
                use(segs_[k]);

        for (const auto& group : directionGroups(pieces, [](const Piece& p) { return p.angle; }))
            buildLine(pieces, group);
        joinEnds();
        markCovered();

        for (size_t k = 0; k < sourceUsed_.size(); ++k)
            if (sourceUsed_[k])
                result_.used.push_back(k);
        return std::move(result_);
    }

private:
    // Pieces in one direction: split by offset and thickness into wall lines,
    // then joined along each line across joints and openings.
    void buildLine(const std::vector<Piece>& pieces, const std::vector<size_t>& group)
    {
        const Vec2 u = pieces[group.front()].u;
        const Vec2 n = perpendicular(u);
        std::vector<size_t> byOffset = group;
        auto offsetOf = [&](size_t k) { return dot(pieces[k].a, n); };
        std::sort(byOffset.begin(), byOffset.end(), [&](size_t x, size_t y) { return offsetOf(x) < offsetOf(y); });
        for (size_t from = 0; from < byOffset.size();) {
            size_t to = from + 1;
            while (to < byOffset.size() && offsetOf(byOffset[to]) - offsetOf(byOffset[from]) <= tol_)
                ++to;
            std::vector<size_t> line(byOffset.begin() + static_cast<std::ptrdiff_t>(from), byOffset.begin() + static_cast<std::ptrdiff_t>(to));
            std::sort(line.begin(), line.end(), [&](size_t x, size_t y) { return pieces[x].thickness < pieces[y].thickness; });
            for (size_t a = 0; a < line.size();) {
                size_t b = a + 1;
                while (b < line.size() && pieces[line[b]].thickness - pieces[line[a]].thickness <= tol_)
                    ++b;
                std::vector<size_t> same(line.begin() + static_cast<std::ptrdiff_t>(a), line.begin() + static_cast<std::ptrdiff_t>(b));
                joinAlong(pieces, same, u, n);
                a = b;
            }
            from = to;
        }
    }

    void joinAlong(const std::vector<Piece>& pieces, const std::vector<size_t>& same, Vec2 u, Vec2 n)
    {
        WallLine line{u, n, 0.0, 0.0};
        std::vector<Interval> spans;
        for (size_t k : same) {
            const Piece& p = pieces[k];
            line.offset += dot(p.a, n) / static_cast<double>(same.size());
            line.thickness += p.thickness / static_cast<double>(same.size());
            spans.push_back({std::min(dot(p.a, u), dot(p.b, u)), std::max(dot(p.a, u), dot(p.b, u))});
        }
        std::sort(spans.begin(), spans.end());
        double s0 = spans.front().first;
        double s1 = spans.front().second;
        std::vector<FoundOpening> openings;
        for (size_t k = 1; k < spans.size(); ++k) {
            const double g0 = s1;
            const double g1 = spans[k].first;
            const double gap = g1 - g0;
            if (gap <= tol_) {
                s1 = std::max(s1, spans[k].second);
                continue;
            }
            if (gap <= opt_.maxOpening) {
                std::optional<FoundOpening> found = doorIn(line, g0, g1);
                if (!found)
                    found = windowIn(line, g0, g1);
                // A narrow gap with nothing in it is where another wall meets this one.
                if (found || gap <= opt_.maxThickness + tol_) {
                    if (found)
                        openings.push_back(*found);
                    s1 = std::max(s1, spans[k].second);
                    continue;
                }
            }
            finishWall(line, s0, s1, std::move(openings));
            openings.clear();
            s0 = spans[k].first;
            s1 = spans[k].second;
        }
        finishWall(line, s0, s1, std::move(openings));
    }

    // A door swing hinged on one jamb of the gap.
    std::optional<FoundOpening> doorIn(const WallLine& line, double g0, double g1)
    {
        const double gap = g1 - g0;
        const double slack = std::max(2.0 * tol_, 0.15 * gap);
        for (size_t k = 0; k < arcs_.size(); ++k) {
            if (arcUsed_[k])
                continue;
            const Arc& a = arcs_[k].arc;
            const double sweep = a.sweep();
            if (sweep < degToRad(15.0) || sweep > degToRad(100.0) || a.radius < 0.35 * gap || a.radius > 1.25 * gap)
                continue;
            const double sc = dot(a.center, line.u);
            const double oc = dot(a.center, line.n) - line.offset;
            if (std::abs(oc) > line.thickness / 2.0 + tol_)
                continue;
            const bool nearStart = std::abs(sc - g0) <= slack;
            const bool nearEnd = std::abs(sc - g1) <= slack;
            if (!nearStart && !nearEnd)
                continue;
            // The open leaf is the end of the swing farthest from the wall.
            const double o1 = dot(a.startPoint(), line.n) - line.offset;
            const double o2 = dot(a.endPoint(), line.n) - line.offset;
            const double open = std::abs(o1) > std::abs(o2) ? o1 : o2;
            if (std::abs(open) < line.thickness / 2.0 + 0.3 * a.radius)
                continue;
            arcUsed_[k] = 1;
            sourceUsed_[arcs_[k].source] = 1;
            doorArcs_.push_back(k);
            FoundOpening f;
            f.kind = Opening::Kind::Door;
            f.s0 = g0;
            f.s1 = g1;
            f.swingRight = open < 0.0;
            f.hingeAtEnd = std::abs(sc - g1) < std::abs(sc - g0);
            return f;
        }
        return std::nullopt;
    }

    // Loose lines along the wall between its faces: the glass and the frame of a window.
    std::vector<Interval> glass(const WallLine& line, double lo, double hi, bool strictlyInside, std::vector<size_t>& found) const
    {
        std::vector<Interval> spans;
        const double limit = line.thickness / 2.0 + (strictlyInside ? -tol_ : tol_);
        for (size_t k = 0; k < segs_.size(); ++k) {
            const Segment& s = segs_[k];
            if (faceUsed_[k] || std::abs(cross(s.u, line.u)) > kSinAngleTolerance)
                continue;
            if (std::abs(dot(s.a, line.n) - line.offset) >= limit || std::abs(dot(s.b, line.n) - line.offset) >= limit)
                continue;
            const double a = std::max(lo, std::min(dot(s.a, line.u), dot(s.b, line.u)));
            const double b = std::min(hi, std::max(dot(s.a, line.u), dot(s.b, line.u)));
            if (b - a > tol_) {
                spans.push_back({a, b});
                found.push_back(k);
            }
        }
        return unite(spans, tol_);
    }

    std::optional<FoundOpening> windowIn(const WallLine& line, double g0, double g1)
    {
        std::vector<size_t> found;
        double covered = 0.0;
        for (const auto& [a, b] : glass(line, g0, g1, false, found))
            covered += b - a;
        if (covered < 0.5 * (g1 - g0))
            return std::nullopt;
        for (size_t k : found)
            use(segs_[k]);
        FoundOpening f;
        f.kind = Opening::Kind::Window;
        f.s0 = g0;
        f.s1 = g1;
        return f;
    }

    void finishWall(const WallLine& line, double s0, double s1, std::vector<FoundOpening> openings)
    {
        // Windows drawn across a wall whose faces run on: glass between the faces.
        std::vector<Interval> taken;
        for (const FoundOpening& f : openings)
            taken.push_back({f.s0, f.s1});
        std::vector<size_t> found;
        for (const auto& [a, b] : glass(line, s0, s1, true, found)) {
            if (b - a < 0.6 * opt_.minOpening)
                continue;
            bool overlaps = false;
            for (const auto& [x, y] : taken)
                overlaps = overlaps || (a < y && b > x);
            if (overlaps)
                continue;
            FoundOpening f;
            f.kind = Opening::Kind::Window;
            f.s0 = a;
            f.s1 = b;
            openings.push_back(f);
            for (size_t k : found) {
                const double sa = dot(segs_[k].a, line.u);
                const double sb = dot(segs_[k].b, line.u);
                if (std::max(sa, sb) > a - tol_ && std::min(sa, sb) < b + tol_)
                    use(segs_[k]);
            }
        }

        Wall w;
        w.start = line.u * s0 + line.n * line.offset;
        w.end = line.u * s1 + line.n * line.offset;
        w.thickness = line.thickness;
        w.height = opt_.wallHeight;
        std::sort(openings.begin(), openings.end(), [](const FoundOpening& x, const FoundOpening& y) { return x.s0 < y.s0; });
        for (const FoundOpening& f : openings) {
            Opening o = f.kind == Opening::Kind::Door ? opt_.door : opt_.window;
            o.kind = f.kind;
            o.position = (f.s0 + f.s1) / 2.0 - s0;
            o.width = f.s1 - f.s0;
            o.swingRight = f.swingRight;
            o.hingeAtEnd = f.hingeAtEnd;
            w.openings.push_back(o);
            ++(f.kind == Opening::Kind::Door ? result_.doors : result_.windows);
        }
        result_.walls.push_back(std::move(w));
    }

    // Each wall end that stops short of another wall's axis (a corner or a
    // T junction) is carried to that axis, so the walls join.
    void joinEnds()
    {
        std::vector<Wall>& walls = result_.walls;
        const std::vector<Wall> original = walls;
        const double minSin = std::sin(degToRad(10.0));
        for (size_t i = 0; i < original.size(); ++i) {
            const Wall& w = original[i];
            for (bool atStart : {true, false}) {
                const Vec2 e = atStart ? w.start : w.end;
                const Vec2 away = normalized(atStart ? w.start - w.end : w.end - w.start);
                double best = std::numeric_limits<double>::infinity();
                for (size_t j = 0; j < original.size(); ++j) {
                    const Wall& v = original[j];
                    const Vec2 vu = normalized(v.end - v.start);
                    const double s = cross(away, vu);
                    if (j == i || std::abs(s) < minSin)
                        continue;
                    // How far along `away` the end meets the axis of v.
                    const double along = -cross(e - v.start, vu) / s;
                    if (std::abs(along) > v.thickness / 2.0 / std::abs(s) + tol_)
                        continue;
                    const double sv = dot(e + away * along - v.start, vu);
                    const double slack = w.thickness / 2.0 / std::abs(s) + tol_;
                    if (sv < -slack || sv > distance(v.start, v.end) + slack)
                        continue;
                    if (std::abs(along) < std::abs(best))
                        best = along;
                }
                if (!std::isfinite(best))
                    continue;
                Wall& out = walls[i];
                if (atStart) {
                    out.start = e + away * best;
                    // Openings are measured from the start.
                    for (Opening& o : out.openings)
                        o.position += best;
                } else {
                    out.end = e + away * best;
                }
            }
        }
    }

    // Loose lines inside a wall (jambs, end caps, sills) and door leaves.
    void markCovered()
    {
        // The lines as drawn: a jamb and the leaf it touches end to end are two things.
        for (const Segment& s : raw_) {
            if (sourceUsed_[s.sources.front()])
                continue;
            bool covered = false;
            for (const Wall& w : result_.walls) {
                const Vec2 u = normalized(w.end - w.start);
                const Vec2 n = perpendicular(u);
                const double len = distance(w.start, w.end);
                auto inside = [&](Vec2 p) {
                    const double t = dot(p - w.start, u);
                    return t >= -tol_ && t <= len + tol_ && std::abs(dot(p - w.start, n)) <= w.thickness / 2.0 + tol_;
                };
                if (inside(s.a) && inside(s.b)) {
                    covered = true;
                    break;
                }
            }
            for (size_t a : doorArcs_) {
                if (covered)
                    break;
                const Arc& arc = arcs_[a].arc;
                const double slack = std::max(2.0 * tol_, 0.15 * arc.radius);
                const bool fromHinge = distance(s.a, arc.center) <= slack || distance(s.b, arc.center) <= slack;
                covered = fromHinge && s.len >= 0.7 * arc.radius && s.len <= 1.3 * arc.radius;
            }
            if (covered)
                use(s);
        }
    }

    void use(const Segment& s)
    {
        for (size_t k : s.sources)
            sourceUsed_[k] = 1;
    }

    PlanRecognitionOptions opt_;
    double tol_;
    std::vector<Segment> raw_;
    std::vector<Segment> segs_;
    std::vector<ArcPiece> arcs_;
    std::vector<int> faceUsed_;  // how many walls each line is a face of
    std::vector<char> arcUsed_;
    std::vector<char> sourceUsed_;
    std::vector<size_t> doorArcs_;
    RecognizedPlan result_;
};

} // namespace

RecognizedPlan recognizePlan(const std::vector<Shape>& shapes, const PlanRecognitionOptions& options)
{
    return Recognizer(shapes, options).run();
}

double guessPlanUnit(const std::vector<Shape>& shapes)
{
    std::vector<Segment> raw;
    std::vector<ArcPiece> arcs;
    collect(shapes, raw, arcs);
    const std::vector<Segment> segs = mergeCollinear(raw, 1e-6);
    // One pass over every unit at once, so the real walls take their lines
    // before rooms or furniture can pair them in a wrong unit.
    const PlanRecognitionOptions base;
    constexpr std::array<double, 3> units{1.0, 10.0, 1000.0};
    std::vector<Candidate> cands;
    for (double unit : units) {
        const PlanRecognitionOptions o = base.inUnits(unit);
        findCandidates(segs, o.minThickness, o.maxThickness, 0.05 * o.minThickness, cands);
    }
    std::vector<int> faceUsed(segs.size(), 0);
    std::array<double, 3> length{};
    for (const Piece& p : pairFaces(segs, std::move(cands), faceUsed, true)) {
        for (size_t k = 0; k < units.size(); ++k) {
            const PlanRecognitionOptions o = base.inUnits(units[k]);
            if (p.thickness >= 0.9 * o.minThickness && p.thickness <= 1.1 * o.maxThickness)
                length[k] += distance(p.a, p.b) / o.minThickness;  // in wall thicknesses, comparable across units
        }
    }
    const auto best = std::max_element(length.begin(), length.end());
    return *best > 0.0 ? units[static_cast<size_t>(best - length.begin())] : 1.0;
}

} // namespace cad
