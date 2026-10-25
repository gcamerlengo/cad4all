// SPDX-License-Identifier: GPL-3.0-or-later
#include "section.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace cad;

namespace {

Wall wall(Vec2 a, Vec2 b)
{
    Wall w;
    w.start = a;
    w.end = b;
    w.thickness = 300;
    w.height = 3000;
    return w;
}

BBox boxOf(const Polyline& p)
{
    BBox b;
    for (const Vec2& v : p.points)
        b.add(v);
    return b;
}

} // namespace

TEST(Section, CutsAWallIntoItsProfile)
{
    // A wall along X, cut across at x = 2000 looking towards +X.
    const Model3D model = buildingModel(std::vector<Wall>{wall({0, 0}, {4000, 0})});
    SectionPlane plane;
    plane.origin = {2000, 0};
    plane.direction = {1, 0};
    const SectionDrawing d = sectionDrawing(model, plane);
    ASSERT_EQ(d.cut.size(), 1u);
    const Polyline& p = d.cut[0];
    EXPECT_TRUE(p.closed);
    EXPECT_EQ(p.points.size(), 4u);
    const BBox b = boxOf(p);
    EXPECT_NEAR(b.width(), 300, 1e-6);
    EXPECT_NEAR(b.min.y, 0, 1e-6);
    EXPECT_NEAR(b.max.y, 3000, 1e-6);
    // The wall goes straight away from the viewer: nothing shows beyond
    // except where it coincides with the cut.
    for (const Line& l : d.beyond) {
        EXPECT_GE(std::min(l.a.x, l.b.x), b.min.x - 1e-6);
        EXPECT_LE(std::max(l.a.x, l.b.x), b.max.x + 1e-6);
    }
}

TEST(Section, ElevationHidesWhatIsBehind)
{
    // Two parallel walls, the front one shorter: seen from the front, the
    // back wall shows only above the front one.
    Wall front = wall({0, 0}, {4000, 0});
    front.height = 1000;
    const Wall back = wall({0, 2000}, {4000, 2000});
    const Model3D model = buildingModel(std::vector<Wall>{front, back});
    SectionPlane plane;
    plane.origin = {0, -1000};
    plane.direction = {0, 1};
    const SectionDrawing d = sectionDrawing(model, plane);
    EXPECT_TRUE(d.cut.empty());
    ASSERT_FALSE(d.beyond.empty());
    // x runs to the right of the viewer, looking north: east.
    EXPECT_NEAR(sectionX(plane, {4000, 0}), 4000, 1e-6);
    // The back wall's vertical ends show only between 1000 and 3000.
    double lowest = 1e9;
    for (const Line& l : d.beyond)
        if (std::abs(l.a.x - l.b.x) < 1e-6 && std::abs(l.a.x) < 1e-6 && std::max(l.a.y, l.b.y) > 1500)
            lowest = std::min({lowest, l.a.y, l.b.y});
    EXPECT_NEAR(lowest, 1000, 60);
    // The ground line spans the drawing.
    EXPECT_LE(d.ground.a.x, 0);
    EXPECT_GE(d.ground.b.x, 4000);
}

TEST(Section, LimitedToTheSectionLine)
{
    const Model3D model = buildingModel(std::vector<Wall>{wall({0, 0}, {10000, 0}), wall({5000, 3000}, {9000, 3000})});
    // Across the first wall only, between x 1000 and 3000 of the drawing.
    SectionPlane plane;
    plane.origin = {0, -500};
    plane.direction = {0, 1};
    plane.from = 1000;
    plane.to = 3000;
    const SectionDrawing d = sectionDrawing(model, plane);
    for (const Line& l : d.beyond) {
        EXPECT_GE(std::min(l.a.x, l.b.x), 1000 - 1e-6);
        EXPECT_LE(std::max(l.a.x, l.b.x), 3000 + 1e-6);
    }
}
