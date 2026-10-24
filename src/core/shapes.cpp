// SPDX-License-Identifier: GPL-3.0-or-later
#include "shapes.h"

#include "hatch.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

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
                       for (size_t i = 0; i < pl.segmentCount(); ++i) {
                           const Shape seg = polylineSegment(pl, i);
                           if (const auto* l = std::get_if<Line>(&seg)) {
                               out.push_back(Piece::segment(l->a, l->b));
                           } else {
                               const Arc& a = std::get<Arc>(seg);
                               Piece p;
                               p.isSegment = false;
                               p.center = a.center;
                               p.radius = a.radius;
                               p.start = a.startAngle;
                               p.end = a.endAngle;
                               out.push_back(p);
                           }
                       }
                   },
                   // Text is not a curve: it never cuts or snaps to other objects.
                   [&](const Text&) {},
                   [&](const Dimension& d) {
                       for (const Line& l : dimensionGeometry(d).lines)
                           out.push_back(Piece::segment(l.a, l.b));
                   },
                   // Nor is a hatch: its outline usually lies on walls that
                   // must stay the cutting edges.
                   [&](const Hatch&) {},
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

// Average advance of a character relative to the text height, measured on
// common sans-serif fonts (capital height about 0.72 of the em).
constexpr double kCharWidth = 0.8;

bool insideText(const Text& text, Vec2 p)
{
    const auto c = textCorners(text);
    const Vec2 d = c[1] - c[0];
    const Vec2 up = c[3] - c[0];
    const double along = dot(p - c[0], d);
    const double across = dot(p - c[0], up);
    return along >= 0.0 && along <= dot(d, d) && across >= 0.0 && across <= dot(up, up);
}

Polyline textOutline(const Text& text)
{
    const auto c = textCorners(text);
    return Polyline{{c[0], c[1], c[2], c[3]}, true};
}

double distanceToText(const Text& text, Vec2 p)
{
    return insideText(text, p) ? 0.0 : distanceTo(textOutline(text), p);
}

size_t codePoints(const std::string& s)
{
    return static_cast<size_t>(std::count_if(s.begin(), s.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

// Direction for text along `dir` that reads left to right (or bottom to top).
Vec2 readable(Vec2 dir)
{
    if (dir.x < -1e-9 || (std::abs(dir.x) <= 1e-9 && dir.y < 0.0))
        return dir * -1.0;
    return dir;
}

std::array<Vec2, 3> arrowHead(Vec2 tip, Vec2 dir, double size)
{
    // ISO closed filled arrow: length `size`, width a third of it.
    const Vec2 back = tip - dir * size;
    const Vec2 side = perpendicular(dir) * (size / 6.0);
    return {tip, back + side, back - side};
}

std::string labelText(const Dimension& d)
{
    std::string value = formatMeasurement(d.measurement());
    if (d.kind == Dimension::Kind::Radius)
        value = "R" + value;
    else if (d.kind == Dimension::Kind::Diameter)
        value = "\xC3\x98" + value;  // Ø in UTF-8
    if (d.text.empty())
        return value;
    std::string out = d.text;
    for (size_t pos = out.find("<>"); pos != std::string::npos; pos = out.find("<>", pos + value.size()))
        out.replace(pos, 2, value);
    return out;
}

void linearGeometry(const Dimension& d, DimensionGeometry& g)
{
    Vec2 dir = d.kind == Dimension::Kind::Aligned ? normalized(d.p2 - d.p1) : polar(1.0, d.angle);
    if (length(dir) < kEps)
        dir = {1.0, 0.0};
    const Vec2 n = perpendicular(dir);
    const Vec2 q1 = d.p1 + n * dot(d.location - d.p1, n);
    const Vec2 q2 = d.p2 + n * dot(d.location - d.p2, n);
    const double gap = d.arrowSize * 0.25;     // between object and extension line
    const double beyond = d.arrowSize * 0.5;   // extension past the dimension line

    for (const auto& [from, to] : {std::pair{d.p1, q1}, std::pair{d.p2, q2}}) {
        const double len = distance(from, to);
        if (len > gap) {
            const Vec2 u = (to - from) * (1.0 / len);
            g.lines.push_back(Line{from + u * gap, to + u * beyond});
        }
    }

    const double span = distance(q1, q2);
    const bool arrowsOutside = span < 2.5 * d.arrowSize;
    if (span < kEps) {
        g.lines.push_back(Line{q1, q2});
    } else {
        const Vec2 u = (q2 - q1) * (1.0 / span);
        if (!arrowsOutside) {
            g.lines.push_back(Line{q1, q2});
            g.arrows.push_back(arrowHead(q1, u * -1.0, d.arrowSize));
            g.arrows.push_back(arrowHead(q2, u, d.arrowSize));
        } else {
            // Not enough room: arrows outside, pointing in.
            g.lines.push_back(Line{q1 - u * (2.0 * d.arrowSize), q2 + u * (2.0 * d.arrowSize)});
            g.arrows.push_back(arrowHead(q1, u, d.arrowSize));
            g.arrows.push_back(arrowHead(q2, u * -1.0, d.arrowSize));
        }
    }

    const Vec2 rd = readable(dir);
    g.label.align = Text::Align::Center;
    g.label.height = d.textHeight;
    g.label.rotation = angleOf(rd);
    const Vec2 lift = perpendicular(rd) * (d.textHeight * 0.25);
    const double w = estimatedTextWidth(g.label);
    if (span >= w + 2.0 * d.arrowSize) {
        g.label.position = midpoint(q1, q2) + lift;
        return;
    }
    // The text does not fit between the extension lines: it goes beside the
    // one further along the reading direction, over an extended dimension line.
    const Vec2 far = dot(q2 - q1, rd) >= 0.0 ? q2 : q1;
    const double start = (arrowsOutside ? 2.0 * d.arrowSize : 0.0) + d.textHeight * 0.5;
    g.lines.push_back(Line{far, far + rd * (start + w)});
    g.label.position = far + rd * (start + w / 2.0) + lift;
}

void radialGeometry(const Dimension& d, DimensionGeometry& g)
{
    const Vec2 c = d.p1;
    const double r = distance(d.p1, d.p2);
    Vec2 u = normalized(d.p2 - c);
    if (length(u) < kEps)
        u = {1.0, 0.0};
    const Vec2 onCurve = c + u * r;
    const Vec2 opposite = c - u * r;
    const bool diameter = d.kind == Dimension::Kind::Diameter;
    g.label.height = d.textHeight;

    if (distance(d.location, c) > r + kEps) {
        // Label outside: leader from the curve, short horizontal landing, text.
        const double side = d.location.x >= c.x ? 1.0 : -1.0;
        const Vec2 landingEnd = d.location + Vec2{side * d.arrowSize, 0.0};
        g.lines.push_back(Line{onCurve, d.location});
        g.lines.push_back(Line{d.location, landingEnd});
        g.arrows.push_back(arrowHead(onCurve, u * -1.0, d.arrowSize));
        g.label.align = side > 0.0 ? Text::Align::Left : Text::Align::Right;
        g.label.position = landingEnd + Vec2{side * d.textHeight * 0.25, -d.textHeight / 2.0};
        return;
    }

    // Label inside: the dimension line runs across (diameter) or from the centre.
    g.lines.push_back(Line{diameter ? opposite : c, onCurve});
    g.arrows.push_back(arrowHead(onCurve, u, d.arrowSize));
    if (diameter)
        g.arrows.push_back(arrowHead(opposite, u * -1.0, d.arrowSize));
    g.label.align = Text::Align::Center;
    g.label.position = d.location + Vec2{0.0, d.textHeight * 0.25};
}

} // namespace

void Polyline::setBulge(size_t i, double b)
{
    if (b == 0.0 && i >= bulges.size())
        return;
    if (bulges.size() <= i)
        bulges.resize(i + 1, 0.0);
    bulges[i] = b;
}

size_t Polyline::segmentCount() const
{
    const size_t n = points.size();
    if (n < 2)
        return 0;
    return closed ? n : n - 1;
}

bool Polyline::hasArcs() const
{
    return std::any_of(bulges.begin(), bulges.end(), [](double b) { return std::abs(b) > 1e-12; });
}

Arc arcFromBulge(Vec2 from, Vec2 to, double bulge)
{
    const Vec2 chord = to - from;
    // Centre on the chord's perpendicular bisector; positive bulges put it
    // on the left, so the arc runs counter-clockwise.
    const Vec2 center = midpoint(from, to) + perpendicular(chord) * ((1.0 - bulge * bulge) / (4.0 * bulge));
    Arc a;
    a.center = center;
    a.radius = distance(center, from);
    const double s = normalizeAngle(angleOf(from - center));
    const double e = normalizeAngle(angleOf(to - center));
    a.startAngle = bulge > 0.0 ? s : e;
    a.endAngle = bulge > 0.0 ? e : s;
    return a;
}

namespace {

struct SegmentArc {
    Vec2 center;
    double radius;
    double start;  // angle of the first vertex
    double sweep;  // signed
};

std::optional<SegmentArc> segmentArc(const Polyline& pl, size_t i)
{
    const double b = pl.bulge(i);
    const Vec2 a = pl.points[i];
    const Vec2 e = pl.points[(i + 1) % pl.points.size()];
    if (std::abs(b) <= 1e-12 || almostEqual(a, e))
        return std::nullopt;
    const Arc arc = arcFromBulge(a, e, b);
    return SegmentArc{arc.center, arc.radius, angleOf(a - arc.center), 4.0 * std::atan(b)};
}

} // namespace

Shape polylineSegment(const Polyline& pl, size_t i)
{
    const Vec2 a = pl.points[i];
    const Vec2 e = pl.points[(i + 1) % pl.points.size()];
    if (std::abs(pl.bulge(i)) <= 1e-12 || almostEqual(a, e))
        return Line{a, e};
    return arcFromBulge(a, e, pl.bulge(i));
}

Vec2 segmentPoint(const Polyline& pl, size_t i, double t)
{
    if (const auto arc = segmentArc(pl, i))
        return arc->center + polar(arc->radius, arc->start + arc->sweep * t);
    const Vec2 a = pl.points[i];
    const Vec2 e = pl.points[(i + 1) % pl.points.size()];
    return a + (e - a) * t;
}

double segmentParam(const Polyline& pl, size_t i, Vec2 p)
{
    if (const auto arc = segmentArc(pl, i)) {
        const double d = angleOf(p - arc->center) - arc->start;
        const double along = normalizeAngle(arc->sweep > 0.0 ? d : -d);
        const double sweep = std::abs(arc->sweep);
        if (along <= sweep)
            return along / sweep;
        // Outside the arc: the nearer end.
        return (along - sweep) < (kTwoPi - along) ? 1.0 : 0.0;
    }
    const Vec2 a = pl.points[i];
    const Vec2 d = pl.points[(i + 1) % pl.points.size()] - a;
    const double len2 = dot(d, d);
    return len2 > 0.0 ? std::clamp(dot(p - a, d) / len2, 0.0, 1.0) : 0.0;
}

Vec2 segmentTangent(const Polyline& pl, size_t i, double t)
{
    if (const auto arc = segmentArc(pl, i)) {
        const Vec2 radial = polar(1.0, arc->start + arc->sweep * t);
        return arc->sweep > 0.0 ? perpendicular(radial) : perpendicular(radial) * -1.0;
    }
    return normalized(pl.points[(i + 1) % pl.points.size()] - pl.points[i]);
}

double Dimension::measurement() const
{
    switch (kind) {
    case Kind::Linear: return std::abs(dot(p2 - p1, polar(1.0, angle)));
    case Kind::Aligned: return distance(p1, p2);
    case Kind::Radius: return distance(p1, p2);
    case Kind::Diameter: return 2.0 * distance(p1, p2);
    }
    return 0.0;
}

std::string formatMeasurement(double value)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(2) << value;
    std::string s = out.str();
    while (!s.empty() && s.back() == '0')
        s.pop_back();
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    if (s == "-0")
        s = "0";
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

double estimatedTextWidth(const Text& text)
{
    return static_cast<double>(codePoints(text.content)) * text.height * kCharWidth;
}

std::array<Vec2, 4> textCorners(const Text& text)
{
    const Vec2 d = polar(1.0, text.rotation);
    const Vec2 up = perpendicular(d) * text.height;
    const double w = estimatedTextWidth(text);
    const double shift = text.align == Text::Align::Left ? 0.0 : text.align == Text::Align::Center ? w / 2.0 : w;
    const Vec2 start = text.position - d * shift;
    return {start, start + d * w, start + d * w + up, start + up};
}

DimensionGeometry dimensionGeometry(const Dimension& dim)
{
    DimensionGeometry g;
    g.label.content = labelText(dim);
    if (dim.kind == Dimension::Kind::Linear || dim.kind == Dimension::Kind::Aligned)
        linearGeometry(dim, g);
    else
        radialGeometry(dim, g);
    return g;
}

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
                       for (size_t i = 0; i < pl.segmentCount(); ++i)
                           if (std::abs(pl.bulge(i)) > 1e-12)
                               box.add(boundingBox(polylineSegment(pl, i)));
                   },
                   [&](const Text& t) {
                       for (Vec2 p : textCorners(t))
                           box.add(p);
                   },
                   [&](const Dimension& d) {
                       const DimensionGeometry g = dimensionGeometry(d);
                       for (const Line& l : g.lines) {
                           box.add(l.a);
                           box.add(l.b);
                       }
                       for (const auto& arrow : g.arrows)
                           for (Vec2 p : arrow)
                               box.add(p);
                       for (Vec2 p : textCorners(g.label))
                           box.add(p);
                   },
                   [&](const Hatch& h) {
                       for (const Polyline& loop : h.loops)
                           box.add(boundingBox(loop));
                   },
               },
               shape);
    return box;
}

Vec2 nearestPoint(const Shape& shape, Vec2 p)
{
    if (const auto* t = std::get_if<Text>(&shape))
        return t->position;
    if (const auto* h = std::get_if<Hatch>(&shape)) {
        Vec2 best = p;
        double bestDist = std::numeric_limits<double>::infinity();
        for (const Polyline& loop : h->loops) {
            const Vec2 q = nearestPoint(loop, p);
            if (distance(p, q) < bestDist) {
                bestDist = distance(p, q);
                best = q;
            }
        }
        return best;
    }
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
    // Text is picked anywhere inside its box, a dimension also by its label.
    if (const auto* t = std::get_if<Text>(&shape))
        return distanceToText(*t, p);
    const double d = distance(p, nearestPoint(shape, p));
    if (const auto* h = std::get_if<Hatch>(&shape)) {
        // Picked on its outline, on a pattern line, or anywhere in a solid fill.
        if (!insideHatch(*h, p))
            return d;
        if (h->solid())
            return 0.0;
        const double r = std::min(d, hatchSpacing(*h) * 1.5);
        double best = d;
        for (const Line& l : hatchLines(*h, BBox::fromCorners(p - Vec2{r, r}, p + Vec2{r, r}), 1000))
            best = std::min(best, distanceToSegment(p, l.a, l.b));
        return best;
    }
    if (const auto* dim = std::get_if<Dimension>(&shape))
        return std::min(d, distanceToText(dimensionGeometry(*dim).label, p));
    return d;
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
                              Polyline r = pl;
                              for (Vec2& p : r.points)
                                  p = t.apply(p);
                              // A mirror turns counter-clockwise arcs clockwise.
                              if (t.isMirroring())
                                  for (double& b : r.bulges)
                                      b = -b;
                              return r;
                          },
                          [&](const Text& tx) -> Shape {
                              Text r = tx;
                              r.height = tx.height * t.scaleFactor();
                              const Vec2 d = polar(1.0, tx.rotation);
                              const Vec2 nd = normalized(t.applyVector(d));
                              r.rotation = angleOf(nd);
                              r.position = t.apply(tx.position);
                              if (t.isMirroring()) {
                                  // Keep the text readable (AutoCAD MIRRTEXT = 0): the
                                  // mirrored box is reused, read from its other side.
                                  if (dot(nd, d) >= 0.0) {
                                      r.position = t.apply(tx.position + perpendicular(d) * tx.height);
                                  } else {
                                      const double w = estimatedTextWidth(tx);
                                      const double shift = tx.align == Text::Align::Left ? w : tx.align == Text::Align::Right ? -w : 0.0;
                                      r.position = t.apply(tx.position + d * shift);
                                      r.rotation = angleOf(nd * -1.0);
                                  }
                              }
                              return r;
                          },
                          [&](const Dimension& dim) -> Shape {
                              Dimension r = dim;
                              r.p1 = t.apply(dim.p1);
                              r.p2 = t.apply(dim.p2);
                              r.location = t.apply(dim.location);
                              r.angle = angleOf(t.applyVector(polar(1.0, dim.angle)));
                              r.textHeight = dim.textHeight * t.scaleFactor();
                              r.arrowSize = dim.arrowSize * t.scaleFactor();
                              return r;
                          },
                          [&](const Hatch& h) -> Shape {
                              Hatch r = h;
                              for (Polyline& loop : r.loops)
                                  loop = std::get<Polyline>(transformed(loop, t));
                              r.origin = t.apply(h.origin);
                              r.scale = h.scale * t.scaleFactor();
                              // A mirrored hatch keeps the direction of its
                              // pattern (AutoCAD MIRRHATCH = 0).
                              if (!t.isMirroring())
                                  r.angle = angleOf(t.applyVector(polar(1.0, h.angle)));
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
                       for (Vec2 p : pl.points)
                           add(p, SnapEndpoint);
                       for (size_t i = 0; i < pl.segmentCount(); ++i) {
                           add(segmentPoint(pl, i, 0.5), SnapMidpoint);
                           if (std::abs(pl.bulge(i)) > 1e-12)
                               add(std::get<Arc>(polylineSegment(pl, i)).center, SnapCenter);
                       }
                   },
                   [&](const Text& t) { add(t.position, SnapInsertion); },
                   [&](const Dimension& d) {
                       for (const Line& l : dimensionGeometry(d).lines) {
                           add(l.a, SnapEndpoint);
                           add(l.b, SnapEndpoint);
                       }
                   },
                   [&](const Hatch&) {},
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
    if (const auto* t = std::get_if<Text>(&shape))
        return crossesBox(textOutline(*t), box) || insideText(*t, box.center());
    if (const auto* d = std::get_if<Dimension>(&shape)) {
        const DimensionGeometry g = dimensionGeometry(*d);
        return std::any_of(g.lines.begin(), g.lines.end(), [&](const Line& l) { return crossesBox(l, box); })
            || crossesBox(g.label, box);
    }
    if (const auto* h = std::get_if<Hatch>(&shape)) {
        return std::any_of(h->loops.begin(), h->loops.end(), [&](const Polyline& l) { return crossesBox(l, box); })
            || insideHatch(*h, box.center());
    }
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
