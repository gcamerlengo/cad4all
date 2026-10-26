// SPDX-License-Identifier: GPL-3.0-or-later
#include "building.h"
#include "planrecognition.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace cad;

namespace {

void line(std::vector<Shape>& out, double x1, double y1, double x2, double y2, double k = 1.0)
{
    out.push_back(Line{{x1 * k, y1 * k}, {x2 * k, y2 * k}});
}

// A 6 x 4 m house in millimetres (scaled by k): 300 mm outer walls, a 100 mm
// partition meeting them in T, a door with its swing in the bottom wall, a
// window drawn in a gap of the right wall and one drawn across the top wall.
std::vector<Shape> house(double k = 1.0)
{
    std::vector<Shape> s;
    // Bottom wall, door from x 1000 to 1900.
    line(s, 0, 0, 1000, 0, k);
    line(s, 1900, 0, 6000, 0, k);
    line(s, 300, 300, 1000, 300, k);
    line(s, 1900, 300, 3000, 300, k);
    line(s, 3100, 300, 5700, 300, k);
    line(s, 1000, 0, 1000, 300, k);
    line(s, 1900, 0, 1900, 300, k);
    s.push_back(Arc{{1000 * k, 300 * k}, 900 * k, 0.0, kPi / 2.0});  // hinge on the left jamb, opens inwards
    line(s, 1000, 300, 1000, 1200, k);                                // the leaf
    // Right wall, window gap from y 1500 to 2700 with its glass.
    line(s, 6000, 0, 6000, 1500, k);
    line(s, 6000, 2700, 6000, 4000, k);
    line(s, 5700, 300, 5700, 1500, k);
    line(s, 5700, 2700, 5700, 3700, k);
    line(s, 5700, 1500, 6000, 1500, k);
    line(s, 5700, 2700, 6000, 2700, k);
    line(s, 5850, 1500, 5850, 2700, k);
    // Top wall, faces running on, glass from x 1000 to 2200.
    line(s, 0, 4000, 6000, 4000, k);
    line(s, 300, 3700, 3000, 3700, k);
    line(s, 3100, 3700, 5700, 3700, k);
    line(s, 1000, 3830, 2200, 3830, k);
    line(s, 1000, 3870, 2200, 3870, k);
    // Left wall.
    line(s, 0, 0, 0, 4000, k);
    line(s, 300, 300, 300, 3700, k);
    // Partition.
    line(s, 3000, 300, 3000, 3700, k);
    line(s, 3100, 300, 3100, 3700, k);
    // A table in the room: not a wall.
    s.push_back(Circle{{4500 * k, 2000 * k}, 400 * k});
    return s;
}

const Wall* wallThrough(const RecognizedPlan& plan, Vec2 p)
{
    for (const Wall& w : plan.walls) {
        const double t = wallParam(w, p);
        const double o = dot(p - w.start, wallNormal(w));
        if (t >= 0.0 && t <= wallLength(w) && std::abs(o) <= w.thickness / 2.0)
            return &w;
    }
    return nullptr;
}

} // namespace

TEST(PlanRecognition, WallsDoorsAndWindowsOfAHouse)
{
    const std::vector<Shape> shapes = house();
    const RecognizedPlan plan = recognizePlan(shapes);
    ASSERT_EQ(plan.walls.size(), 5u);
    EXPECT_EQ(plan.doors, 1);
    EXPECT_EQ(plan.windows, 2);

    const Wall* bottom = wallThrough(plan, {2500, 150});
    ASSERT_NE(bottom, nullptr);
    EXPECT_NEAR(bottom->thickness, 300, 1e-6);
    // Carried to the corners on the wall axes.
    EXPECT_NEAR(std::min(bottom->start.x, bottom->end.x), 150, 1e-6);
    EXPECT_NEAR(std::max(bottom->start.x, bottom->end.x), 5850, 1e-6);
    ASSERT_EQ(bottom->openings.size(), 1u);
    const Opening& door = bottom->openings[0];
    EXPECT_EQ(door.kind, Opening::Kind::Door);
    EXPECT_NEAR(door.width, 900, 1e-6);
    EXPECT_NEAR(distance(wallPoint(*bottom, door.position), {1450, 150}), 0.0, 1e-6);
    EXPECT_NEAR(door.height, 2100, 1e-6);
    // The leaf opens into the room (y > 150) and hangs on the jamb at x 1000.
    const Vec2 leafSide = wallNormal(*bottom) * (door.swingRight ? -1.0 : 1.0);
    EXPECT_GT(leafSide.y, 0.5);
    const Vec2 hinge = wallPoint(*bottom, door.hingeAtEnd ? openingEnd(door) : openingStart(door));
    EXPECT_NEAR(hinge.x, 1000, 1e-6);

    const Wall* right = wallThrough(plan, {5850, 1000});
    ASSERT_NE(right, nullptr);
    ASSERT_EQ(right->openings.size(), 1u);
    EXPECT_EQ(right->openings[0].kind, Opening::Kind::Window);
    EXPECT_NEAR(right->openings[0].width, 1200, 1e-6);
    EXPECT_NEAR(right->openings[0].sill, 900, 1e-6);

    const Wall* top = wallThrough(plan, {2500, 3850});
    ASSERT_NE(top, nullptr);
    EXPECT_NEAR(top->thickness, 300, 1e-6);
    ASSERT_EQ(top->openings.size(), 1u);
    EXPECT_EQ(top->openings[0].kind, Opening::Kind::Window);
    EXPECT_NEAR(top->openings[0].width, 1200, 1e-6);
    EXPECT_NEAR(distance(wallPoint(*top, top->openings[0].position), {1600, 3850}), 0.0, 1e-6);

    // The partition reaches the axes of the walls it meets, so it joins them in T.
    const Wall* partition = wallThrough(plan, {3050, 2000});
    ASSERT_NE(partition, nullptr);
    EXPECT_NEAR(partition->thickness, 100, 1e-6);
    EXPECT_NEAR(std::min(partition->start.y, partition->end.y), 150, 1e-6);
    EXPECT_NEAR(std::max(partition->start.y, partition->end.y), 3850, 1e-6);

    // Every line and the door swing are now walls; the table is left alone.
    EXPECT_EQ(plan.used.size(), shapes.size() - 1);
    EXPECT_EQ(std::count(plan.used.begin(), plan.used.end(), shapes.size() - 1), 0);

    // The walls join into a closed building: L corners and T junctions.
    const std::vector<WallFaces> faces = joinWalls(plan.walls);
    int joinedEnds = 0;
    for (const WallFaces& f : faces)
        joinedEnds += int(f.startJoined) + int(f.endJoined);
    EXPECT_EQ(joinedEnds, 10);
}

TEST(PlanRecognition, GuessesTheUnitOfTheDrawing)
{
    EXPECT_EQ(guessPlanUnit(house()), 1.0);
    EXPECT_EQ(guessPlanUnit(house(0.1)), 10.0);
    EXPECT_EQ(guessPlanUnit(house(0.001)), 1000.0);

    // In centimetres the same house comes out the same, in centimetres.
    const RecognizedPlan plan = recognizePlan(house(0.1), PlanRecognitionOptions{}.inUnits(10.0));
    ASSERT_EQ(plan.walls.size(), 5u);
    EXPECT_EQ(plan.doors, 1);
    EXPECT_EQ(plan.windows, 2);
    EXPECT_NEAR(plan.walls[0].height, 270, 1e-9);
}

TEST(PlanRecognition, WideGapsWithoutSymbolsStayOpen)
{
    // One wall line with a 2 m gap and nothing in it: a passage, two walls.
    std::vector<Shape> s;
    line(s, 0, 0, 3000, 0);
    line(s, 5000, 0, 8000, 0);
    line(s, 0, 200, 3000, 200);
    line(s, 5000, 200, 8000, 200);
    const RecognizedPlan plan = recognizePlan(s);
    EXPECT_EQ(plan.walls.size(), 2u);
    EXPECT_EQ(plan.doors + plan.windows, 0);
}

TEST(PlanRecognition, PolylineOutlinesAndRotatedPlans)
{
    // A building drawn as two closed polylines (outer and inner face), turned by 30 degrees.
    const Transform rot = Transform::rotation({0, 0}, degToRad(30.0));
    Polyline outer{{{0, 0}, {5000, 0}, {5000, 4000}, {0, 4000}}, true};
    Polyline inner{{{250, 250}, {4750, 250}, {4750, 3750}, {250, 3750}}, true};
    for (Vec2& p : outer.points)
        p = rot.apply(p);
    for (Vec2& p : inner.points)
        p = rot.apply(p);
    const RecognizedPlan plan = recognizePlan({outer, inner});
    ASSERT_EQ(plan.walls.size(), 4u);
    for (const Wall& w : plan.walls)
        EXPECT_NEAR(w.thickness, 250, 1e-6);
    EXPECT_EQ(plan.used.size(), 2u);
    int joinedEnds = 0;
    for (const WallFaces& f : joinWalls(plan.walls))
        joinedEnds += int(f.startJoined) + int(f.endJoined);
    EXPECT_EQ(joinedEnds, 8);
}
