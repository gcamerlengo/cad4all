// SPDX-License-Identifier: GPL-3.0-or-later
// Drives the command engine the way a user does from the command line.
#include "commandengine.h"
#include "building.h"
#include "hatch.h"

#include <gtest/gtest.h>

using namespace cad;

namespace {

class Commands : public ::testing::Test {
protected:
    void type(std::initializer_list<const char*> inputs)
    {
        for (const char* in : inputs)
            engine.submit(QString::fromUtf8(in));
    }

    template <class T>
    std::vector<T> shapes() const
    {
        std::vector<T> out;
        for (const auto& [id, e] : doc.entities())
            if (const auto* s = std::get_if<T>(&e.shape))
                out.push_back(*s);
        return out;
    }

    Document doc;
    CommandEngine engine{doc};
};

} // namespace

TEST_F(Commands, LineChainWithCloseAndUndo)
{
    type({"LINEA", "0,0", "10,0", "@0,10", "C"});
    EXPECT_FALSE(engine.isActive());
    ASSERT_EQ(shapes<Line>().size(), 3u);
    EXPECT_EQ(shapes<Line>()[2].b, (Vec2{0, 0}));

    // The whole command is one undo step.
    type({"U"});
    EXPECT_TRUE(doc.entities().empty());
}

TEST_F(Commands, LineUndoOptionRemovesLastSegment)
{
    type({"L", "0,0", "10,0", "20,0", "A", ""});
    ASSERT_EQ(shapes<Line>().size(), 1u);
    EXPECT_EQ(shapes<Line>()[0].b, (Vec2{10, 0}));
}

TEST_F(Commands, EnterRepeatsLastCommand)
{
    type({"CERCHIO", "0,0", "5", ""});
    EXPECT_TRUE(engine.isActive());
    EXPECT_EQ(engine.activeCommandName(), QStringLiteral("CIRCLE"));
    type({"10,0", "D", "4"});
    const auto circles = shapes<Circle>();
    ASSERT_EQ(circles.size(), 2u);
    EXPECT_DOUBLE_EQ(circles[0].radius, 5.0);
    EXPECT_DOUBLE_EQ(circles[1].radius, 2.0);
}

TEST_F(Commands, DirectDistanceEntryFollowsCursor)
{
    type({"L", "0,0"});
    engine.setCursor({0, 50});
    type({"7", ""});
    const auto lines = shapes<Line>();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NEAR(lines[0].b.x, 0.0, 1e-12);
    EXPECT_NEAR(lines[0].b.y, 7.0, 1e-12);
}

TEST_F(Commands, OrthoConstrainsRelativeToBase)
{
    engine.setOrthoEnabled(true);
    type({"L", "0,0"});
    const Vec2 p = engine.constrain({10, 2});
    EXPECT_EQ(p, (Vec2{10, 0}));
    engine.cancel();
}

TEST_F(Commands, RectangleAndPolyline)
{
    type({"REC", "0,0", "@4,3"});
    type({"PL", "0,0", "1,0", "1,1", ""});
    const auto plines = shapes<Polyline>();
    ASSERT_EQ(plines.size(), 2u);
    EXPECT_TRUE(plines[0].closed);
    EXPECT_EQ(plines[0].points[2], (Vec2{4, 3}));
    EXPECT_FALSE(plines[1].closed);
    EXPECT_EQ(plines[1].points.size(), 3u);
}

TEST_F(Commands, ArcThroughThreePoints)
{
    type({"ARCO", "10,0", "0,10", "-10,0"});
    const auto arcs = shapes<Arc>();
    ASSERT_EQ(arcs.size(), 1u);
    EXPECT_NEAR(arcs[0].radius, 10.0, 1e-9);
}

TEST_F(Commands, MoveUsesExistingSelection)
{
    const EntityId id = doc.add(Line{{0, 0}, {1, 0}});
    engine.select({id});
    type({"SPOSTA", "0,0", "5,5"});
    EXPECT_EQ(std::get<Line>(doc.entity(id)->shape).a, (Vec2{5, 5}));
    EXPECT_TRUE(engine.selection().empty());
}

TEST_F(Commands, CopyAsksForObjectsAndRepeats)
{
    const EntityId id = doc.add(Circle{{0, 0}, 1});
    type({"COPIA"});
    EXPECT_TRUE(engine.wantsSelection());
    engine.select({id});
    type({"", "0,0", "10,0", "20,0", ""});
    EXPECT_EQ(shapes<Circle>().size(), 3u);
}

TEST_F(Commands, RotateScaleMirror)
{
    const EntityId id = doc.add(Line{{0, 0}, {10, 0}});
    engine.select({id});
    type({"RUOTA", "0,0", "90"});
    auto l = std::get<Line>(doc.entity(id)->shape);
    EXPECT_NEAR(l.b.x, 0.0, 1e-9);
    EXPECT_NEAR(l.b.y, 10.0, 1e-9);

    engine.select({id});
    type({"SCALA", "0,0", "0.5"});
    l = std::get<Line>(doc.entity(id)->shape);
    EXPECT_NEAR(l.b.y, 5.0, 1e-9);

    engine.select({id});
    type({"SPECCHIO", "0,0", "10,0", "S"});
    ASSERT_EQ(doc.entities().size(), 1u); // source erased
    l = std::get<Line>(doc.entities().begin()->second.shape);
    EXPECT_NEAR(l.b.y, -5.0, 1e-9);
}

TEST_F(Commands, EraseAndUnknownCommand)
{
    const EntityId id = doc.add(Line{{0, 0}, {1, 0}});
    engine.select({id});
    type({"E"});
    EXPECT_TRUE(doc.entities().empty());
    EXPECT_FALSE(engine.run(QStringLiteral("NONESISTE")));
}

TEST_F(Commands, SnapFindsEndpointAndIntersection)
{
    doc.add(Line{{0, 0}, {10, 0}});
    doc.add(Line{{5, -5}, {5, 5}});
    type({"L"});
    auto s = engine.findSnap({9.9, 0.1}, 0.5);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->type, SnapEndpoint);
    s = engine.findSnap({5.1, 0.1}, 0.5);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->type, SnapIntersection);
    EXPECT_EQ(s->point, (Vec2{5, 0}));
    engine.cancel();
}

TEST_F(Commands, WindowAndCrossingSelection)
{
    const EntityId inside = doc.add(Line{{1, 1}, {2, 2}});
    const EntityId crossing = doc.add(Line{{0, 0}, {10, 0}});
    const BBox box = BBox::fromCorners({0.5, -1}, {3, 3});
    auto win = engine.pickBox(box, false);
    ASSERT_EQ(win.size(), 1u);
    EXPECT_EQ(win[0], inside);
    auto cross = engine.pickBox(box, true);
    EXPECT_EQ(cross.size(), 2u);
    EXPECT_EQ(engine.pick({5, 0.01}, 0.1), crossing);
}

TEST_F(Commands, OffsetByDistanceRepeats)
{
    engine.setPickTolerance(0.5);
    doc.add(Line{{0, 0}, {10, 0}});
    type({"OFFSET", "2", "5,0", "5,1", "5,2", "5,3", ""});
    // The second pick is on the first copy (y = 2), so it makes one at y = 4.
    const auto lines = shapes<Line>();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_NEAR(lines[1].a.y, 2.0, 1e-9);
    EXPECT_NEAR(lines[2].a.y, 4.0, 1e-9);
    EXPECT_FALSE(engine.isActive());
}

TEST_F(Commands, OffsetThroughPoint)
{
    engine.setPickTolerance(0.5);
    doc.add(Circle{{0, 0}, 5});
    type({"O", "P", "5,0", "8,0", "E"});
    const auto circles = shapes<Circle>();
    ASSERT_EQ(circles.size(), 2u);
    EXPECT_NEAR(circles[1].radius, 8.0, 1e-9);
}

TEST_F(Commands, TrimAndExtendUseAllEdges)
{
    engine.setPickTolerance(0.5);
    doc.add(Line{{0, 0}, {10, 0}});
    doc.add(Line{{5, -5}, {5, 5}});
    doc.add(Line{{12, -5}, {12, 5}});
    type({"TAGLIA", "8,0", ""});
    auto horizontal = [&] {
        for (const Line& l : shapes<Line>())
            if (std::abs(l.a.y - l.b.y) < 1e-12)
                return l;
        return Line{};
    };
    EXPECT_NEAR(horizontal().b.x, 5.0, 1e-9);
    EXPECT_EQ(shapes<Line>().size(), 3u);

    // The cut end sits on the edge at x = 5, so extending goes on to x = 12.
    type({"ESTENDI", "4.9,0", ""});
    EXPECT_NEAR(horizontal().b.x, 12.0, 1e-9);

    // Each command session is a single undo step.
    type({"U"});
    EXPECT_NEAR(horizontal().b.x, 5.0, 1e-9);
}

TEST_F(Commands, FilletWithRadius)
{
    engine.setPickTolerance(0.5);
    doc.add(Line{{0, 0}, {10, 0}});
    doc.add(Line{{10, 0}, {10, 10}});
    type({"RACCORDO", "R", "2", "3,0", "10,7"});
    EXPECT_FALSE(engine.isActive());
    ASSERT_EQ(shapes<Arc>().size(), 1u);
    EXPECT_NEAR(shapes<Arc>()[0].radius, 2.0, 1e-12);
    EXPECT_NEAR(shapes<Line>()[0].b.x, 8.0, 1e-9);

    // The radius is remembered for the next FILLET; reset it for other tests.
    type({"F", "R", "0", ""});
}

TEST_F(Commands, ObjectPickDisablesSnapAndSelection)
{
    doc.add(Line{{0, 0}, {10, 0}});
    type({"TR"});
    EXPECT_TRUE(engine.wantsObjectPick());
    EXPECT_FALSE(engine.wantsSelection());
    EXPECT_FALSE(engine.findSnap({10, 0}, 1.0));
    engine.cancel();
    EXPECT_FALSE(engine.wantsObjectPick());
}

TEST_F(Commands, TextKeepsSpacesAndStacksLines)
{
    type({"TESTO", "10,20", "5", "0"});
    EXPECT_TRUE(engine.wantsText());
    type({"Camera da letto", "  12 mq", ""});
    EXPECT_FALSE(engine.isActive());
    const auto texts = shapes<Text>();
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[0].content, "Camera da letto");
    EXPECT_EQ(texts[0].position, (Vec2{10, 20}));
    EXPECT_DOUBLE_EQ(texts[0].height, 5.0);
    EXPECT_EQ(texts[1].content, "  12 mq");
    EXPECT_LT(texts[1].position.y, 20.0);
    EXPECT_DOUBLE_EQ(texts[1].position.x, 10.0);

    // The whole command is one undo step.
    type({"U"});
    EXPECT_TRUE(doc.entities().empty());
}

TEST_F(Commands, TextJustifyAndRotation)
{
    type({"DT", "G", "C", "0,0", "2", "90", "Asse", ""});
    const auto texts = shapes<Text>();
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_EQ(texts[0].align, Text::Align::Center);
    EXPECT_NEAR(texts[0].rotation, kPi / 2, 1e-12);
    type({"DT", "G", "S", ""});  // back to the default for the other tests
    engine.cancel();
}

TEST_F(Commands, TextEditChangesContentAndDimensionLabel)
{
    type({"TESTO", "0,0", "5", "0", "Bagno", ""});
    type({"QUOTALIN", "0,-20", "100,-20", "50,-40"});
    engine.setPickTolerance(0.5);
    type({"ED", "2,2", "Cucina", "50,-40", "<> cm", "E"});
    EXPECT_EQ(shapes<Text>()[0].content, "Cucina");
    EXPECT_EQ(shapes<Dimension>()[0].text, "<> cm");
    EXPECT_EQ(dimensionGeometry(shapes<Dimension>()[0]).label.content, "100 cm");
}

TEST_F(Commands, LinearDimensionChoosesOrientationFromCursor)
{
    type({"DIMLINEAR", "0,0", "100,40", "50,80"});
    type({"DLI", "0,0", "100,40", "150,20"});
    type({"DLI", "0,0", "100,40", "V", "50,80"});
    const auto dims = shapes<Dimension>();
    ASSERT_EQ(dims.size(), 3u);
    EXPECT_DOUBLE_EQ(dims[0].measurement(), 100.0);  // above: horizontal
    EXPECT_DOUBLE_EQ(dims[1].measurement(), 40.0);   // beside: vertical
    EXPECT_DOUBLE_EQ(dims[2].measurement(), 40.0);   // forced vertical
}

TEST_F(Commands, AlignedDimensionOfPickedLineWithText)
{
    type({"L", "0,0", "30,40", ""});
    engine.setPickTolerance(0.5);
    type({"QUOTALLINEATA", "", "15,20", "T", "<> m", "0,10"});
    const auto dims = shapes<Dimension>();
    ASSERT_EQ(dims.size(), 1u);
    EXPECT_EQ(dims[0].kind, Dimension::Kind::Aligned);
    EXPECT_DOUBLE_EQ(dims[0].measurement(), 50.0);
    EXPECT_EQ(dimensionGeometry(dims[0]).label.content, "50 m");
}

TEST_F(Commands, RadiusAndDiameterDimensions)
{
    type({"CERCHIO", "0,0", "25"});
    engine.setPickTolerance(0.5);
    type({"DRA", "25,0", "40,40"});
    type({"QUOTADIAMETRO", "0,25", "5,5"});
    type({"DRA", "100,100"});  // nothing there
    EXPECT_TRUE(engine.isActive());
    engine.cancel();
    const auto dims = shapes<Dimension>();
    ASSERT_EQ(dims.size(), 2u);
    EXPECT_EQ(dimensionGeometry(dims[0]).label.content, "R25");
    EXPECT_NEAR(distance(dims[0].p2, {0, 0}), 25.0, 1e-9);
    EXPECT_EQ(dimensionGeometry(dims[1]).label.content, "\xC3\x98" "50");
}

TEST_F(Commands, DimScaleSizesNewDimensions)
{
    type({"DIMSCALE", "10"});
    type({"DLI", "0,0", "1000,0", "500,-100"});
    type({"DIMSCALE", "1"});
    const auto dims = shapes<Dimension>();
    ASSERT_EQ(dims.size(), 1u);
    EXPECT_DOUBLE_EQ(dims[0].textHeight, 25.0);
    EXPECT_DOUBLE_EQ(dims[0].arrowSize, 25.0);
}

TEST_F(Commands, TrimIgnoresTextAsEdge)
{
    type({"L", "0,0", "100,0", ""});
    type({"TESTO", "40,-1", "5", "0", "Muro", ""});
    engine.setPickTolerance(0.5);
    // The text box overlaps the line but does not cut it: the whole line goes.
    type({"TR", "10,0", "E"});
    EXPECT_TRUE(shapes<Line>().empty());
    EXPECT_EQ(shapes<Text>().size(), 1u);
}

TEST_F(Commands, ChamferWithDistances)
{
    type({"L", "0,0", "10,0", ""});
    type({"L", "10,0", "10,10", ""});
    engine.setPickTolerance(0.5);
    type({"CIMA", "D", "2", "3", "2,0", "10,8"});
    EXPECT_FALSE(engine.isActive());
    const auto lines = shapes<Line>();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[2].a, (Vec2{8, 0}));
    EXPECT_EQ(lines[2].b, (Vec2{10, 3}));
    type({"CHA", "D", "0", "0", ""});  // back to the default for the other tests
}

TEST_F(Commands, FilletLineAndCircle)
{
    type({"L", "-20,-10", "20,-10", ""});
    type({"C", "0,0", "5"});
    engine.setPickTolerance(0.5);
    type({"RACCORDO", "R", "3", "-15,-10", "-5,0"});
    EXPECT_FALSE(engine.isActive());
    EXPECT_EQ(shapes<Circle>().size(), 1u);
    EXPECT_EQ(shapes<Arc>().size(), 1u);
    type({"F", "R", "0", ""});
}

TEST_F(Commands, StretchWithCrossingWindow)
{
    type({"REC", "0,0", "10,5"});
    type({"STIRA", "8,-1", "12,6", "0,0", "@5,0"});
    EXPECT_FALSE(engine.isActive());
    const auto rect = shapes<Polyline>();
    ASSERT_EQ(rect.size(), 1u);
    EXPECT_EQ(rect[0].points[1], (Vec2{15, 0}));
    EXPECT_EQ(rect[0].points[0], (Vec2{0, 0}));
    type({"U"});
    EXPECT_EQ(shapes<Polyline>()[0].points[1], (Vec2{10, 0}));
}

TEST_F(Commands, PolylineArcSegmentsAreTangent)
{
    type({"PL", "0,0", "10,0", "A", "10,10", "L", "0,10", ""});
    const auto plines = shapes<Polyline>();
    ASSERT_EQ(plines.size(), 1u);
    ASSERT_EQ(plines[0].points.size(), 4u);
    EXPECT_NEAR(plines[0].bulge(0), 0.0, 1e-9);
    EXPECT_NEAR(plines[0].bulge(1), 1.0, 1e-9);  // half circle, counter-clockwise
    EXPECT_NEAR(plines[0].bulge(2), 0.0, 1e-9);
}

TEST_F(Commands, FilletAndChamferWholePolyline)
{
    type({"REC", "0,0", "10,5"});
    engine.setPickTolerance(0.5);
    type({"F", "R", "1"});
    type({"F", "P", "5,0"});
    EXPECT_FALSE(engine.isActive());
    auto pl = shapes<Polyline>();
    ASSERT_EQ(pl.size(), 1u);
    EXPECT_EQ(pl[0].points.size(), 8u);
    EXPECT_TRUE(pl[0].hasArcs());
    type({"U"});
    type({"CHA", "D", "1", "1", ""});
    type({"CHA", "P", "5,0"});
    pl = shapes<Polyline>();
    EXPECT_EQ(pl[0].points.size(), 8u);
    EXPECT_FALSE(pl[0].hasArcs());
    type({"F", "R", "0", ""});
    type({"CHA", "D", "0", "0", ""});
}

TEST_F(Commands, HatchInsideRoomWithOptions)
{
    type({"L", "0,0", "10,0", "10,5", "0,5", "C"});
    type({"L", "6,0", "6,5", ""});
    type({"TRATTEGGIO", "M", "?", "XYZ", "NET", "S", "2", "AN", "30", "3,3", "8,3", ""});
    EXPECT_FALSE(engine.isActive());
    auto hatches = shapes<Hatch>();
    ASSERT_EQ(hatches.size(), 1u);
    EXPECT_EQ(hatches[0].pattern, "NET");
    EXPECT_DOUBLE_EQ(hatches[0].scale, 2.0);
    EXPECT_NEAR(hatches[0].angle, degToRad(30), 1e-12);
    EXPECT_EQ(hatches[0].loops.size(), 2u);  // both rooms, one hatch

    // A point outside everything finds nothing and the command goes on.
    type({"H", "20,20"});
    EXPECT_TRUE(engine.isActive());
    type({""});
    EXPECT_EQ(shapes<Hatch>().size(), 1u);

    type({"MODIFICATRATTEGGIO", "6,1", "1,1", "M", "SOLID", "S", "0.5", ""});
    hatches = shapes<Hatch>();
    EXPECT_TRUE(hatches[0].solid());
    EXPECT_DOUBLE_EQ(hatches[0].scale, 0.5);
    type({"U"});
    EXPECT_EQ(shapes<Hatch>()[0].scale, 2.0);
    type({"TRATTEGGIO", "M", "ANSI31", "S", "1", "AN", "0", ""});
}

TEST_F(Commands, HatchBySelectingObjects)
{
    type({"C", "0,0", "5"});
    type({"REC", "-1,-1", "1,1"});
    engine.setPickTolerance(0.1);
    type({"H", "O", "5,0", "1,0", ""});
    const auto hatches = shapes<Hatch>();
    ASSERT_EQ(hatches.size(), 1u);
    ASSERT_EQ(hatches[0].loops.size(), 2u);
    EXPECT_TRUE(insideHatch(hatches[0], {3, 0}));
    EXPECT_FALSE(insideHatch(hatches[0], {0, 0}));
}

TEST_F(Commands, WallChainWithOptions)
{
    type({"MURO", "S", "250", "A", "3000", "G", "S", "0,0", "5000,0", "@0,4000", "AN", "5000,3000", "@-5000,0", "C"});
    EXPECT_FALSE(engine.isActive());
    const auto walls = shapes<Wall>();
    ASSERT_EQ(walls.size(), 4u);
    EXPECT_DOUBLE_EQ(walls[0].thickness, 250);
    EXPECT_DOUBLE_EQ(walls[0].height, 3000);
    EXPECT_EQ(walls[0].justification, Wall::Justification::Left);
    EXPECT_EQ(walls[1].end, (Vec2{5000, 3000}));
    EXPECT_EQ(walls[3].end, (Vec2{0, 0}));
    // All four corners are joined.
    for (const WallFaces& f : joinWalls(walls))
        EXPECT_TRUE(f.startJoined && f.endJoined);
    type({"U"});
    EXPECT_TRUE(shapes<Wall>().empty());
    type({"MU", "S", "300", "A", "2700", "G", "C", ""});
}

TEST_F(Commands, DoorsAndWindows)
{
    type({"MURO", "0,0", "6000,0", ""});
    engine.setPickTolerance(10);
    // A door near the start is moved inside the wall; the side is picked.
    type({"PORTA", "L", "1000", "100,50", "300,-500", ""});
    auto w = shapes<Wall>()[0];
    ASSERT_EQ(w.openings.size(), 1u);
    EXPECT_DOUBLE_EQ(w.openings[0].position, 500);
    EXPECT_DOUBLE_EQ(w.openings[0].width, 1000);
    EXPECT_TRUE(w.openings[0].swingRight);
    EXPECT_FALSE(w.openings[0].hingeAtEnd);

    // Overlapping and off-wall points are refused, Enter takes the default side.
    type({"PO", "600,0", "3000,5000", "3000,0", "", ""});
    w = shapes<Wall>()[0];
    ASSERT_EQ(w.openings.size(), 2u);
    EXPECT_FALSE(w.openings[1].swingRight);

    type({"FINESTRA", "D", "1000", "A", "1200", "5000,0", ""});
    w = shapes<Wall>()[0];
    ASSERT_EQ(w.openings.size(), 3u);
    EXPECT_EQ(w.openings[2].kind, Opening::Kind::Window);
    EXPECT_DOUBLE_EQ(w.openings[2].sill, 1000);
    EXPECT_DOUBLE_EQ(w.openings[2].height, 1200);

    type({"MODIFICAMURO", "4000,0", "S", "400", "E", "3000,0", "A", "3500", ""});
    w = shapes<Wall>()[0];
    EXPECT_DOUBLE_EQ(w.thickness, 400);
    EXPECT_DOUBLE_EQ(w.height, 3500);
    ASSERT_EQ(w.openings.size(), 2u);
    EXPECT_EQ(w.openings[1].kind, Opening::Kind::Window);
    type({"PORTA", "L", "900", "", "FI", "D", "900", "A", "1400", ""});
}

TEST_F(Commands, EditingCommandsAcceptWalls)
{
    type({"MURO", "0,0", "5000,0", "5000,4000", ""});
    type({"L", "2500,-1000", "2500,1000", ""});
    engine.setPickTolerance(10);
    // Commands made for lines and arcs leave walls alone, without failing.
    type({"OFFSET", "500", "2500,0", "2500,500", ""});
    type({"TAGLIA", "", "2500,0", ""});
    type({"ESTENDI", "", "2500,0", ""});
    type({"RACCORDO", "2500,0", "5000,2000", ""});
    EXPECT_EQ(shapes<Wall>().size(), 2u);
    // Moving, rotating, stretching and scaling do work.
    const EntityId first = doc.entities().begin()->first;
    engine.select({first});
    type({"SPOSTA", "0,0", "100,0"});
    EXPECT_EQ(shapes<Wall>()[0].start, (Vec2{100, 0}));
    engine.select({first});
    type({"RUOTA", "0,0", "90"});
    EXPECT_NEAR(shapes<Wall>()[0].end.y, 5100, 1e-9);
    type({"STIRA", "-500,5000", "500,5200", "0,0", "0,1000"});
    EXPECT_NEAR(shapes<Wall>()[0].end.y, 6100, 1e-9);
    engine.select({first});
    type({"SCALA", "0,0", "2"});
    EXPECT_DOUBLE_EQ(shapes<Wall>()[0].thickness, 600);
}

TEST_F(Commands, StoriesFromTheCommandLine)
{
    type({"PIANO", "N", "Primo piano", "Q", "3200", "A", "2900", "N", ""});
    ASSERT_EQ(doc.stories().size(), 3u);
    EXPECT_EQ(doc.stories()[1].name, "Primo piano");
    EXPECT_DOUBLE_EQ(doc.stories()[1].elevation, 3200);
    EXPECT_DOUBLE_EQ(doc.stories()[1].height, 2900);
    // The next one goes on top, with a default name.
    EXPECT_EQ(doc.stories()[2].name, "Piano 2");
    EXPECT_DOUBLE_EQ(doc.stories()[2].elevation, 6100);
    EXPECT_EQ(doc.currentStory(), doc.stories()[2].id);
    type({"G", "G"});
    EXPECT_EQ(doc.currentStory(), doc.stories()[0].id);
    type({"C", "piano 2", "E"});
    EXPECT_EQ(doc.stories().size(), 2u);
    type({"R", "Sottotetto", ""});
    EXPECT_FALSE(engine.isActive());
    EXPECT_EQ(doc.story(doc.currentStory())->name, "Sottotetto");
}

TEST_F(Commands, HatchInsideARoomOfWalls)
{
    type({"MURO", "S", "200", "0,0", "6000,0", "6000,4000", "0,4000", "C"});
    type({"MURO", "S", "100", "3000,0", "3000,4000", ""});
    engine.setPickTolerance(10);
    type({"PORTA", "1500,50", "1500,500", ""});
    type({"TRATTEGGIO", "1000,2000", ""});
    auto hatches = shapes<Hatch>();
    ASSERT_EQ(hatches.size(), 1u);
    // The room on the left, between the inner faces of its walls.
    BBox box;
    for (const auto& loop : hatches[0].loops)
        box.add(boundingBox(Shape{loop}));
    EXPECT_NEAR(box.min.x, 100, 1e-6);
    EXPECT_NEAR(box.max.x, 2950, 1e-6);
    EXPECT_NEAR(box.max.y, 3900, 1e-6);
}

TEST_F(Commands, SlabInsideARoomAndUnderTheWalls)
{
    type({"MURO", "S", "200", "0,0", "6000,0", "6000,4000", "0,4000", "C"});
    // A door does not open the room.
    engine.setPickTolerance(10);
    type({"PORTA", "3000,50", "3000,500", ""});
    ASSERT_EQ(shapes<Wall>()[0].openings.size(), 1u);
    type({"SOLAIO", "S", "250", "3000,2000", ""});
    auto slabs = shapes<Slab>();
    ASSERT_EQ(slabs.size(), 1u);
    EXPECT_DOUBLE_EQ(slabs[0].thickness, 250);
    BBox box = boundingBox(slabs[0]);
    EXPECT_NEAR(box.min.x, 100, 1e-6);
    EXPECT_NEAR(box.max.y, 3900, 1e-6);

    type({"SOLAIO", "M", ""});
    slabs = shapes<Slab>();
    ASSERT_EQ(slabs.size(), 2u);
    box = boundingBox(slabs[1]);
    EXPECT_NEAR(box.min.x, -100, 1e-6);
    EXPECT_NEAR(box.max.y, 4100, 1e-6);

    type({"SOLAIO", "D", "0,0", "1000,0", "1000,1000", "C", ""});
    EXPECT_EQ(shapes<Slab>().size(), 3u);
    // A stairwell: a hole in the slab under the walls.
    type({"RETTANGOLO", "1000,1000", "2000,3000"});
    type({"SOLAIO", "F", "-100,2000", "1000,2000", ""});
    slabs = shapes<Slab>();
    ASSERT_EQ(slabs[1].loops.size(), 2u);
    type({"SOLAIO", "S", "300", ""});
}

TEST_F(Commands, RoofOverTheWalls)
{
    type({"MURO", "S", "300", "0,0", "8000,0", "8000,5000", "0,5000", "C"});
    type({"TETTO", "T", "C", "P", "35", "S", "400", "M", ""});
    auto roofs = shapes<Roof>();
    ASSERT_EQ(roofs.size(), 1u);
    const Roof& r = roofs[0];
    EXPECT_NEAR(r.pitch, degToRad(35.0), 1e-12);
    EXPECT_DOUBLE_EQ(r.overhang, 400);
    EXPECT_DOUBLE_EQ(r.base, 2700);  // the top of the walls
    const auto g = roofGeometry(r);
    ASSERT_TRUE(g);
    EXPECT_EQ(g->faces.size(), 2u);  // gable roof
    const BBox box = boundingBox(Polyline{g->footprint, true});
    EXPECT_NEAR(box.min.x, -150, 1e-6);
    EXPECT_NEAR(box.max.y, 5150, 1e-6);

    // Drawn outline: an L is fine, other concave shapes are refused.
    type({"TETTO", "T", "P", "0,0", "4000,0", "4000,2000", "2000,2000", "2000,4000", "0,4000", "C", ""});
    EXPECT_EQ(shapes<Roof>().size(), 2u);
    type({"TETTO", "0,0", "4000,0", "2000,1000", "2000,4000", "C", ""});
    EXPECT_EQ(shapes<Roof>().size(), 2u);
    type({"TETTO", "0,0", "4000,0", "4000,2000", ""});
    EXPECT_EQ(shapes<Roof>().size(), 3u);
    type({"TETTO", "P", "30", "S", "500", ""});
}

TEST_F(Commands, StairUpToTheNextStory)
{
    // 3000 high story: 17 risers, none over 18 cm.
    type({"SCALINATA", "L", "1100", "P", "270", "0,0", "0,1000"});
    auto stairs = shapes<Stair>();
    ASSERT_EQ(stairs.size(), 1u);
    EXPECT_EQ(stairs[0].risers, 17);
    EXPECT_DOUBLE_EQ(stairs[0].height, 3000);
    EXPECT_DOUBLE_EQ(stairs[0].width, 1100);
    EXPECT_DOUBLE_EQ(stairs[0].going, 270);
    EXPECT_NEAR(stairs[0].angle, kPi / 2.0, 1e-12);
    EXPECT_EQ(stairs[0].kind, Stair::Kind::Straight);
    EXPECT_FALSE(engine.isActive());

    type({"SN", "T", "U", "V", "D", "A", "2800", "5000,0", "6000,0"});
    stairs = shapes<Stair>();
    ASSERT_EQ(stairs.size(), 2u);
    EXPECT_EQ(stairs[1].kind, Stair::Kind::U);
    EXPECT_TRUE(stairs[1].turnRight);
    EXPECT_EQ(stairs[1].risers, 16);
    type({"SN", "T", "D", "V", "S", "L", "1000", "P", "280", ""});
}

TEST_F(Commands, CurrentColorLinetypeAndLineweight)
{
    type({"COLORE", "rosso", "TIPOLINEA", "nascosta"});
    // An unknown linetype keeps the command asking.
    EXPECT_TRUE(engine.isActive());
    type({"hidden", "SPESSLINEA", "0.5", "L", "0,0", "10,0", ""});
    const Entity& e = doc.entities().begin()->second;
    EXPECT_EQ(e.color, std::optional<Color>(aciToColor(1)));
    EXPECT_EQ(e.linetype, "HIDDEN");
    EXPECT_EQ(e.lineweight, 50);

    type({"COL", "DaLayer", "SCALATL", "25"});
    EXPECT_FALSE(doc.currentStyle().color.has_value());
    EXPECT_DOUBLE_EQ(doc.linetypeScale(), 25.0);
}

TEST_F(Commands, ChangePropertiesOfSelection)
{
    doc.addLayer(Layer::named("Assi"));
    type({"L", "0,0", "10,0", "", "CERCHIO", "0,0", "5"});
    std::vector<EntityId> ids;
    for (const auto& [id, e] : doc.entities())
        ids.push_back(id);
    engine.select(ids);
    type({"CAMBIAPROP", "C", "3", "LA", "assi", "TI", "CENTER", "SP", "0.7", ""});
    EXPECT_FALSE(engine.isActive());
    for (const auto& [id, e] : doc.entities()) {
        EXPECT_EQ(e.layer, "Assi");
        EXPECT_EQ(e.color, std::optional<Color>(aciToColor(3)));
        EXPECT_EQ(e.linetype, "CENTER");
        EXPECT_EQ(e.lineweight, 70);
    }
    // One undo step for the whole command.
    type({"U"});
    for (const auto& [id, e] : doc.entities())
        EXPECT_EQ(styleOf(e), EntityStyle{});
}

TEST_F(Commands, MatchPropertiesCopiesLayerAndStyle)
{
    doc.addLayer(Layer::named("Assi"));
    doc.setCurrentLayer("Assi");
    doc.setCurrentStyle({aciToColor(2), "DASHED", 35});
    type({"L", "0,0", "10,0", ""});
    doc.setCurrentLayer("0");
    doc.setCurrentStyle({});
    type({"L", "0,5", "10,5", ""});
    const EntityId source = doc.entities().begin()->first;
    const EntityId target = doc.entities().rbegin()->first;

    engine.setPickTolerance(0.5);
    type({"CORRISPPROP"});
    engine.pickPoint({5, 0});
    engine.select({target});
    type({""});
    EXPECT_FALSE(engine.isActive());
    EXPECT_EQ(doc.entity(target)->layer, "Assi");
    EXPECT_EQ(styleOf(*doc.entity(target)), styleOf(*doc.entity(source)));
}

TEST_F(Commands, GripsStretchEverySelectedObjectSharingThePoint)
{
    type({"L", "0,0", "10,0", "10,10", ""});
    std::vector<EntityId> ids;
    for (const auto& [id, e] : doc.entities())
        ids.push_back(id);
    engine.select(ids);
    EXPECT_EQ(engine.grips().size(), 6u);

    ASSERT_TRUE(engine.startGrip({10, 0}, 0.1));
    EXPECT_TRUE(engine.isActive());
    type({"12,1"});
    EXPECT_FALSE(engine.isActive());
    const auto lines = shapes<Line>();
    EXPECT_EQ(lines[0].b, (Vec2{12, 1}));
    EXPECT_EQ(lines[1].a, (Vec2{12, 1}));
    // The objects stay selected and the edit is one undo step.
    EXPECT_EQ(engine.selection().size(), 2u);
    type({"U"});
    EXPECT_EQ(shapes<Line>()[0].b, (Vec2{10, 0}));

    EXPECT_FALSE(engine.startGrip({50, 50}, 0.1));
}

TEST_F(Commands, ArrayRectangularAndPolar)
{
    type({"CERCHIO", "0,0", "1"});
    const EntityId circle = doc.entities().begin()->first;
    engine.select({circle});
    type({"SERIE", "R", "2", "3", "10", "20"});
    EXPECT_FALSE(engine.isActive());
    auto circles = shapes<Circle>();
    ASSERT_EQ(circles.size(), 6u);
    EXPECT_EQ(circles.back().center, (Vec2{40, 10}));

    type({"U"});
    engine.select({circle});
    type({"SERIE", "PO", "10,0", "4", "", ""});
    circles = shapes<Circle>();
    ASSERT_EQ(circles.size(), 4u);
    EXPECT_NEAR(circles[2].center.x, 20.0, 1e-9);
    EXPECT_NEAR(circles[2].center.y, 0.0, 1e-9);
}

TEST_F(Commands, ExplodeAndJoinKeepLayerAndStyle)
{
    doc.setCurrentStyle({aciToColor(1), "DASHED", 50});
    type({"RETTANGOLO", "0,0", "10,5"});
    engine.select({doc.entities().begin()->first});
    type({"ESPLODI"});
    ASSERT_EQ(shapes<Line>().size(), 4u);
    for (const auto& [id, e] : doc.entities())
        EXPECT_EQ(styleOf(e), (EntityStyle{aciToColor(1), "DASHED", 50}));

    std::vector<EntityId> ids;
    for (const auto& [id, e] : doc.entities())
        ids.push_back(id);
    engine.select(ids);
    type({"UNISCI"});
    const auto plines = shapes<Polyline>();
    ASSERT_EQ(plines.size(), 1u);
    EXPECT_TRUE(plines[0].closed);
    EXPECT_EQ(doc.entities().begin()->second.linetype, "DASHED");
}

TEST_F(Commands, EllipseSplineAndLeader)
{
    type({"ELLISSE", "-10,0", "10,0", "5"});
    ASSERT_EQ(shapes<Polyline>().size(), 1u);
    EXPECT_TRUE(shapes<Polyline>()[0].closed);
    type({"ELLISSE", "C", "0,0", "0,4", "8"});
    // The longer half-axis wins: an ellipse 16 by 8.
    const BBox box = boundingBox(shapes<Polyline>()[1]);
    EXPECT_NEAR(box.width(), 16.0, 1e-6);

    type({"SPLINE", "0,0", "10,10", "20,0", ""});
    EXPECT_EQ(shapes<Polyline>().size(), 3u);

    type({"DIRETTRICE", "0,0", "10,10", "20,10", "", "Vedi dettaglio"});
    EXPECT_FALSE(engine.isActive());
    ASSERT_EQ(shapes<Text>().size(), 1u);
    EXPECT_EQ(shapes<Text>()[0].content, "Vedi dettaglio");
    EXPECT_EQ(shapes<Hatch>().size(), 1u);
}

TEST_F(Commands, DistAreaAndListReport)
{
    QStringList messages;
    QObject::connect(&engine, &CommandEngine::messageAdded, [&](const QString& m) { messages << m; });
    type({"DIST", "0,0", "3,4"});
    EXPECT_TRUE(messages.last().startsWith(QStringLiteral("Distanza = 5")));
    type({"AREA", "0,0", "10,0", "10,10", "0,10", ""});
    EXPECT_TRUE(messages.last().startsWith(QStringLiteral("Area = 100,  Perimetro = 40")));

    type({"CERCHIO", "0,0", "1"});
    engine.setPickTolerance(0.1);
    type({"AREA", "O"});
    engine.pickPoint({1, 0});
    EXPECT_TRUE(messages.last().startsWith(QStringLiteral("Area = 3,14")));

    engine.select({doc.entities().begin()->first});
    type({"LISTA"});
    EXPECT_TRUE(messages.join(QLatin1Char('\n')).contains(QStringLiteral("CERCHIO  Layer: 0")));
}

TEST_F(Commands, SelectAllOrLastAtTheSelectionPrompt)
{
    type({"LINEA", "0,0", "10,0", "", "CERCHIO", "0,0", "5"});
    type({"SPOSTA", "U", "", "0,0", "0,10"});
    EXPECT_EQ(shapes<Circle>()[0].center, (Vec2{0, 10}));
    EXPECT_EQ(shapes<Line>()[0].a, (Vec2{0, 0}));
    type({"SPOSTA", "TUTTO", "", "0,0", "5,0"});
    EXPECT_EQ(shapes<Circle>()[0].center, (Vec2{5, 10}));
    EXPECT_EQ(shapes<Line>()[0].a, (Vec2{5, 0}));
}

TEST_F(Commands, ParagraphTextFromTheCommandLine)
{
    QString editor;
    QObject::connect(&engine, &CommandEngine::textEditorRequested, [&](const QString&, const QString& text) { editor = "?" + text; });
    type({"TESTOM", "0,100", "A", "5", "200,50"});
    EXPECT_EQ(editor, QStringLiteral("?"));
    EXPECT_TRUE(engine.wantsText());
    type({"Prima riga", "Seconda riga", ""});
    EXPECT_FALSE(engine.isActive());
    ASSERT_EQ(shapes<MText>().size(), 1u);
    const MText m = shapes<MText>()[0];
    EXPECT_EQ(m.content, "Prima riga\nSeconda riga");
    EXPECT_DOUBLE_EQ(m.width, 200);
    EXPECT_DOUBLE_EQ(m.height, 5);
    EXPECT_EQ(m.position, (Vec2{0, 100}));

    // The editor gives the whole paragraph at once, also to TEXTEDIT.
    type({"MT", "10,10", "L", "0"});
    engine.submitParagraph(QStringLiteral("a\nb\nc"));
    EXPECT_FALSE(engine.isActive());
    EXPECT_EQ(textLines(shapes<MText>()[1]).size(), 3u);
    type({"MODIFICATESTO", "1,99"});
    EXPECT_EQ(editor, QStringLiteral("?Prima riga\nSeconda riga"));
    engine.submitParagraph(QStringLiteral("Nuovo"));
    type({""});
    EXPECT_EQ(shapes<MText>()[0].content, "Nuovo");

    type({"ESPLODI", "TUTTO", ""});
    EXPECT_EQ(shapes<Text>().size(), 4u);
}

TEST_F(Commands, LayoutCommandCreatesSheets)
{
    int shown = 0;
    QObject::connect(&engine, &CommandEngine::layoutsRequested, [&] { ++shown; });
    type({"RETTANGOLO", "0,0", "12000,8000"});
    type({"TAVOLA", "", "", "", ""});
    ASSERT_EQ(doc.layouts().size(), 1u);
    EXPECT_EQ(doc.layouts()[0].name, "Tavola 1");
    EXPECT_EQ(doc.layouts()[0].paper, "A3");
    EXPECT_TRUE(doc.layouts()[0].landscape);
    EXPECT_DOUBLE_EQ(doc.layouts()[0].viewports[0].scale, 50);
    EXPECT_EQ(shown, 1);

    type({"LO", "N", "Dettaglio", "A4", "1:20"});
    ASSERT_EQ(doc.layouts().size(), 2u);
    EXPECT_EQ(doc.layouts()[1].paper, "A4");
    EXPECT_DOUBLE_EQ(doc.layouts()[1].viewports[0].scale, 20);
    EXPECT_EQ(doc.layouts()[1].titleBlock.number, "2");

    type({"TAVOLA", "E", "Tavola 1"});
    EXPECT_FALSE(engine.isActive());
    ASSERT_EQ(doc.layouts().size(), 1u);
    EXPECT_EQ(doc.layouts()[0].name, "Dettaglio");
}

TEST_F(Commands, SectionAndElevationAsBlocks)
{
    type({"MURO", "0,0", "4000,0", "4000,3000", "0,3000", "C"});
    // Across the two long walls, looking east.
    type({"SEZIONE", "2000,-1000", "2000,4000", "3000,1500", "10000,0"});
    EXPECT_FALSE(engine.isActive());
    const auto block = doc.block("SEZIONE A-A");
    ASSERT_NE(block, nullptr);
    int fills = 0;
    for (const BlockItem& item : block->items)
        fills += std::holds_alternative<Hatch>(item.shape);
    EXPECT_EQ(fills, 2);
    const auto inserts = shapes<Insert>();
    ASSERT_EQ(inserts.size(), 1u);
    EXPECT_EQ(inserts[0].position, (Vec2{10000, 0}));
    // The section line and its letters are in the plan.
    int letters = 0;
    for (const Text& t : shapes<Text>())
        letters += t.content == "A";
    EXPECT_EQ(letters, 2);

    type({"PROSPETTO", "N", "0,-8000"});
    EXPECT_NE(doc.block("PROSPETTO NORD"), nullptr);
    type({"PS", "", "0,-16000"});
    EXPECT_NE(doc.block("PROSPETTO SUD"), nullptr);
    type({"SZ", "-1000,1500", "5000,1500", "0,3000", "0,-20000"});
    EXPECT_NE(doc.block("SEZIONE B-B"), nullptr);
    EXPECT_EQ(shapes<Insert>().size(), 4u);
}

TEST_F(Commands, SectionNeedsABuilding)
{
    type({"LINEA", "0,0", "100,0", ""});
    type({"PROSPETTO", "S"});
    EXPECT_FALSE(engine.isActive());
    EXPECT_TRUE(doc.blocks().empty());
}
