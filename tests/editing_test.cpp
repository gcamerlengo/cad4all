// SPDX-License-Identifier: GPL-3.0-or-later
#include "editing.h"

#include <gtest/gtest.h>

#include <algorithm>

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

TEST(Fillet, LineAndArc)
{
    // A line along the X axis meets an arc rising from (20, 0): the fillet
    // sits in the corner above the line and outside the arc.
    const Line line{{0, 0}, {20, 0}};
    const Arc arc{{30, 0}, 10, kPi / 2, kPi};
    const Vec2 pickArc = arc.center + polar(10, degToRad(135));
    const auto r = filletCurves(line, {5, 0}, arc, pickArc, 2);
    ASSERT_TRUE(r);
    ASSERT_TRUE(r->arc);
    const double cx = 30 - std::sqrt(140.0);
    expectNear(r->arc->center, {cx, 2}, 1e-9);
    expectNear(std::get<Line>(r->first).b, {cx, 0}, 1e-9);
    const Arc trimmed = std::get<Arc>(r->second);
    EXPECT_NEAR(trimmed.startAngle, kPi / 2, 1e-9);
    // The trimmed arc ends where the fillet touches it.
    expectNear(trimmed.endPoint(), arc.center + normalized(r->arc->center - arc.center) * 10, 1e-9);
    EXPECT_LT(r->arc->sweep(), kPi);
}

TEST(Fillet, CircleStaysWhole)
{
    const auto r = filletCurves(Line{{-20, -10}, {20, -10}}, {-15, -10}, Circle{{0, 0}, 5}, {-5, 0}, 3);
    ASSERT_TRUE(r);
    EXPECT_TRUE(std::holds_alternative<Circle>(r->second));
    ASSERT_TRUE(r->arc);
    // The line is 5 away from the circle: the fillet centre is 3 above the
    // line and 8 from the circle's centre, on the side of the picks.
    expectNear(r->arc->center, {-std::sqrt(15.0), -7}, 1e-9);
}

TEST(Fillet, ArcsTooFarApartForRadius)
{
    FilletError e;
    const Arc a{{0, 0}, 1, 0, kPi};
    const Arc b{{100, 0}, 1, 0, kPi};
    EXPECT_FALSE(filletCurves(a, {1, 0}, b, {99, 0}, 2, &e));
    EXPECT_EQ(e, FilletError::RadiusTooLarge);
}

// ---------------------------------------------------------------- chamfer

TEST(Chamfer, TwoDistances)
{
    const auto r = chamferLines(Line{{0, 0}, {10, 0}}, {2, 0}, Line{{10, 0}, {10, 10}}, {10, 8}, 2, 3);
    ASSERT_TRUE(r);
    ASSERT_TRUE(r->bevel);
    expectNear(r->first.b, {8, 0});
    expectNear(r->second.b, {10, 3});
    expectNear(r->bevel->a, {8, 0});
    expectNear(r->bevel->b, {10, 3});
}

TEST(Chamfer, AngleGivesSecondDistance)
{
    const Line a{{0, 0}, {10, 0}};
    const Line b{{10, 0}, {10, 10}};
    const auto d2 = chamferDistanceFromAngle(a, {2, 0}, b, {10, 8}, 2, degToRad(45));
    ASSERT_TRUE(d2);
    EXPECT_NEAR(*d2, 2.0, 1e-9);
    FilletError e;
    EXPECT_FALSE(chamferLines(a, {2, 0}, b, {10, 8}, 20, 1, &e));
    EXPECT_EQ(e, FilletError::RadiusTooLarge);
}

// ---------------------------------------------------------------- stretch

TEST(Stretch, MovesOnlyWhatIsInside)
{
    const BBox window = BBox::fromCorners({8, -1}, {12, 6});
    const Vec2 off{5, 0};
    const auto pl = std::get<Polyline>(stretchShape(Polyline{{{0, 0}, {10, 0}, {10, 5}, {0, 5}}, true}, window, off));
    expectNear(pl.points[0], {0, 0});
    expectNear(pl.points[1], {15, 0});
    expectNear(pl.points[2], {15, 5});
    expectNear(pl.points[3], {0, 5});

    const auto line = std::get<Line>(stretchShape(Line{{0, 0}, {10, 0}}, window, off));
    expectNear(line.b, {15, 0});

    // An arc with one end inside keeps its other end and its midpoint.
    const Arc arc{{5, 0}, 5, 0, kPi};  // from (10, 0) to (0, 0) through (5, 5)
    const auto stretched = std::get<Arc>(stretchShape(arc, window, off));
    expectNear(stretched.startPoint(), {15, 0}, 1e-9);
    expectNear(stretched.endPoint(), {0, 0}, 1e-9);
    EXPECT_NEAR(distanceTo(stretched, {5, 5}), 0.0, 1e-9);

    Dimension d;
    d.p1 = {0, 0};
    d.p2 = {10, 0};
    d.location = {5, -3};
    EXPECT_DOUBLE_EQ(std::get<Dimension>(stretchShape(d, window, off)).measurement(), 15.0);

    // Circles move with their centre, otherwise they stay.
    EXPECT_EQ(std::get<Circle>(stretchShape(Circle{{0, 0}, 3}, window, off)).center, (Vec2{0, 0}));
    EXPECT_EQ(std::get<Circle>(stretchShape(Circle{{10, 2}, 3}, window, off)).center, (Vec2{15, 2}));
}

// ---------------------------------------------------------------- polylines with arcs

TEST(PolylineArcs, OffsetChangesArcRadius)
{
    // Half circle of radius 5 around (5,0), below the chord.
    const Polyline pl{{{0, 0}, {10, 0}}, false, {1.0}};
    const auto inner = std::get<Polyline>(*offsetShape(pl, 1, {5, -1}));
    ASSERT_EQ(inner.points.size(), 2u);
    expectNear(inner.points[0], {1, 0});
    expectNear(inner.points[1], {9, 0});
    EXPECT_NEAR(inner.bulge(0), 1.0, 1e-9);
    const auto outer = std::get<Polyline>(*offsetShape(pl, 1, {5, -7}));
    expectNear(outer.points[0], {-1, 0});
    expectNear(outer.points[1], {11, 0});
}

TEST(PolylineArcs, TrimKeepsTheArc)
{
    // Straight segment then a half circle; trimming the straight part at a
    // vertical boundary leaves the arc untouched.
    const Polyline pl{{{0, 0}, {10, 0}, {10, 10}}, false, {0.0, 1.0}};
    const auto r = trimShape(pl, {Line{{5, -5}, {5, 5}}}, {2, 0});
    ASSERT_TRUE(r.changed);
    ASSERT_EQ(r.pieces.size(), 1u);
    const auto rest = std::get<Polyline>(r.pieces[0]);
    ASSERT_EQ(rest.points.size(), 3u);
    expectNear(rest.points[0], {5, 0});
    EXPECT_NEAR(rest.bulge(1), 1.0, 1e-9);
}

TEST(PolylineArcs, FilletRoundsEveryCorner)
{
    const Polyline rect{{{0, 0}, {10, 0}, {10, 5}, {0, 5}}, true};
    const auto r = filletPolyline(rect, 1);
    EXPECT_EQ(r.changed, 4);
    EXPECT_EQ(r.skipped, 0);
    ASSERT_EQ(r.polyline.points.size(), 8u);
    int arcs = 0;
    for (size_t i = 0; i < r.polyline.segmentCount(); ++i)
        if (std::abs(r.polyline.bulge(i) - std::tan(kPi / 8)) < 1e-9)
            ++arcs;
    EXPECT_EQ(arcs, 4);
    const BBox box = boundingBox(r.polyline);
    expectNear(box.min, {0, 0});
    expectNear(box.max, {10, 5});
    // Running it again finds no straight corners left.
    EXPECT_EQ(filletPolyline(r.polyline, 1).changed, 0);
}

TEST(PolylineArcs, ChamferSkipsShortSegments)
{
    const Polyline step{{{0, 0}, {10, 0}, {10, 1}, {20, 1}}};
    const auto r = chamferPolyline(step, 2, 2);
    EXPECT_EQ(r.changed, 0);
    EXPECT_EQ(r.skipped, 2);
    const auto ok = chamferPolyline(step, 0.4, 0.4);
    EXPECT_EQ(ok.changed, 2);
    ASSERT_EQ(ok.polyline.points.size(), 6u);
    expectNear(ok.polyline.points[1], {9.6, 0});
    expectNear(ok.polyline.points[2], {10, 0.4});
    // Cuts that meet in the middle of the short segment share one vertex.
    EXPECT_EQ(chamferPolyline(step, 0.5, 0.5).polyline.points.size(), 5u);
}

TEST(Grips, LineEndsAndMidpoint)
{
    const Line l{{0, 0}, {10, 0}};
    const auto grips = gripPoints(l);
    ASSERT_EQ(grips.size(), 3u);
    EXPECT_EQ(grips[2], (Vec2{5, 0}));
    const auto end = moveGrip(l, 1, {10, 5});
    ASSERT_TRUE(end);
    EXPECT_EQ(std::get<Line>(*end).b, (Vec2{10, 5}));
    EXPECT_EQ(std::get<Line>(*end).a, (Vec2{0, 0}));
    const auto moved = moveGrip(l, 2, {5, 3});
    EXPECT_EQ(std::get<Line>(*moved).a, (Vec2{0, 3}));
    EXPECT_EQ(std::get<Line>(*moved).b, (Vec2{10, 3}));
    // A line cannot shrink to a point.
    EXPECT_FALSE(moveGrip(l, 1, {0, 0}));
}

TEST(Grips, CircleAndArc)
{
    const Circle c{{0, 0}, 5};
    EXPECT_DOUBLE_EQ(std::get<Circle>(*moveGrip(c, 2, {0, 8})).radius, 8.0);
    EXPECT_EQ(std::get<Circle>(*moveGrip(c, 0, {1, 1})).center, (Vec2{1, 1}));

    const Arc a{{0, 0}, 10, 0.0, kPi};  // upper half
    const auto grips = gripPoints(a);
    ASSERT_EQ(grips.size(), 4u);
    EXPECT_NEAR(grips[3].y, 10.0, 1e-9);
    // Dragging the midpoint up makes a flatter, larger arc through the same ends.
    const Arc r = std::get<Arc>(*moveGrip(a, 3, {0, 5}));
    EXPECT_TRUE(almostEqual(r.startPoint(), {10, 0}, 1e-9) || almostEqual(r.endPoint(), {10, 0}, 1e-9));
    EXPECT_NEAR(r.radius, 12.5, 1e-9);
}

TEST(Grips, PolylineVerticesAndSegments)
{
    const Polyline pl({{0, 0}, {10, 0}, {10, 10}}, false);
    const auto grips = gripPoints(pl);
    ASSERT_EQ(grips.size(), 5u);  // three vertices, two midpoints
    const Polyline v = std::get<Polyline>(*moveGrip(pl, 1, {12, 0}));
    EXPECT_EQ(v.points[1], (Vec2{12, 0}));
    // A straight segment moves parallel.
    const Polyline s = std::get<Polyline>(*moveGrip(pl, 3, {5, -2}));
    EXPECT_EQ(s.points[0], (Vec2{0, -2}));
    EXPECT_EQ(s.points[1], (Vec2{10, -2}));
    EXPECT_EQ(s.points[2], (Vec2{10, 10}));

    // An arc segment bends through the point: here a half circle below the chord.
    const Polyline arc({{0, 0}, {10, 0}}, false, {1.0});
    const Polyline bent = std::get<Polyline>(*moveGrip(arc, 2, {5, -2}));
    EXPECT_GT(bent.bulge(0), 0.0);
    EXPECT_LT(bent.bulge(0), 1.0);
}

TEST(Grips, WallKeepsItsOpeningsInPlace)
{
    Wall w;
    w.start = {0, 0};
    w.end = {5000, 0};
    w.openings.push_back({Opening::Kind::Door, 2000.0, 900.0, 2100.0, 0.0});
    const Wall r = std::get<Wall>(*moveGrip(w, 0, {-1000, 0}));
    EXPECT_EQ(r.start, (Vec2{-1000, 0}));
    EXPECT_NEAR(r.openings[0].position, 3000.0, 1e-9);
}

TEST(Measure, LengthsAndAreas)
{
    EXPECT_DOUBLE_EQ(shapeLength(Line{{0, 0}, {3, 4}}), 5.0);
    EXPECT_NEAR(*shapeArea(Circle{{0, 0}, 2}), 4 * kPi, 1e-12);
    const Polyline square({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true);
    EXPECT_NEAR(*shapeArea(square), 100.0, 1e-9);
    EXPECT_NEAR(shapeLength(square), 40.0, 1e-9);
    // A half disc closed by its diameter, drawn either way round.
    const Polyline half({{-5, 0}, {5, 0}}, true, {1.0, 0.0});
    EXPECT_NEAR(polylineArea(half), kPi * 25 / 2, 1e-9);
    const Polyline back({{5, 0}, {-5, 0}}, true, {0.0, -1.0});
    EXPECT_NEAR(polylineArea(back), kPi * 25 / 2, 1e-9);
    EXPECT_FALSE(shapeArea(Polyline({{0, 0}, {1, 0}, {1, 1}})).has_value());
}

TEST(Explode, PolylineAndDimension)
{
    const auto parts = explodeShape(Polyline({{0, 0}, {10, 0}, {10, 10}}, false, {0.0, 0.5}));
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<Line>(parts[0]));
    EXPECT_TRUE(std::holds_alternative<Arc>(parts[1]));

    Dimension d;
    d.p1 = {0, 0};
    d.p2 = {100, 0};
    d.location = {50, 20};
    const auto dim = explodeShape(d);
    EXPECT_TRUE(std::any_of(dim.begin(), dim.end(), [](const Shape& s) { return std::holds_alternative<Text>(s); }));
    EXPECT_TRUE(std::any_of(dim.begin(), dim.end(), [](const Shape& s) { return std::holds_alternative<Hatch>(s); }));
    EXPECT_TRUE(explodeShape(Circle{{0, 0}, 1}).empty());
}

TEST(Join, ChainsLinesAndArcsInAnyDirection)
{
    // A square from four lines, one of them drawn backwards.
    const std::vector<Shape> square = {Line{{0, 0}, {10, 0}}, Line{{10, 10}, {10, 0}}, Line{{10, 10}, {0, 10}}, Line{{0, 10}, {0, 0}}};
    const auto joined = joinShapes(square, 1e-9);
    ASSERT_EQ(joined.size(), 1u);
    const Polyline& pl = std::get<Polyline>(joined[0].shape);
    EXPECT_TRUE(pl.closed);
    EXPECT_EQ(pl.points.size(), 4u);
    EXPECT_EQ(joined[0].sources.size(), 4u);
    EXPECT_NEAR(polylineArea(pl), 100.0, 1e-9);

    // Lines in a row become one line; a line far away is left alone.
    const auto row = joinShapes({Line{{0, 0}, {5, 0}}, Line{{5, 0}, {9, 0}}, Line{{50, 50}, {60, 60}}}, 1e-9);
    ASSERT_EQ(row.size(), 1u);
    EXPECT_EQ(std::get<Line>(row[0].shape).b, (Vec2{9, 0}));

    // A line and an arc keep the arc as a bulge.
    const auto withArc = joinShapes({Line{{-10, 0}, {-5, 0}}, Arc{{0, 0}, 5, 0.0, kPi}}, 1e-9);
    ASSERT_EQ(withArc.size(), 1u);
    const Polyline& p2 = std::get<Polyline>(withArc[0].shape);
    EXPECT_NEAR(shapeLength(p2), 5.0 + 5.0 * kPi, 1e-9);
}

TEST(Curves, EllipseAndSpline)
{
    const Polyline e = ellipsePolyline({0, 0}, {10, 0}, 0.5);
    EXPECT_TRUE(e.closed);
    EXPECT_NEAR(polylineArea(e), kPi * 10 * 5, 1e-4 * kPi * 10 * 5);
    for (Vec2 p : e.points)
        EXPECT_NEAR(p.x * p.x / 100 + p.y * p.y / 25, 1.0, 1e-9);

    const std::vector<Vec2> fit = {{0, 0}, {10, 5}, {20, 0}, {30, 5}};
    const Polyline s = splinePolyline(fit, false);
    EXPECT_EQ(s.points.front(), fit.front());
    EXPECT_EQ(s.points.back(), fit.back());
    // The curve passes through every fit point.
    for (Vec2 f : fit)
        EXPECT_TRUE(std::any_of(s.points.begin(), s.points.end(), [&](Vec2 p) { return almostEqual(p, f, 1e-9); }));
}
