// SPDX-License-Identifier: GPL-3.0-or-later
#include "drawingpainter.h"

#include "hatch.h"

#include <QFontMetricsF>
#include <QPainterPath>

#include <cmath>

using namespace cad;

namespace {

// Pattern lines closer than this many device units are drawn as a tint.
constexpr double kMinPatternSpacing = 2.0;

} // namespace

BBox drawnBox(const Shape& shape)
{
    const BBox box = boundingBox(shape);
    if (const auto* w = std::get_if<Wall>(&shape))
        return box.inflated(4.0 * w->thickness);
    return box;
}

DrawingPainter::DrawingPainter(QPainter& painter, Vec2 center, double scale, QPointF deviceCenter)
    : p_(painter), center_(center), scale_(scale), deviceCenter_(deviceCenter)
{
    visible_ = BBox::fromCorners({-1e300, -1e300}, {1e300, 1e300});
    textFont_.setStyleHint(QFont::SansSerif);
    textFont_.setPixelSize(100);
}

QPainterPath DrawingPainter::polylinePath(const Polyline& pl) const
{
    QPainterPath path;
    if (pl.points.empty())
        return path;
    path.moveTo(toDevice(pl.points.front()));
    for (size_t i = 0; i < pl.segmentCount(); ++i) {
        const Shape seg = polylineSegment(pl, i);
        if (const auto* l = std::get_if<Line>(&seg)) {
            path.lineTo(toDevice(l->b));
            continue;
        }
        const Arc& a = std::get<Arc>(seg);
        const QPointF c = toDevice(a.center);
        const double r = a.radius * scale_;
        const QRectF rect(c.x() - r, c.y() - r, 2 * r, 2 * r);
        // Travel from this vertex: clockwise segments sweep backwards.
        const bool ccw = pl.bulge(i) > 0.0;
        const double from = ccw ? a.startAngle : a.endAngle;
        path.arcTo(rect, radToDeg(from), radToDeg(ccw ? a.sweep() : -a.sweep()));
    }
    if (pl.closed)
        path.closeSubpath();
    return path;
}

void DrawingPainter::drawShape(const Shape& shape)
{
    if (const auto* l = std::get_if<Line>(&shape)) {
        p_.drawLine(toDevice(l->a), toDevice(l->b));
    } else if (const auto* c = std::get_if<Circle>(&shape)) {
        const double r = c->radius * scale_;
        p_.drawEllipse(toDevice(c->center), r, r);
    } else if (const auto* a = std::get_if<Arc>(&shape)) {
        const QPointF c = toDevice(a->center);
        const double r = a->radius * scale_;
        const QRectF rect(c.x() - r, c.y() - r, 2 * r, 2 * r);
        // QPainterPath takes angles in degrees as doubles, so large arcs keep
        // exact endpoints (drawArc rounds to 1/16 of a degree).
        QPainterPath path;
        path.arcMoveTo(rect, radToDeg(a->startAngle));
        path.arcTo(rect, radToDeg(a->startAngle), radToDeg(a->sweep()));
        p_.drawPath(path);
    } else if (const auto* pl = std::get_if<Polyline>(&shape)) {
        if (!pl->hasArcs()) {
            QPolygonF poly;
            poly.reserve(static_cast<qsizetype>(pl->points.size()));
            for (Vec2 v : pl->points)
                poly << toDevice(v);
            if (pl->closed)
                p_.drawPolygon(poly);
            else
                p_.drawPolyline(poly);
            return;
        }
        p_.drawPath(polylinePath(*pl));
    } else if (const auto* t = std::get_if<Text>(&shape)) {
        drawText(*t);
    } else if (const auto* d = std::get_if<Dimension>(&shape)) {
        const DimensionGeometry g = dimensionGeometry(*d);
        for (const Line& l : g.lines)
            p_.drawLine(toDevice(l.a), toDevice(l.b));
        p_.save();
        p_.setBrush(p_.pen().color());
        QPen solid = p_.pen();
        solid.setStyle(Qt::SolidLine);
        p_.setPen(solid);
        for (const auto& arrow : g.arrows)
            p_.drawPolygon(QPolygonF({toDevice(arrow[0]), toDevice(arrow[1]), toDevice(arrow[2])}));
        p_.restore();
        drawText(g.label);
    } else if (const auto* h = std::get_if<Hatch>(&shape)) {
        drawHatch(*h);
    } else if (const auto* w = std::get_if<Wall>(&shape)) {
        drawWall(*w, wallFaces(*w));
    } else if (const auto* sl = std::get_if<Slab>(&shape)) {
        for (const Polyline& loop : sl->loops)
            drawShape(loop);
    } else if (const auto* r = std::get_if<Roof>(&shape)) {
        // Roofs are above the cut of the plan: dashed, as usual in plans.
        p_.save();
        QPen pen = p_.pen();
        pen.setStyle(Qt::DashLine);
        p_.setPen(pen);
        for (const Shape& s : roofPlan(*r))
            drawShape(s);
        p_.restore();
    }
}

void DrawingPainter::drawWall(const Wall& w, const WallFaces& faces)
{
    // The cut parts get a light tint of the wall colour, as walls are
    // usually shown filled in plan.
    QColor tint = p_.pen().color();
    tint.setAlpha(45);
    QPainterPath solid;
    for (const auto& part : wallSolidParts(w, faces)) {
        QPolygonF poly;
        for (Vec2 v : part)
            poly << toDevice(v);
        poly << poly.front();
        solid.addPolygon(poly);
    }
    p_.fillPath(solid, tint);
    for (const Shape& s : wallPlan(w, faces))
        drawShape(s);
}

void DrawingPainter::drawHatch(const Hatch& h)
{
    QPainterPath area;
    area.setFillRule(Qt::OddEvenFill);
    for (const Polyline& loop : h.loops) {
        Polyline closed = loop;
        closed.closed = true;
        area.addPath(polylinePath(closed));
    }
    QColor color = p_.pen().color();
    if (h.solid()) {
        p_.fillPath(area, color);
        return;
    }
    bool dense = hatchSpacing(h) * scale_ < kMinPatternSpacing;
    std::vector<Line> lines;
    if (!dense)
        lines = hatchLines(h, visible_, 200000, &dense);
    if (dense) {
        // Too fine to see the lines: a tint shows where the hatch is.
        color.setAlpha(70);
        p_.fillPath(area, color);
        return;
    }
    for (const Line& l : lines) {
        if (l.a == l.b)
            p_.drawPoint(toDevice(l.a));
        else
            p_.drawLine(toDevice(l.a), toDevice(l.b));
    }
}

void DrawingPainter::drawText(const Text& t)
{
    if (t.content.empty())
        return;
    const double px = t.height * scale_;
    if (px < 1.5) {
        // Too small to read: a line along the baseline shows where it is.
        const auto c = textCorners(t);
        p_.drawLine(toDevice(c[0]), toDevice(c[1]));
        return;
    }
    const QFontMetricsF fm(textFont_, p_.device());
    const QString s = QString::fromStdString(t.content);
    const double w = fm.horizontalAdvance(s);
    const double x = t.align == Text::Align::Left ? 0.0 : t.align == Text::Align::Center ? -w / 2.0 : -w;
    const double k = px / fm.capHeight();
    p_.save();
    p_.translate(toDevice(t.position));
    p_.rotate(-radToDeg(t.rotation));
    p_.scale(k, k);
    p_.setFont(textFont_);
    p_.drawText(QPointF(x, 0.0), s);
    p_.restore();
}
