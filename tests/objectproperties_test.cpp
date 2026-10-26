// SPDX-License-Identifier: GPL-3.0-or-later
#include "objectproperties.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace cad;

namespace {

const ObjectProperty* find(const std::vector<ObjectProperty>& props, const std::string& key)
{
    const auto it = std::find_if(props.begin(), props.end(), [&](const ObjectProperty& p) { return p.key == key; });
    return it == props.end() ? nullptr : &*it;
}

} // namespace

TEST(ObjectProperties, OneLine)
{
    Document doc;
    doc.addLayer(Layer::named("Muri"));
    const EntityId id = doc.add(Line{{0, 0}, {30, 40}});
    const auto props = objectProperties(doc, {id});
    EXPECT_EQ(selectionTitle(doc, {id}), "Linea");
    ASSERT_NE(find(props, "length"), nullptr);
    EXPECT_DOUBLE_EQ(find(props, "length")->number, 50.0);
    EXPECT_TRUE(find(props, "angle")->readOnly);
    EXPECT_EQ(find(props, "color")->text, "DaLayer");

    // Longer along the same direction, then on another layer and red: three undo steps.
    EXPECT_EQ(setObjectProperty(doc, {id}, "length", "100"), 1);
    EXPECT_EQ(std::get<Line>(doc.entity(id)->shape).b, (Vec2{60, 80}));
    EXPECT_EQ(setObjectProperty(doc, {id}, "layer", "1"), 1);
    EXPECT_EQ(doc.entity(id)->layer, "Muri");
    EXPECT_EQ(setObjectProperty(doc, {id}, "color", "rosso"), 1);
    EXPECT_EQ(doc.entity(id)->color, (Color{255, 0, 0}));
    EXPECT_EQ(setObjectProperty(doc, {id}, "color", "viola"), 0);
    EXPECT_EQ(setObjectProperty(doc, {id}, "angle", "10"), 0);
    EXPECT_EQ(setObjectProperty(doc, {id}, "length", "-3"), 0);
    doc.undo();
    doc.undo();
    EXPECT_EQ(doc.entity(id)->layer, "0");
    EXPECT_FALSE(doc.entity(id)->color.has_value());
}

TEST(ObjectProperties, SeveralObjects)
{
    Document doc;
    const EntityId a = doc.add(Circle{{0, 0}, 5});
    const EntityId b = doc.add(Circle{{10, 0}, 5});
    const EntityId c = doc.add(Line{{0, 0}, {1, 0}});

    // Two circles: common radius, different centres.
    auto props = objectProperties(doc, {a, b});
    EXPECT_EQ(selectionTitle(doc, {a, b}), "Cerchio (2)");
    EXPECT_FALSE(find(props, "radius")->varies);
    EXPECT_TRUE(find(props, "centerX")->varies);
    EXPECT_EQ(setObjectProperty(doc, {a, b}, "diameter", "4,5"), 2);
    EXPECT_DOUBLE_EQ(std::get<Circle>(doc.entity(b)->shape).radius, 2.25);
    doc.undo();
    EXPECT_DOUBLE_EQ(std::get<Circle>(doc.entity(a)->shape).radius, 5.0);

    // Different kinds: only the look.
    props = objectProperties(doc, {a, b, c});
    EXPECT_EQ(selectionTitle(doc, {a, b, c}), "Tutti (3)");
    EXPECT_EQ(find(props, "radius"), nullptr);
    ASSERT_NE(find(props, "lineweight"), nullptr);
    EXPECT_EQ(setObjectProperty(doc, {a, b, c}, "lineweight", "0.5"), 3);
    EXPECT_EQ(doc.entity(c)->lineweight, 50);
}

TEST(ObjectProperties, TextDimensionAndWall)
{
    Document doc;
    const EntityId text = doc.add(Text{{0, 0}, 2.5, 0.0, "Cucina", Text::Align::Left});
    EXPECT_EQ(setObjectProperty(doc, {text}, "content", "Soggiorno"), 1);
    EXPECT_EQ(std::get<Text>(doc.entity(text)->shape).content, "Soggiorno");
    EXPECT_EQ(setObjectProperty(doc, {text}, "align", "1"), 1);
    EXPECT_EQ(std::get<Text>(doc.entity(text)->shape).align, Text::Align::Center);

    Dimension d;
    d.p2 = {100, 0};
    d.location = {50, -10};
    const EntityId dim = doc.add(d);
    EXPECT_DOUBLE_EQ(find(objectProperties(doc, {dim}), "measurement")->number, 100.0);
    EXPECT_EQ(setObjectProperty(doc, {dim}, "dimText", "<> cm"), 1);
    EXPECT_EQ(std::get<Dimension>(doc.entity(dim)->shape).text, "<> cm");

    Wall w;
    w.end = {4000, 0};
    const EntityId wall = doc.add(w);
    const auto props = objectProperties(doc, {wall});
    EXPECT_DOUBLE_EQ(find(props, "wallLength")->number, 4000.0);
    EXPECT_EQ(setObjectProperty(doc, {wall}, "thickness", "250"), 1);
    EXPECT_DOUBLE_EQ(std::get<Wall>(doc.entity(wall)->shape).thickness, 250.0);
}
