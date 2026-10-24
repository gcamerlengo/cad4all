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
