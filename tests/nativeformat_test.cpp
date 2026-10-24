// SPDX-License-Identifier: GPL-3.0-or-later
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
