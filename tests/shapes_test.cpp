// SPDX-License-Identifier: GPL-3.0-or-later
#include "shapes.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace cad;

namespace {

void expectNear(Vec2 a, Vec2 b, double eps = 1e-9)
{
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
}

bool hasSnap(const std::vector<SnapPoint>& pts, Vec2 p, SnapType type)
{
    return std::any_of(pts.begin(), pts.end(), [&](const SnapPoint& s) { return s.type == type && almostEqual(s.point, p, 1e-9); });
}

} // namespace

TEST(Shapes, ArcBoundingBoxIncludesQuadrants)
{
    // Upper half circle from 0 to 180 degrees.
    const Arc arc{{0, 0}, 10, 0, kPi};
    const BBox b = boundingBox(arc);
    expectNear(b.min, {-10, 0});
    expectNear(b.max, {10, 10});
}

TEST(Shapes, ArcFromThreePoints)
{
    // Counter-clockwise through the top.
    auto arc = arcFromThreePoints({10, 0}, {0, 10}, {-10, 0});
    ASSERT_TRUE(arc);
    expectNear(arc->center, {0, 0});
    EXPECT_NEAR(arc->radius, 10, 1e-9);
    expectNear(arc->startPoint(), {10, 0});
    expectNear(arc->endPoint(), {-10, 0});

    // Clockwise through the top: same arc, stored counter-clockwise.
    arc = arcFromThreePoints({-10, 0}, {0, 10}, {10, 0});
    ASSERT_TRUE(arc);
    expectNear(arc->startPoint(), {10, 0});
    expectNear(arc->endPoint(), {-10, 0});

    EXPECT_FALSE(arcFromThreePoints({0, 0}, {1, 1}, {2, 2}));
}

TEST(Shapes, DistanceToArcUsesEndpointsOutsideSweep)
{
    const Arc arc{{0, 0}, 10, 0, kPi / 2};
    EXPECT_NEAR(distanceTo(arc, {20, 20}), distance({20, 20}, polar(10, kPi / 4)), 1e-9);
    // Below the arc: nearest point is the start point (10, 0).
    EXPECT_NEAR(distanceTo(arc, {10, -5}), 5.0, 1e-9);
}

TEST(Shapes, SnapPoints)
{
    std::vector<SnapPoint> pts;
    collectSnapPoints(Line{{0, 0}, {10, 0}}, SnapEndpoint | SnapMidpoint, pts);
    EXPECT_TRUE(hasSnap(pts, {0, 0}, SnapEndpoint));
    EXPECT_TRUE(hasSnap(pts, {5, 0}, SnapMidpoint));

    pts.clear();
    collectSnapPoints(Arc{{0, 0}, 10, 0, kPi}, SnapMidpoint | SnapCenter | SnapQuadrant, pts);
    EXPECT_TRUE(hasSnap(pts, {0, 10}, SnapMidpoint));
    EXPECT_TRUE(hasSnap(pts, {0, 0}, SnapCenter));
    EXPECT_TRUE(hasSnap(pts, {0, 10}, SnapQuadrant));
    EXPECT_FALSE(hasSnap(pts, {0, -10}, SnapQuadrant));
}

TEST(Shapes, IntersectionsAcrossTypes)
{
    const Polyline square{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true};
    const Circle circle{{10, 10}, 5};
    const auto pts = intersections(square, circle);
    ASSERT_EQ(pts.size(), 2u);

    // The arc only keeps the intersections inside its sweep.
    const Arc upper{{0, 0}, 5, 0, kPi};
    const Line horizontal{{-10, 3}, {10, 3}};
    EXPECT_EQ(intersections(upper, horizontal).size(), 2u);
    const Line below{{-10, -3}, {10, -3}};
    EXPECT_TRUE(intersections(upper, below).empty());
}

TEST(Shapes, MirroredArcKeepsItsPoints)
{
    const Arc arc{{0, 0}, 10, 0, kPi / 2}; // from (10,0) to (0,10)
    const auto m = std::get<Arc>(transformed(arc, Transform::mirror({0, 0}, {0, 1})));
    // Mirrored across the Y axis the arc goes from (0,10) to (-10,0).
    expectNear(m.startPoint(), {0, 10});
    expectNear(m.endPoint(), {-10, 0});
    EXPECT_NEAR(m.sweep(), kPi / 2, 1e-9);
}

TEST(Shapes, ScaledCircle)
{
    const auto c = std::get<Circle>(transformed(Circle{{1, 1}, 2}, Transform::scaling({0, 0}, 3)));
    expectNear(c.center, {3, 3});
    EXPECT_NEAR(c.radius, 6, 1e-12);
}

TEST(Shapes, CrossingBox)
{
    const Line l{{0, 0}, {10, 0}};
    EXPECT_TRUE(crossesBox(l, BBox::fromCorners({5, -1}, {6, 1})));    // crosses the box
    EXPECT_TRUE(crossesBox(l, BBox::fromCorners({-1, -1}, {11, 1})));  // fully inside
    EXPECT_FALSE(crossesBox(l, BBox::fromCorners({0, 1}, {10, 2})));   // above
    // A circle around the box: bounding boxes overlap but the curve does not touch it.
    EXPECT_FALSE(crossesBox(Circle{{0, 0}, 10}, BBox::fromCorners({-1, -1}, {1, 1})));
}

TEST(Shapes, TextBoxFollowsAlignmentAndRotation)
{
    Text t;
    t.position = {10, 0};
    t.height = 2;
    t.content = "ABCD";
    const double w = estimatedTextWidth(t);
    EXPECT_GT(w, 0.0);

    t.align = Text::Align::Center;
    const BBox centered = boundingBox(t);
    EXPECT_NEAR(centered.min.x, 10 - w / 2, 1e-9);
    EXPECT_NEAR(centered.max.y, 2, 1e-9);

    t.align = Text::Align::Left;
    t.rotation = kPi / 2;
    const BBox vertical = boundingBox(t);
    EXPECT_NEAR(vertical.min.x, 8, 1e-9);
    EXPECT_NEAR(vertical.max.y, w, 1e-9);
}

TEST(Shapes, TextIsPickedInsideItsBoxAndSnapsToInsertion)
{
    const Text t{{0, 0}, 5, 0, "Cucina", Text::Align::Left};
    EXPECT_DOUBLE_EQ(distanceTo(t, {3, 2}), 0.0);
    EXPECT_NEAR(distanceTo(t, {3, 8}), 3.0, 1e-9);
    std::vector<SnapPoint> pts;
    collectSnapPoints(t, SnapInsertion | SnapEndpoint, pts);
    ASSERT_EQ(pts.size(), 1u);
    EXPECT_EQ(pts[0].type, SnapInsertion);
    // Text does not cut other objects.
    EXPECT_TRUE(intersections(t, Line{{-10, 1}, {50, 1}}).empty());
    EXPECT_TRUE(crossesBox(t, BBox::fromCorners({1, 1}, {2, 2})));
}

TEST(Shapes, MirroredTextStaysReadable)
{
    const Text t{{10, 0}, 2, 0, "AB", Text::Align::Left};
    const double w = estimatedTextWidth(t);
    // Mirror about the Y axis: the text moves to the left and still reads left to right.
    const Text m = std::get<Text>(transformed(t, Transform::mirror({0, 0}, {0, 1})));
    EXPECT_NEAR(std::cos(m.rotation), 1.0, 1e-9);
    expectNear(m.position, {-10 - w, 0});
    // Mirror about the X axis: the text goes below, upright.
    const Text h = std::get<Text>(transformed(t, Transform::mirror({0, 0}, {1, 0})));
    EXPECT_NEAR(std::cos(h.rotation), 1.0, 1e-9);
    expectNear(h.position, {10, -2});
}

TEST(Shapes, MeasurementFormatUsesDecimalComma)
{
    EXPECT_EQ(formatMeasurement(50.0), "50");
    EXPECT_EQ(formatMeasurement(12.5), "12,5");
    EXPECT_EQ(formatMeasurement(1.0 / 3.0), "0,33");
    EXPECT_EQ(formatMeasurement(-0.001), "0");
}

TEST(Shapes, LinearDimensionGeometry)
{
    Dimension d;
    d.p1 = {0, 0};
    d.p2 = {100, 30};
    d.location = {50, 60};
    d.angle = 0;
    EXPECT_DOUBLE_EQ(d.measurement(), 100.0);
    const DimensionGeometry g = dimensionGeometry(d);
    // Two extension lines and the dimension line at y = 60.
    ASSERT_EQ(g.lines.size(), 3u);
    expectNear(g.lines[2].a, {0, 60});
    expectNear(g.lines[2].b, {100, 60});
    ASSERT_EQ(g.arrows.size(), 2u);
    expectNear(g.arrows[0][0], {0, 60});
    EXPECT_EQ(g.label.content, "100");
    EXPECT_EQ(g.label.align, Text::Align::Center);
    EXPECT_NEAR(g.label.position.x, 50, 1e-9);
    EXPECT_GT(g.label.position.y, 60);

    d.kind = Dimension::Kind::Aligned;
    EXPECT_NEAR(d.measurement(), std::hypot(100.0, 30.0), 1e-9);

    d.kind = Dimension::Kind::Linear;
    d.text = "L = <> cm";
    EXPECT_EQ(dimensionGeometry(d).label.content, "L = 100 cm");
}

TEST(Shapes, VerticalDimensionTextReadsBottomToTop)
{
    Dimension d;
    d.p1 = {0, 0};
    d.p2 = {0, 40};
    d.location = {-20, 0};
    d.angle = kPi / 2;
    const DimensionGeometry g = dimensionGeometry(d);
    EXPECT_NEAR(g.label.rotation, kPi / 2, 1e-9);
    EXPECT_LT(g.label.position.x, -20);  // above the line means to its left
    // Mirroring keeps it consistent: the label is derived again.
    const auto m = std::get<Dimension>(transformed(d, Transform::mirror({0, 0}, {0, 1})));
    EXPECT_DOUBLE_EQ(m.measurement(), 40.0);
}

TEST(Shapes, RadialDimensions)
{
    Dimension r;
    r.kind = Dimension::Kind::Radius;
    r.p1 = {0, 0};
    r.p2 = {10, 0};
    r.location = {20, 0};
    const DimensionGeometry outside = dimensionGeometry(r);
    EXPECT_EQ(outside.label.content, "R10");
    EXPECT_EQ(outside.label.align, Text::Align::Left);
    ASSERT_EQ(outside.arrows.size(), 1u);
    expectNear(outside.arrows[0][0], {10, 0});

    r.kind = Dimension::Kind::Diameter;
    r.location = {2, 0};
    const DimensionGeometry inside = dimensionGeometry(r);
    EXPECT_EQ(inside.label.content, "\xC3\x98" "20");
    ASSERT_EQ(inside.lines.size(), 1u);
    expectNear(inside.lines[0].a, {-10, 0});
    EXPECT_EQ(inside.arrows.size(), 2u);
    // Picked by its label as well as by its lines.
    EXPECT_DOUBLE_EQ(distanceTo(r, inside.label.position + Vec2{0.1, 0.5}), 0.0);
}

TEST(Shapes, DimensionTextMovesOutWhenItDoesNotFit)
{
    Dimension d;
    d.p1 = {0, 0};
    d.p2 = {17.9, 0};
    d.location = {9, -4};
    d.textHeight = 25;
    d.arrowSize = 25;
    const DimensionGeometry g = dimensionGeometry(d);
    // Beyond the right extension line, not across the measured span.
    const auto corners = textCorners(g.label);
    EXPECT_GT(corners[0].x, 17.9);
    // The dimension line runs on under the text.
    EXPECT_GE(g.lines.back().b.x, corners[1].x - 1e-9);
}
