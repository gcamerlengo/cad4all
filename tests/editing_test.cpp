// SPDX-License-Identifier: GPL-3.0-or-later
#include "editing.h"

#include <gtest/gtest.h>

using namespace cad;

namespace {

void expectNear(Vec2 a, Vec2 b, double eps = 1e-9)
{
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
}

} // namespace

// ---------------------------------------------------------------- offset

TEST(Offset, LineOnPickedSide)
{
    const Line l{{0, 0}, {10, 0}};
    auto up = std::get<Line>(*offsetShape(l, 2, {5, 7}));
    expectNear(up.a, {0, 2});
    expectNear(up.b, {10, 2});
    auto down = std::get<Line>(*offsetShape(l, 2, {5, -1}));
    expectNear(down.a, {0, -2});
}

TEST(Offset, CircleAndArcGrowOrShrink)
{
    EXPECT_DOUBLE_EQ(std::get<Circle>(*offsetShape(Circle{{0, 0}, 5}, 1, {10, 0})).radius, 6.0);
    EXPECT_DOUBLE_EQ(std::get<Circle>(*offsetShape(Circle{{0, 0}, 5}, 1, {1, 0})).radius, 4.0);
    EXPECT_FALSE(offsetShape(Circle{{0, 0}, 5}, 6, {1, 0}));
    const Arc a = std::get<Arc>(*offsetShape(Arc{{0, 0}, 5, 0, kPi}, 2, {0, 10}));
    EXPECT_DOUBLE_EQ(a.radius, 7.0);
    EXPECT_DOUBLE_EQ(a.endAngle, kPi);
}

TEST(Offset, ClosedPolylineInside)
{
    const Polyline square{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true};
    const auto in = std::get<Polyline>(*offsetShape(square, 1, {5, 5}));
    ASSERT_EQ(in.points.size(), 4u);
    EXPECT_TRUE(in.closed);
    expectNear(in.points[0], {1, 1});
    expectNear(in.points[2], {9, 9});

    const auto out = std::get<Polyline>(*offsetShape(square, 1, {5, -3}));
    expectNear(out.points[0], {-1, -1});
    expectNear(out.points[2], {11, 11});
}

TEST(Offset, OpenPolylineCorners)
{
    // An L shape offset to its inner side.
    const Polyline l{{{0, 10}, {0, 0}, {10, 0}}, false};
    const auto r = std::get<Polyline>(*offsetShape(l, 2, {5, 5}));
    ASSERT_EQ(r.points.size(), 3u);
    expectNear(r.points[0], {2, 10});
    expectNear(r.points[1], {2, 2});
    expectNear(r.points[2], {10, 2});
}

// ---------------------------------------------------------------- trim

TEST(Trim, LineBetweenTwoBoundaries)
{
    const Line target{{0, 0}, {10, 0}};
    const std::vector<Shape> bounds{Line{{3, -1}, {3, 1}}, Line{{7, -1}, {7, 1}}};
    const auto r = trimShape(target, bounds, {5, 0});
    ASSERT_TRUE(r.changed);
    ASSERT_EQ(r.pieces.size(), 2u);
    expectNear(std::get<Line>(r.pieces[0]).b, {3, 0});
    expectNear(std::get<Line>(r.pieces[1]).a, {7, 0});

    // Picking the end piece removes only that piece.
    const auto end = trimShape(target, bounds, {9, 0});
    ASSERT_EQ(end.pieces.size(), 1u);
    expectNear(std::get<Line>(end.pieces[0]).b, {7, 0});
}

TEST(Trim, WithoutBoundariesDeletes)
{
    const auto r = trimShape(Line{{0, 0}, {10, 0}}, {Line{{0, 5}, {10, 5}}}, {5, 0});
    EXPECT_TRUE(r.changed);
    EXPECT_TRUE(r.pieces.empty());
}

TEST(Trim, CircleBecomesArc)
{
    // A vertical line through the centre cuts the circle at the top and bottom.
    const auto r = trimShape(Circle{{0, 0}, 5}, {Line{{0, -10}, {0, 10}}}, {5, 0});
    ASSERT_TRUE(r.changed);
    ASSERT_EQ(r.pieces.size(), 1u);
    const Arc a = std::get<Arc>(r.pieces[0]);
    // The right half was picked, so the left half remains: from 90 to 270 degrees.
    EXPECT_NEAR(a.startAngle, kPi / 2, 1e-9);
    EXPECT_NEAR(a.endAngle, 3 * kPi / 2, 1e-9);

    // A single cut cannot trim a circle.
    EXPECT_FALSE(trimShape(Circle{{0, 0}, 5}, {Line{{5, -1}, {5, 1}}}, {0, 5}).changed);
}

TEST(Trim, ArcKeepsOuterParts)
{
    const Arc arc{{0, 0}, 10, 0, kPi};
    const auto r = trimShape(arc, {Line{{0, 0}, {0, 20}}}, polar(10, kPi / 4));
    ASSERT_EQ(r.pieces.size(), 1u);
    const Arc left = std::get<Arc>(r.pieces[0]);
    EXPECT_NEAR(left.startAngle, kPi / 2, 1e-9);
    EXPECT_NEAR(left.endAngle, kPi, 1e-9);
}

TEST(Trim, OpenPolyline)
{
    const Polyline pl{{{0, 0}, {10, 0}, {10, 10}}, false};
    const auto r = trimShape(pl, {Line{{5, -1}, {5, 1}}}, {8, 0});
    ASSERT_EQ(r.pieces.size(), 1u);
    // The part after the cut was picked; the part from the start to the cut remains.
    const auto& kept = std::get<Polyline>(r.pieces[0]);
    ASSERT_EQ(kept.points.size(), 2u);
    expectNear(kept.points[1], {5, 0});

    const auto r2 = trimShape(pl, {Line{{5, -1}, {5, 1}}}, {2, 0});
    ASSERT_EQ(r2.pieces.size(), 1u);
    const auto& rest = std::get<Polyline>(r2.pieces[0]);
    ASSERT_EQ(rest.points.size(), 3u);
    expectNear(rest.points[0], {5, 0});
    expectNear(rest.points[2], {10, 10});
}

TEST(Trim, ClosedPolylineOpensUp)
{
    const Polyline square{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true};
    // A horizontal line crosses the left and right sides at y = 5.
    const auto r = trimShape(square, {Line{{-5, 5}, {15, 5}}}, {5, 10});
    ASSERT_TRUE(r.changed);
    ASSERT_EQ(r.pieces.size(), 1u);
    const auto& u = std::get<Polyline>(r.pieces[0]);
    EXPECT_FALSE(u.closed);
    // What is left is the lower U: from (0,5) down, across and up to (10,5).
    ASSERT_EQ(u.points.size(), 4u);
    expectNear(u.points.front(), {0, 5});
    expectNear(u.points.back(), {10, 5});
}

// ---------------------------------------------------------------- extend

TEST(Extend, LineToBoundary)
{
    const auto r = extendShape(Line{{0, 0}, {5, 0}}, {Line{{10, -5}, {10, 5}}}, {4, 0});
    ASSERT_TRUE(r);
    expectNear(std::get<Line>(*r).b, {10, 0});
    // Nothing in the way: no change.
    EXPECT_FALSE(extendShape(Line{{0, 0}, {5, 0}}, {Line{{10, 1}, {10, 5}}}, {4, 0}));
}

TEST(Extend, NearestStartEnd)
{
    const auto r = extendShape(Line{{0, 0}, {5, 0}}, {Line{{-3, -5}, {-3, 5}}, Line{{10, -5}, {10, 5}}}, {1, 0});
    ASSERT_TRUE(r);
    expectNear(std::get<Line>(*r).a, {-3, 0});
    expectNear(std::get<Line>(*r).b, {5, 0});
}

TEST(Extend, ArcAlongItsCircle)
{
    // Quarter arc from 0 to 90 degrees, extended at its end to the line x = -5.
    const Arc arc{{0, 0}, 10, 0, kPi / 2};
    const auto r = extendShape(arc, {Line{{-5, -20}, {-5, 20}}}, {0, 10});
    ASSERT_TRUE(r);
    const Arc a = std::get<Arc>(*r);
    expectNear(a.endPoint(), {-5, std::sqrt(75.0)}, 1e-9);
    EXPECT_NEAR(a.startAngle, 0.0, 1e-12);
}

TEST(Extend, PolylineLastSegment)
{
    const Polyline pl{{{0, 0}, {5, 0}, {5, 5}}, false};
    const auto r = extendShape(pl, {Line{{0, 12}, {10, 12}}}, {5, 4});
    ASSERT_TRUE(r);
    expectNear(std::get<Polyline>(*r).points.back(), {5, 12});
}

// ---------------------------------------------------------------- fillet

TEST(Fillet, ZeroRadiusMakesCorner)
{
    // Two lines that do not meet: fillet 0 extends both to their corner.
    const auto r = filletLines(Line{{0, 0}, {8, 0}}, {2, 0}, Line{{10, 2}, {10, 10}}, {10, 8}, 0);
    ASSERT_TRUE(r);
    expectNear(r->first.b, {10, 0});
    expectNear(r->second.b, {10, 0});
    EXPECT_FALSE(r->arc);
}

TEST(Fillet, RadiusIsTangent)
{
    const auto r = filletLines(Line{{0, 0}, {10, 0}}, {2, 0}, Line{{10, 0}, {10, 10}}, {10, 8}, 2);
    ASSERT_TRUE(r);
    ASSERT_TRUE(r->arc);
    expectNear(r->first.b, {8, 0});
    expectNear(r->second.b, {10, 2});
    expectNear(r->arc->center, {8, 2});
    EXPECT_NEAR(r->arc->sweep(), kPi / 2, 1e-9);
    // The arc ends on the two tangent points.
    const bool startsOnFirst = almostEqual(r->arc->startPoint(), {8, 0}, 1e-9) || almostEqual(r->arc->endPoint(), {8, 0}, 1e-9);
    EXPECT_TRUE(startsOnFirst);
}

TEST(Fillet, Errors)
{
    FilletError e;
    EXPECT_FALSE(filletLines(Line{{0, 0}, {10, 0}}, {1, 0}, Line{{0, 1}, {10, 1}}, {1, 1}, 1, &e));
    EXPECT_EQ(e, FilletError::Parallel);
    EXPECT_FALSE(filletLines(Line{{0, 0}, {10, 0}}, {2, 0}, Line{{10, 0}, {10, 10}}, {10, 8}, 50, &e));
    EXPECT_EQ(e, FilletError::RadiusTooLarge);
}
