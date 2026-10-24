// SPDX-License-Identifier: GPL-3.0-or-later
// Drives the command engine the way a user does from the command line.
#include "commandengine.h"

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
