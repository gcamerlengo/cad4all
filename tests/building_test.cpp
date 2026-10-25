// SPDX-License-Identifier: GPL-3.0-or-later
#include "building.h"

#include <gtest/gtest.h>

#include <map>

using namespace cad;

namespace {

Wall wall(Vec2 a, Vec2 b, double thickness = 200.0)
{
    Wall w;
    w.start = a;
    w.end = b;
    w.thickness = thickness;
    return w;
}

void expectNear(Vec2 a, Vec2 b)
{
    EXPECT_NEAR(a.x, b.x, 1e-9) << "x";
    EXPECT_NEAR(a.y, b.y, 1e-9) << "y";
}

std::map<Model3D::Material, double> areas(const Model3D& m)
{
    std::map<Model3D::Material, double> out;
    for (const auto& t : m.triangles)
        out[t.material] += length(cross(t.v[1] - t.v[0], t.v[2] - t.v[0])) / 2.0;
    return out;
}

template <class T>
std::vector<T> only(const std::vector<Shape>& shapes)
{
    std::vector<T> out;
    for (const Shape& s : shapes)
        if (const auto* v = std::get_if<T>(&s))
            out.push_back(*v);
    return out;
}

} // namespace

TEST(Building, JustificationPutsTheFaces)
{
    Wall w = wall({0, 0}, {1000, 0});
    EXPECT_DOUBLE_EQ(wallLeftOffset(w), 100.0);
    EXPECT_DOUBLE_EQ(wallRightOffset(w), -100.0);
    w.justification = Wall::Justification::Left;
    EXPECT_DOUBLE_EQ(wallLeftOffset(w), 0.0);
    EXPECT_DOUBLE_EQ(wallRightOffset(w), -200.0);
    w.justification = Wall::Justification::Right;
    EXPECT_DOUBLE_EQ(wallLeftOffset(w), 200.0);
    EXPECT_DOUBLE_EQ(wallRightOffset(w), 0.0);
}

TEST(Building, CornerIsMitred)
{
    const auto f = joinWalls({wall({0, 0}, {1000, 0}), wall({1000, 0}, {1000, 1000})});
    ASSERT_EQ(f.size(), 2u);
    EXPECT_TRUE(f[0].endJoined);
    EXPECT_FALSE(f[0].startJoined);
    EXPECT_TRUE(f[1].startJoined);
    // Outer corner on the right faces, inner corner on the left ones.
    expectNear(f[0].corners[1], {1100, -100});
    expectNear(f[0].corners[2], {900, 100});
    expectNear(f[1].corners[0], {1100, -100});
    expectNear(f[1].corners[3], {900, 100});
    // No end lines at the corner: three faces and one end each.
    EXPECT_EQ(only<Line>(wallPlan(wall({0, 0}, {1000, 0}), f[0])).size(), 3u);
}

TEST(Building, TeeJunctionTrimsAndOpensTheFace)
{
    const Wall through = wall({0, 0}, {2000, 0});
    const auto f = joinWalls({through, wall({1000, 0}, {1000, 1000})});
    EXPECT_TRUE(f[1].startJoined);
    expectNear(f[1].corners[3], {900, 100});
    expectNear(f[1].corners[0], {1100, 100});
    ASSERT_EQ(f[0].leftGaps.size(), 1u);
    EXPECT_NEAR(f[0].leftGaps[0].first, 900, 1e-9);
    EXPECT_NEAR(f[0].leftGaps[0].second, 1100, 1e-9);
    // Left face in two pieces, the right face whole, two ends.
    EXPECT_EQ(only<Line>(wallPlan(through, f[0])).size(), 5u);
}

TEST(Building, CollinearWallsContinue)
{
    const auto f = joinWalls({wall({0, 0}, {1000, 0}), wall({1000, 0}, {2000, 0})});
    EXPECT_TRUE(f[0].endStraight);
    EXPECT_TRUE(f[1].startStraight);
    // A thicker wall in line does not continue the faces.
    const auto g = joinWalls({wall({0, 0}, {1000, 0}), wall({1000, 0}, {2000, 0}, 300)});
    EXPECT_FALSE(g[0].endJoined);
}

TEST(Building, OpeningsFitInsideTheWall)
{
    Wall w = wall({0, 0}, {3000, 0});
    Opening door;
    door.position = 100;
    const auto fitted = fitOpening(w, door);
    ASSERT_TRUE(fitted);
    EXPECT_DOUBLE_EQ(fitted->position, 450.0);
    w.openings.push_back(*fitted);
    door.position = 1000;
    EXPECT_FALSE(fitOpening(w, door));  // overlaps the first one
    door.position = 1500;
    EXPECT_TRUE(fitOpening(w, door));
    door.width = 4000;
    EXPECT_FALSE(fitOpening(w, door));
}

TEST(Building, DoorInPlan)
{
    Wall w = wall({0, 0}, {3000, 0}, 300);
    Opening door;
    door.position = 1500;
    w.openings.push_back(door);
    const auto plan = wallPlan(w);
    const auto arcs = only<Arc>(plan);
    ASSERT_EQ(arcs.size(), 1u);
    expectNear(arcs[0].center, {1050, 150});
    EXPECT_DOUBLE_EQ(arcs[0].radius, 900.0);
    EXPECT_NEAR(arcs[0].sweep(), kPi / 2.0, 1e-12);
    // Faces in two pieces each, two ends, two jambs, the leaf.
    EXPECT_EQ(only<Line>(plan).size(), 9u);

    w.openings[0].swingRight = true;
    w.openings[0].hingeAtEnd = true;
    const auto flipped = only<Arc>(wallPlan(w));
    expectNear(flipped[0].center, {1950, -150});
    // The solid parts leave the opening empty.
    EXPECT_EQ(wallSolidParts(w, wallFaces(w)).size(), 2u);
}

TEST(Building, ModelOfAWallWithOpenings)
{
    Wall w = wall({0, 0}, {3000, 0}, 300);
    Model3D plain = buildingModel({w});
    EXPECT_EQ(plain.triangles.size(), 12u);
    EXPECT_EQ(plain.edges.size(), 12u);
    const auto [lo, hi] = plain.bounds();
    EXPECT_DOUBLE_EQ(hi.z, 2700.0);
    EXPECT_DOUBLE_EQ(lo.y, -150.0);
    EXPECT_NEAR(areas(plain)[Model3D::Material::Wall], 19.62e6, 1.0);

    Opening window;
    window.kind = Opening::Kind::Window;
    window.position = 1500;
    window.width = 1200;
    window.height = 1400;
    window.sill = 900;
    w.openings = {window};
    auto a = areas(buildingModel({w}));
    // Two faces lose the window, the reveals add its perimeter by the thickness.
    EXPECT_NEAR(a[Model3D::Material::Wall], 19.62e6 - 2 * 1.68e6 + 5200 * 300, 1.0);
    EXPECT_NEAR(a[Model3D::Material::Glass], 1.68e6, 1.0);

    Opening door;
    door.position = 1500;
    w.openings = {door};
    a = areas(buildingModel({w}));
    EXPECT_NEAR(a[Model3D::Material::Wall], 19.62e6 - 2 * 1.89e6 - 900 * 300 + (2 * 2100 + 900) * 300, 1.0);
    EXPECT_GT(a[Model3D::Material::Wood], 0.0);
}

TEST(Building, JoinedWallsHaveNoInnerEnds)
{
    const Model3D m = buildingModel({wall({0, 0}, {1000, 0}), wall({1000, 0}, {1000, 1000})});
    // Each wall: two faces, one free end, top and bottom.
    EXPECT_EQ(m.triangles.size(), 20u);
}

TEST(Building, MirrorSwapsSidesAndDoorSwing)
{
    Wall w = wall({0, 0}, {3000, 0});
    w.justification = Wall::Justification::Left;
    Opening door;
    door.position = 1000;
    w.openings.push_back(door);
    const Wall m = std::get<Wall>(transformed(w, Transform::mirror({0, 0}, {1, 0})));
    EXPECT_EQ(m.justification, Wall::Justification::Right);
    EXPECT_TRUE(m.openings[0].swingRight);
    // The mirrored wall lies on the other side of the line, as it should.
    EXPECT_NEAR(boundingBox(m).max.y, 200.0, 1e-9);
    EXPECT_NEAR(boundingBox(w).min.y, -200.0, 1e-9);
}

namespace {

Roof rectRoof(std::vector<bool> gables = {})
{
    Roof r;
    r.footprint = Polyline{{{0, 0}, {8000, 0}, {8000, 4000}, {0, 4000}}, true};
    r.gables = std::move(gables);
    r.pitch = std::atan(0.5);  // rises 1 every 2
    r.base = 3000;
    r.overhang = 0;
    return r;
}

} // namespace

TEST(Building, HipRoofHasFourFacesAndARidge)
{
    const Roof r = rectRoof();
    const auto g = roofGeometry(r);
    ASSERT_TRUE(g);
    EXPECT_EQ(g->faces.size(), 4u);
    // Ridge at mid depth, 2000 in from the long sides.
    EXPECT_NEAR(roofHeight(r, {4000, 2000}), 3000 + 1000, 1e-6);
    EXPECT_NEAR(roofHeight(r, {4000, 0}), 3000, 1e-6);
    EXPECT_NEAR(roofHeight(r, {500, 2000}), 3000 + 250, 1e-6);  // near the hip end
    // Outline, the ridge and four hips.
    const auto plan = roofPlan(r);
    EXPECT_EQ(only<Line>(plan).size(), 5u);
    EXPECT_EQ(only<Polyline>(plan).size(), 1u);
}

TEST(Building, GableRoofSlopesOnTheLongSides)
{
    const Roof r = rectRoof({false, true, false, true});
    const auto g = roofGeometry(r);
    ASSERT_TRUE(g);
    EXPECT_EQ(g->faces.size(), 2u);
    EXPECT_NEAR(roofHeight(r, {100, 2000}), 4000, 1e-6);  // the ridge runs to the gable
    const auto a = areas(buildingModel(std::vector<Wall>{}));
    EXPECT_TRUE(a.empty());

    Document doc;
    doc.add(r);
    const auto m = areas(buildingModel(doc));
    // Two slopes, 8000 long and sqrt(2000² + 1000²) wide, top and bottom.
    EXPECT_GT(m.at(Model3D::Material::Roof), 2 * 2 * 8000 * std::hypot(2000.0, 1000.0) * 0.99);
    // The two gable triangles close the ends.
    EXPECT_GT(m.at(Model3D::Material::Wall), 2 * 4000 * 1000 / 2.0 * 0.9);
}

TEST(Building, RoofNeedsAConvexOrRightAngledFootprint)
{
    Roof r;
    r.footprint = Polyline{{{0, 0}, {4000, 0}, {2000, 1000}, {2000, 4000}}, true};
    EXPECT_FALSE(roofGeometry(r));
    r.footprint.points.resize(2);
    EXPECT_FALSE(roofGeometry(r));
}

namespace {

bool hasLine(const RoofGeometry& g, Vec2 a, Vec2 b)
{
    return std::any_of(g.lines.begin(), g.lines.end(), [&](const auto& l) {
        return (distance(l[0], a) < 1e-6 && distance(l[1], b) < 1e-6) || (distance(l[0], b) < 1e-6 && distance(l[1], a) < 1e-6);
    });
}

Roof lRoof(bool clockwise)
{
    Roof r;
    std::vector<Vec2> pts{{0, 0}, {12000, 0}, {12000, 5000}, {5000, 5000}, {5000, 11000}, {0, 11000}};
    if (clockwise)
        std::reverse(pts.begin(), pts.end());
    r.footprint = Polyline{pts, true};
    r.pitch = kPi / 4.0;  // rises as much as it goes in
    r.base = 0;
    r.overhang = 0;
    return r;
}

} // namespace

TEST(Building, HipRoofOnAnLPlan)
{
    for (bool clockwise : {false, true}) {
        const Roof r = lRoof(clockwise);
        const auto g = roofGeometry(r);
        ASSERT_TRUE(g);
        EXPECT_EQ(g->pieces.size(), 2u);
        // Two ridges meeting over the corner, a hip outside and a valley inside.
        EXPECT_TRUE(hasLine(*g, {9500, 2500}, {2500, 2500}));
        EXPECT_TRUE(hasLine(*g, {2500, 8500}, {2500, 2500}));
        EXPECT_TRUE(hasLine(*g, {0, 0}, {5000, 5000}));
        EXPECT_EQ(g->lines.size(), 7u);
        EXPECT_NEAR(g->height({2500, 2500}), 2500, 1e-6);
        EXPECT_NEAR(g->height({4000, 4000}), 1000, 1e-6);  // in the valley
        EXPECT_NEAR(g->height({8000, 1000}), 1000, 1e-6);
    }
}

TEST(Building, GableRoofOnAnLPlanHasGablesAtTheWingEnds)
{
    Roof r = lRoof(false);
    r.gables = roofGables(r.footprint.points, RoofStyle::Gable);
    EXPECT_EQ(r.gables, (std::vector<bool>{false, true, false, false, true, false}));
    const auto g = roofGeometry(r);
    ASSERT_TRUE(g);
    // The ridges run to the gables.
    EXPECT_TRUE(hasLine(*g, {12000, 2500}, {2500, 2500}));
    EXPECT_TRUE(hasLine(*g, {2500, 11000}, {2500, 2500}));
    EXPECT_NEAR(g->height({11900, 2500}), 2500, 1e-6);
    // Reversed corners give the same gables, on their own edges.
    std::vector<Vec2> back(r.footprint.points.rbegin(), r.footprint.points.rend());
    EXPECT_EQ(roofGables(back, RoofStyle::Gable), (std::vector<bool>{true, false, false, true, false, false}));
    // A rectangle is a plain gable roof.
    EXPECT_EQ(roofGables({{0, 0}, {8000, 0}, {8000, 4000}, {0, 4000}}, RoofStyle::Gable),
              (std::vector<bool>{false, true, false, true}));
}

TEST(Building, RoofOnAUPlanHasNoHoles)
{
    Roof r;
    r.footprint = Polyline{{{0, 0}, {14000, 0}, {14000, 10000}, {10000, 10000}, {10000, 5000}, {4000, 5000}, {4000, 10000}, {0, 10000}}, true};
    r.overhang = 0;
    r.base = 0;
    r.gables = roofGables(r.footprint.points, RoofStyle::Gable);
    const auto g = roofGeometry(r);
    ASSERT_TRUE(g);
    EXPECT_EQ(g->pieces.size(), 3u);
    double area = 0.0;
    for (const RoofFace& f : g->faces) {
        double a = 0.0;
        for (size_t k = 0; k < f.polygon.size(); ++k)
            a += cross(f.polygon[k], f.polygon[(k + 1) % f.polygon.size()]);
        area += a / 2.0;
    }
    // The faces cover the plan once.
    EXPECT_NEAR(area, 14000.0 * 5000 + 2 * 4000.0 * 5000, 1.0);
}

TEST(Building, FlatRoofIsOneSlab)
{
    Roof r = rectRoof({true, true, true, true});
    r.overhang = 500;
    const auto g = roofGeometry(r);
    ASSERT_TRUE(g);
    ASSERT_EQ(g->faces.size(), 1u);
    const BBox box = boundingBox(Polyline{g->outline, true});
    EXPECT_NEAR(box.min.x, -500, 1e-9);
    EXPECT_NEAR(box.max.y, 4500, 1e-9);
}

TEST(Building, SlabsAndStoriesIn3D)
{
    Document doc;
    Slab slab;
    slab.loops = {Polyline{{{0, 0}, {5000, 0}, {5000, 4000}, {0, 4000}}, true},
                  Polyline{{{1000, 1000}, {2000, 1000}, {2000, 2000}, {1000, 2000}}, true}};
    slab.thickness = 200;
    doc.add(slab);
    Model3D m = buildingModel(doc);
    // Top and bottom less the hole, the outer and inner sides.
    EXPECT_NEAR(areas(m)[Model3D::Material::Wall], 2 * (20e6 - 1e6) + (18000 + 4000) * 200, 1.0);
    auto [lo, hi] = m.bounds();
    EXPECT_DOUBLE_EQ(hi.z, 0.0);
    EXPECT_DOUBLE_EQ(lo.z, -200.0);

    // A wall on the first floor stands on its elevation.
    doc.setCurrentStory(doc.addStory({0, "Primo piano", 3000, 3000}));
    doc.add(wall({0, 0}, {5000, 0}));
    m = buildingModel(doc);
    std::tie(lo, hi) = m.bounds();
    EXPECT_DOUBLE_EQ(hi.z, 3000.0 + 2700.0);
    EXPECT_DOUBLE_EQ(lo.z, -200.0);
}

TEST(Building, StraightStair)
{
    Stair s;
    s.start = {0, 0};
    s.angle = kPi / 2.0;  // up along Y
    s.risers = 16;
    s.height = 2880;
    s.going = 300;
    s.width = 1200;
    const auto steps = stairSteps(s);
    ASSERT_EQ(steps.size(), 15u);  // the last riser reaches the floor above
    EXPECT_DOUBLE_EQ(steps.front().top, 180.0);
    EXPECT_DOUBLE_EQ(steps.back().top, 15 * 180.0);
    const BBox box = boundingBox(Shape{s});
    EXPECT_NEAR(box.min.x, -600, 1e-9);
    EXPECT_NEAR(box.max.y, 15 * 300, 1e-9);
    const auto [lo, hi] = buildingModel([&] {
        Document doc;
        doc.add(s);
        return doc;
    }()).bounds();
    EXPECT_NEAR(hi.z, 15 * 180.0, 1e-9);
    EXPECT_NEAR(lo.z, 0.0, 1e-9);
}

TEST(Building, LAndUStairsTurnOnALanding)
{
    Stair s;
    s.kind = Stair::Kind::L;
    s.risers = 18;
    s.going = 250;
    s.width = 1000;
    auto steps = stairSteps(s);
    ASSERT_EQ(steps.size(), 17u);  // 8 + landing + 8
    // The landing, halfway up, a square as wide as the stair.
    EXPECT_DOUBLE_EQ(steps[8].top, 9 * s.height / 18);
    BBox landing;
    for (Vec2 p : steps[8].polygon)
        landing.add(p);
    EXPECT_NEAR(landing.width(), 1000, 1e-9);
    EXPECT_NEAR(landing.height(), 1000, 1e-9);
    // Turning left from +X goes to +Y; turning right to -Y.
    EXPECT_GT(boundingBox(Shape{s}).max.y, 1000);
    s.turnRight = true;
    EXPECT_LT(boundingBox(Shape{s}).min.y, -1000);

    s.kind = Stair::Kind::U;
    s.turnRight = false;
    steps = stairSteps(s);
    ASSERT_EQ(steps.size(), 17u);
    // The second flight comes back beside the first.
    BBox last;
    for (Vec2 p : steps.back().polygon)
        last.add(p);
    EXPECT_NEAR(last.center().y, 1000, 1e-9);
    EXPECT_LT(last.max.x, 8 * 250 + 1e-9);
    // A mirror turns it the other way.
    const Stair m = std::get<Stair>(transformed(s, Transform::mirror({0, 0}, {1, 0})));
    EXPECT_TRUE(m.turnRight);
    EXPECT_NEAR(boundingBox(Shape{m}).min.y, -1500 - 0.0, 1e-6);
}
