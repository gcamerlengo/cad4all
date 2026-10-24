// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry.h"

#include <gtest/gtest.h>

using namespace cad;

namespace {

void expectNear(Vec2 a, Vec2 b, double eps = 1e-9)
{
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
}

} // namespace

TEST(Geometry, NormalizeAngle)
{
    EXPECT_NEAR(normalizeAngle(-kPi / 2), 3 * kPi / 2, 1e-12);
    EXPECT_NEAR(normalizeAngle(5 * kPi), kPi, 1e-12);
    EXPECT_GE(normalizeAngle(-1e-18), 0.0);
    EXPECT_LT(normalizeAngle(-1e-18), kTwoPi);
}

TEST(Geometry, AngleInSweepAcrossZero)
{
    // Sweep from 350 to 10 degrees passes through 0.
    const double s = degToRad(350), e = degToRad(10);
    EXPECT_TRUE(angleInSweep(0.0, s, e));
    EXPECT_TRUE(angleInSweep(degToRad(355), s, e));
    EXPECT_FALSE(angleInSweep(degToRad(180), s, e));
    EXPECT_NEAR(sweepAngle(s, e), degToRad(20), 1e-12);
}

TEST(Geometry, SegmentIntersection)
{
    const auto r = intersectSegments({0, 0}, {10, 10}, {0, 10}, {10, 0});
    ASSERT_EQ(r.size(), 1u);
    expectNear(r[0], {5, 5});
    EXPECT_TRUE(intersectSegments({0, 0}, {1, 0}, {0, 1}, {1, 1}).empty()); // parallel
    EXPECT_TRUE(intersectSegments({0, 0}, {1, 1}, {3, 0}, {3, 5}).empty()); // out of range
    // Touching at an endpoint counts.
    EXPECT_EQ(intersectSegments({0, 0}, {5, 0}, {5, 0}, {5, 5}).size(), 1u);
}

TEST(Geometry, SegmentCircleIntersection)
{
    auto r = intersectSegmentCircle({-10, 0}, {10, 0}, {0, 0}, 5);
    ASSERT_EQ(r.size(), 2u);
    expectNear(r[0], {-5, 0});
    expectNear(r[1], {5, 0});
    // Tangent line touches once.
    r = intersectSegmentCircle({-10, 5}, {10, 5}, {0, 0}, 5);
    ASSERT_EQ(r.size(), 1u);
    expectNear(r[0], {0, 5}, 1e-6);
    // Segment entirely inside the circle.
    EXPECT_TRUE(intersectSegmentCircle({-1, 0}, {1, 0}, {0, 0}, 5).empty());
}

TEST(Geometry, CircleCircleIntersection)
{
    auto r = intersectCircles({0, 0}, 5, {8, 0}, 5);
    ASSERT_EQ(r.size(), 2u);
    expectNear(r[0], {4, 3});
    expectNear(r[1], {4, -3});
    EXPECT_EQ(intersectCircles({0, 0}, 5, {10, 0}, 5).size(), 1u); // touching
    EXPECT_TRUE(intersectCircles({0, 0}, 5, {20, 0}, 5).empty());
    EXPECT_TRUE(intersectCircles({0, 0}, 5, {0, 0}, 5).empty());   // concentric
}

TEST(Geometry, Transforms)
{
    expectNear(Transform::translation({3, 4}).apply({1, 1}), {4, 5});
    expectNear(Transform::rotation({1, 0}, kPi / 2).apply({2, 0}), {1, 1});
    expectNear(Transform::scaling({1, 1}, 2).apply({2, 2}), {3, 3});
    const Transform m = Transform::mirror({0, 0}, {0, 10}); // mirror across the Y axis
    expectNear(m.apply({3, 2}), {-3, 2});
    EXPECT_TRUE(m.isMirroring());
    EXPECT_NEAR(Transform::scaling({0, 0}, 3).scaleFactor(), 3.0, 1e-12);
}

TEST(Geometry, BBox)
{
    BBox b = BBox::fromCorners({5, 5}, {0, 0});
    EXPECT_TRUE(b.valid());
    EXPECT_TRUE(b.contains(Vec2{2, 2}));
    EXPECT_TRUE(b.contains(BBox::fromCorners({1, 1}, {2, 2})));
    EXPECT_TRUE(b.intersects(BBox::fromCorners({4, 4}, {9, 9})));
    EXPECT_FALSE(b.intersects(BBox::fromCorners({6, 6}, {9, 9})));
    EXPECT_FALSE(BBox{}.valid());
}
