// SPDX-License-Identifier: GPL-3.0-or-later
#include "dxf.h"
#include "nativeformat.h"

#include <gtest/gtest.h>

using namespace cad;

TEST(NativeFormat, RoundTrip)
{
    Document doc;
    doc.addLayer({"Muri", {200, 10, 30}, true, true});
    doc.addLayer({"Arredi", {0, 128, 255}, false, false});
    doc.setCurrentLayer("Muri");
    doc.add(Line{{0, 0}, {10.25, 0}});
    doc.add(Circle{{1, 2}, 3.5});
    doc.add(Entity{0, "Arredi", Arc{{0, 0}, 4, 0.5, 2.5}});
    doc.add(Polyline{{{0, 0}, {1, 0}, {1, 1}}, true});

    const QByteArray json = io::toNativeJson(doc);
    Document loaded;
    QString error;
    ASSERT_TRUE(io::fromNativeJson(json, loaded, &error)) << error.toStdString();
    EXPECT_TRUE(error.isEmpty());

    ASSERT_EQ(loaded.layers().size(), 3u);
    const Layer* muri = loaded.layer("Muri");
    ASSERT_NE(muri, nullptr);
    EXPECT_EQ(muri->color, (Color{200, 10, 30}));
    EXPECT_TRUE(muri->locked);
    EXPECT_FALSE(loaded.layer("Arredi")->visible);
    EXPECT_EQ(loaded.currentLayer(), "Muri");

    ASSERT_EQ(loaded.entities().size(), 4u);
    auto it = loaded.entities().begin();
    EXPECT_EQ(std::get<Line>(it->second.shape).b, (Vec2{10.25, 0}));
    ++it;
    EXPECT_DOUBLE_EQ(std::get<Circle>(it->second.shape).radius, 3.5);
    ++it;
    EXPECT_EQ(it->second.layer, "Arredi");
    EXPECT_DOUBLE_EQ(std::get<Arc>(it->second.shape).endAngle, 2.5);
    ++it;
    EXPECT_TRUE(std::get<Polyline>(it->second.shape).closed);

    // A freshly loaded drawing has no history and no pending changes.
    EXPECT_FALSE(loaded.canUndo());
    EXPECT_FALSE(loaded.isModified());
}

TEST(NativeFormat, RejectsOtherFiles)
{
    Document doc;
    doc.add(Line{{0, 0}, {1, 0}});
    QString error;
    EXPECT_FALSE(io::fromNativeJson("{\"format\":\"other\"}", doc, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(io::fromNativeJson("not json", doc, &error));
    // The drawing is untouched after a failed load.
    EXPECT_EQ(doc.entities().size(), 1u);
}

TEST(NativeFormat, SkipsUnknownEntities)
{
    const QByteArray json = R"({"format":"cad4all","version":1,"layers":[{"name":"0"}],
        "entities":[{"type":"spline","layer":"0"},{"type":"line","layer":"0","a":[0,0],"b":[1,1]}]})";
    Document doc;
    QString error;
    ASSERT_TRUE(io::fromNativeJson(json, doc, &error));
    EXPECT_EQ(doc.entities().size(), 1u);
    EXPECT_FALSE(error.isEmpty());
}

TEST(NativeFormat, TextAndDimensionRoundTrip)
{
    Document doc;
    doc.add(Text{{1, 2}, 3.5, 0.25, "Soggiorno è grande", Text::Align::Center});
    Dimension d;
    d.kind = Dimension::Kind::Radius;
    d.p1 = {0, 0};
    d.p2 = {5, 0};
    d.location = {9, 1};
    d.textHeight = 25;
    d.arrowSize = 20;
    d.text = "<> max";
    doc.add(d);

    Document loaded;
    QString error;
    ASSERT_TRUE(io::fromNativeJson(io::toNativeJson(doc), loaded, &error)) << error.toStdString();
    EXPECT_TRUE(error.isEmpty());
    ASSERT_EQ(loaded.entities().size(), 2u);
    auto it = loaded.entities().begin();
    const Text t = std::get<Text>(it->second.shape);
    EXPECT_EQ(t.content, "Soggiorno è grande");
    EXPECT_EQ(t.align, Text::Align::Center);
    EXPECT_DOUBLE_EQ(t.rotation, 0.25);
    EXPECT_DOUBLE_EQ(t.height, 3.5);
    ++it;
    const Dimension r = std::get<Dimension>(it->second.shape);
    EXPECT_EQ(r.kind, Dimension::Kind::Radius);
    EXPECT_EQ(r.location, (Vec2{9, 1}));
    EXPECT_DOUBLE_EQ(r.textHeight, 25.0);
    EXPECT_DOUBLE_EQ(r.arrowSize, 20.0);
    EXPECT_EQ(r.text, "<> max");
}

TEST(NativeFormat, WallRoundTrip)
{
    Document doc;
    Wall w;
    w.start = {0, 0};
    w.end = {4000, 1000};
    w.thickness = 250;
    w.height = 3000;
    w.justification = Wall::Justification::Right;
    w.openings.push_back(Opening{Opening::Kind::Door, 800, 900, 2100, 0, true, false});
    w.openings.push_back(Opening{Opening::Kind::Window, 2500, 1200, 1400, 900, false, false});
    doc.add(w);

    Document loaded;
    QString error;
    ASSERT_TRUE(io::fromNativeJson(io::toNativeJson(doc), loaded, &error)) << error.toStdString();
    const Wall& r = std::get<Wall>(loaded.entities().begin()->second.shape);
    EXPECT_EQ(r.end, (Vec2{4000, 1000}));
    EXPECT_DOUBLE_EQ(r.thickness, 250);
    EXPECT_DOUBLE_EQ(r.height, 3000);
    EXPECT_EQ(r.justification, Wall::Justification::Right);
    ASSERT_EQ(r.openings.size(), 2u);
    EXPECT_TRUE(r.openings[0].swingRight);
    EXPECT_EQ(r.openings[1].kind, Opening::Kind::Window);
    EXPECT_DOUBLE_EQ(r.openings[1].sill, 900);
}

TEST(NativeFormat, StoriesSlabsAndRoofsRoundTrip)
{
    Document doc;
    const StoryId first = doc.addStory({0, "Primo piano", 3000, 2800});
    doc.add(Slab{{Polyline{{{0, 0}, {5000, 0}, {5000, 4000}}, true}}, 250, -50});
    doc.setCurrentStory(first);
    Roof roof;
    roof.footprint = Polyline{{{0, 0}, {6000, 0}, {6000, 4000}, {0, 4000}}, true};
    roof.gables = {false, true, false, true};
    roof.pitch = 0.4;
    roof.base = 2600;
    roof.overhang = 600;
    roof.thickness = 300;
    doc.add(roof);

    Document loaded;
    QString error;
    ASSERT_TRUE(io::fromNativeJson(io::toNativeJson(doc), loaded, &error)) << error.toStdString();
    ASSERT_EQ(loaded.stories().size(), 2u);
    EXPECT_EQ(loaded.stories()[0].name, "Piano terra");
    EXPECT_EQ(loaded.stories()[1].name, "Primo piano");
    EXPECT_DOUBLE_EQ(loaded.stories()[1].elevation, 3000);
    EXPECT_DOUBLE_EQ(loaded.stories()[1].height, 2800);
    EXPECT_EQ(loaded.currentStory(), loaded.stories()[1].id);

    ASSERT_EQ(loaded.entities().size(), 2u);
    auto it = loaded.entities().begin();
    EXPECT_EQ(it->second.story, loaded.stories()[0].id);
    const auto& slab = std::get<Slab>(it->second.shape);
    ASSERT_EQ(slab.loops.size(), 1u);
    EXPECT_EQ(slab.loops[0].points.size(), 3u);
    EXPECT_DOUBLE_EQ(slab.thickness, 250);
    EXPECT_DOUBLE_EQ(slab.offset, -50);
    ++it;
    EXPECT_EQ(it->second.story, loaded.stories()[1].id);
    const auto& r = std::get<Roof>(it->second.shape);
    EXPECT_EQ(r.footprint.points.size(), 4u);
    EXPECT_EQ(r.gables, roof.gables);
    EXPECT_DOUBLE_EQ(r.pitch, 0.4);
    EXPECT_DOUBLE_EQ(r.base, 2600);
    EXPECT_DOUBLE_EQ(r.overhang, 600);
    EXPECT_DOUBLE_EQ(r.thickness, 300);
}

TEST(NativeFormat, FileWithoutStoriesUsesTheGroundFloor)
{
    Document doc;
    doc.add(Line{{0, 0}, {1, 0}});
    // An older file: no stories and no story on the entities.
    QByteArray json = io::toNativeJson(doc);
    Document loaded;
    ASSERT_TRUE(io::fromNativeJson(json, loaded));
    ASSERT_EQ(loaded.stories().size(), 1u);
    EXPECT_EQ(loaded.entities().begin()->second.story, loaded.currentStory());
}

TEST(NativeFormat, StairRoundTrip)
{
    Document doc;
    Stair s;
    s.start = {100, 200};
    s.angle = 0.5;
    s.kind = Stair::Kind::U;
    s.turnRight = true;
    s.width = 1100;
    s.height = 2900;
    s.risers = 16;
    s.going = 290;
    doc.add(s);
    Document loaded;
    ASSERT_TRUE(io::fromNativeJson(io::toNativeJson(doc), loaded));
    ASSERT_EQ(loaded.entities().size(), 1u);
    const auto& r = std::get<Stair>(loaded.entities().begin()->second.shape);
    EXPECT_EQ(r.start, s.start);
    EXPECT_DOUBLE_EQ(r.angle, 0.5);
    EXPECT_EQ(r.kind, Stair::Kind::U);
    EXPECT_TRUE(r.turnRight);
    EXPECT_DOUBLE_EQ(r.width, 1100);
    EXPECT_DOUBLE_EQ(r.height, 2900);
    EXPECT_EQ(r.risers, 16);
    EXPECT_DOUBLE_EQ(r.going, 290);
}

TEST(NativeFormat, ParagraphTextRoundTrip)
{
    Document doc;
    MText m;
    m.position = {1, 2};
    m.height = 3.5;
    m.width = 40;
    m.rotation = 0.25;
    m.content = "Riga uno\nRiga due";
    m.align = Text::Align::Center;
    m.vertical = MText::Vertical::Middle;
    m.lineSpacing = 1.5;
    doc.add(m);
    Document back;
    ASSERT_TRUE(io::fromNativeJson(io::toNativeJson(doc), back));
    const MText r = std::get<MText>(back.entities().begin()->second.shape);
    EXPECT_EQ(r.content, m.content);
    EXPECT_EQ(r.position, m.position);
    EXPECT_DOUBLE_EQ(r.width, 40);
    EXPECT_DOUBLE_EQ(r.rotation, 0.25);
    EXPECT_EQ(r.align, Text::Align::Center);
    EXPECT_EQ(r.vertical, MText::Vertical::Middle);
    EXPECT_DOUBLE_EQ(r.lineSpacing, 1.5);

    // R12 DXF has no MTEXT: each line becomes a TEXT.
    Document fromDxf;
    ASSERT_TRUE(io::fromDxf(io::toDxf(doc), fromDxf));
    EXPECT_EQ(fromDxf.entities().size(), 2u);
}
