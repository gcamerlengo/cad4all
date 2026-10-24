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
