// SPDX-License-Identifier: GPL-3.0-or-later
#include "hatch.h"

#include <gtest/gtest.h>

using namespace cad;

namespace {

double polygonArea(const Polyline& pl)
{
    double a = 0.0;
    for (const auto& poly : flattenLoops({pl}, kPi / 3600.0))
        for (size_t i = 0; i < poly.size(); ++i)
            a += cross(poly[i], poly[(i + 1) % poly.size()]);
    return std::abs(a) / 2.0;
}

} // namespace

TEST(Hatch, BoundaryOfRoomMadeOfLines)
{
    // Four walls drawn as separate lines that run past the corners, a
    // dividing wall, and a loose line inside that does not close anything.
    const std::vector<Shape> shapes{
        Line{{-1, 0}, {11, 0}}, Line{{10, -1}, {10, 6}}, Line{{11, 5}, {-1, 5}}, Line{{0, 6}, {0, -1}},
        Line{{6, 0}, {6, 5}},   Line{{2, 1}, {3, 2}},
    };
    const auto left = findHatchBoundary(shapes, {3, 3});
    ASSERT_EQ(left.size(), 1u);
    EXPECT_EQ(left[0].points.size(), 4u);
    EXPECT_NEAR(polygonArea(left[0]), 30.0, 1e-9);
    const auto right = findHatchBoundary(shapes, {8, 3});
    ASSERT_EQ(right.size(), 1u);
    EXPECT_NEAR(polygonArea(right[0]), 20.0, 1e-9);
    EXPECT_TRUE(findHatchBoundary(shapes, {20, 3}).empty());
}

TEST(Hatch, IslandsAndArcs)
{
    // A room with a rounded side (half circle of radius 2.5) and a column inside.
    const std::vector<Shape> shapes{
        Polyline{{{0, 0}, {10, 0}, {10, 5}, {0, 5}}, true, {0, 1, 0, 0}},
        Circle{{4, 2.5}, 1},
    };
    const auto loops = findHatchBoundary(shapes, {1, 1});
    ASSERT_EQ(loops.size(), 2u);
    EXPECT_NEAR(polygonArea(loops[0]), 50.0 + kPi * 2.5 * 2.5 / 2.0, 1e-4);
    EXPECT_TRUE(loops[0].hasArcs());
    EXPECT_NEAR(polygonArea(loops[1]), kPi, 1e-6);
    const Hatch h = makeHatch(loops, "SOLID", 1, 0);
    EXPECT_TRUE(insideHatch(h, {1, 1}));
    EXPECT_FALSE(insideHatch(h, {4, 2.5}));
    EXPECT_TRUE(insideHatch(h, {12, 2.5}));
    // Inside the column the column itself is the region.
    const auto column = findHatchBoundary(shapes, {4, 2.5});
    ASSERT_EQ(column.size(), 1u);
    EXPECT_NEAR(polygonArea(column[0]), kPi, 1e-6);
}

TEST(Hatch, ArcCrossingALine)
{
    // A line cut off by an arc: the boundary follows the true arc.
    const std::vector<Shape> shapes{
        Line{{0, 0}, {10, 0}}, Line{{0, 0}, {0, 10}}, Arc{{0, 0}, 5, 0, kPi / 2},
    };
    const auto loops = findHatchBoundary(shapes, {1, 1});
    ASSERT_EQ(loops.size(), 1u);
    EXPECT_NEAR(polygonArea(loops[0]), kPi * 25 / 4, 1e-4);
    EXPECT_EQ(loops[0].points.size(), 3u);
}

TEST(Hatch, PatternLinesAreClipped)
{
    // ANSI31 at scale 1: 45 degree lines 3.175 apart.
    const Hatch h = makeHatch({Polyline{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}, true}}, "ANSI31", 1, 0);
    EXPECT_NEAR(hatchSpacing(h), 3.175, 1e-12);
    const BBox all = BBox::fromCorners({-1e9, -1e9}, {1e9, 1e9});
    const auto lines = hatchLines(h, all);
    EXPECT_NEAR(static_cast<double>(lines.size()), 100.0 * std::sqrt(2.0) / 3.175, 1.0);
    for (const Line& l : lines) {
        EXPECT_NEAR(l.b.y - l.a.y, l.b.x - l.a.x, 1e-9);
        EXPECT_GE(std::min(l.a.x, l.b.x), -1e-9);
        EXPECT_LE(std::max(l.a.y, l.b.y), 100 + 1e-9);
    }
    bool dense = false;
    EXPECT_TRUE(hatchLines(h, all, 10, &dense).empty());
    EXPECT_TRUE(dense);
    // A hole is left empty.
    Hatch holed = h;
    holed.loops.push_back(Polyline{{{40, 40}, {60, 40}, {60, 60}, {40, 60}}, true});
    for (const Line& l : hatchLines(holed, all))
        EXPECT_FALSE(BBox::fromCorners({40.01, 40.01}, {59.99, 59.99}).contains(midpoint(l.a, l.b)));
}

TEST(Hatch, SolidAsTrapezoids)
{
    const Hatch h = makeHatch({Polyline{{{0, 0}, {10, 0}, {5, 10}}, true}}, "SOLID", 1, 0);
    double area = 0.0;
    for (const auto& q : hatchTrapezoids(h))
        area += ((q[1].x - q[0].x) + (q[2].x - q[3].x)) / 2.0 * (q[3].y - q[0].y);
    EXPECT_NEAR(area, 50.0, 1e-9);
}

TEST(Hatch, NestedIslandsStayEmpty)
{
    // Wall ring between two rectangles; a table inside the inner one.
    const std::vector<Shape> shapes{
        Polyline{{{0, 0}, {20, 0}, {20, 10}, {0, 10}}, true},
        Polyline{{{1, 1}, {19, 1}, {19, 9}, {1, 9}}, true},
        Circle{{10, 5}, 2},
    };
    const auto loops = findHatchBoundary(shapes, {0.5, 0.5});
    ASSERT_EQ(loops.size(), 2u);
    const Hatch h = makeHatch(loops, "SOLID", 1, 0);
    EXPECT_TRUE(insideHatch(h, {0.5, 5}));
    EXPECT_FALSE(insideHatch(h, {5, 5}));
    EXPECT_FALSE(insideHatch(h, {10, 5}));
}
