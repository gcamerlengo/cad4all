// SPDX-License-Identifier: GPL-3.0-or-later
#include "document.h"
#include "layout.h"
#include "nativeformat.h"

#include <gtest/gtest.h>

using namespace cad;

TEST(Layout, StandardScales)
{
    EXPECT_DOUBLE_EQ(standardScale(1), 1);
    EXPECT_DOUBLE_EQ(standardScale(1.5), 2);
    EXPECT_DOUBLE_EQ(standardScale(21), 25);
    EXPECT_DOUBLE_EQ(standardScale(30), 50);
    EXPECT_DOUBLE_EQ(standardScale(51), 100);
    EXPECT_DOUBLE_EQ(standardScale(100), 100);
    EXPECT_DOUBLE_EQ(standardScale(0), 1);
}

TEST(Layout, SheetSizesAndDrawingArea)
{
    Layout l;
    l.paper = "A3";
    l.landscape = true;
    EXPECT_EQ(l.size(), (Vec2{420, 297}));
    l.landscape = false;
    EXPECT_EQ(l.size(), (Vec2{297, 420}));
    l.paper = "A1";
    l.landscape = true;
    EXPECT_EQ(l.size(), (Vec2{841, 594}));
    // The title block takes the bottom of the frame.
    const BBox area = drawingArea(l);
    EXPECT_DOUBLE_EQ(area.min.y, kSheetMargin + kTitleBlockHeight);
    EXPECT_DOUBLE_EQ(area.max.x, 841 - kSheetMargin);
    l.hasTitleBlock = false;
    EXPECT_DOUBLE_EQ(drawingArea(l).min.y, kSheetMargin);
    EXPECT_EQ(sheetFrame(l).size(), 1u);
}

TEST(Layout, FitsTheDrawingAtAStandardScale)
{
    // A 12 x 8 m house drawn in millimetres on an A3: 1:50 fits.
    const BBox house = BBox::fromCorners({0, 0}, {12000, 8000});
    Layout l = makeLayout("Pianta", "A3", true, house, 0.0, 1.0, 0);
    ASSERT_EQ(l.viewports.size(), 1u);
    const Viewport& v = l.viewports.front();
    EXPECT_DOUBLE_EQ(v.scale, 50);
    EXPECT_EQ(v.center, (Vec2{6000, 4000}));
    const BBox shown = viewportArea(v, l.unitMm);
    EXPECT_LE(shown.min.x, 0);
    EXPECT_LE(shown.min.y, 0);
    EXPECT_GE(shown.max.x, 12000);
    EXPECT_GE(shown.max.y, 8000);

    // Drawn in metres the scale is the same.
    Layout m = makeLayout("Pianta", "A3", true, BBox::fromCorners({0, 0}, {12, 8}), 0.0, 1000.0, 0);
    EXPECT_DOUBLE_EQ(m.viewports.front().scale, 50);

    // A given scale is kept.
    Layout fixed = makeLayout("Dettaglio", "A4", false, house, 20.0, 1.0, 0);
    EXPECT_DOUBLE_EQ(fixed.viewports.front().scale, 20);
    EXPECT_EQ(fixed.paper, "A4");

    // The title block shows the name, the scale and the fields.
    l.titleBlock.number = "3";
    bool scale = false, number = false;
    for (const Shape& s : sheetFrame(l))
        if (const Text* t = std::get_if<Text>(&s)) {
            scale = scale || t->content == "1:50";
            number = number || t->content == "3";
        }
    EXPECT_TRUE(scale);
    EXPECT_TRUE(number);
}

TEST(Layout, DocumentKeepsSheetsByName)
{
    Document doc;
    EXPECT_TRUE(doc.addLayout(makeLayout("Tavola 1", "A3", true, {}, 100, 1, 0)));
    EXPECT_FALSE(doc.addLayout(makeLayout("Tavola 1", "A2", true, {}, 100, 1, 0)));
    EXPECT_TRUE(doc.addLayout(makeLayout("Tavola 2", "A2", true, {}, 100, 1, 0)));
    Layout renamed = *doc.layout("Tavola 2");
    renamed.name = "Tavola 1";
    EXPECT_FALSE(doc.updateLayout("Tavola 2", renamed));
    renamed.name = "Prospetti";
    EXPECT_TRUE(doc.updateLayout("Tavola 2", renamed));
    EXPECT_NE(doc.layout("Prospetti"), nullptr);
    EXPECT_TRUE(doc.removeLayout("Tavola 1"));
    EXPECT_EQ(doc.layouts().size(), 1u);
    doc.reset();
    EXPECT_TRUE(doc.layouts().empty());
}

TEST(Layout, NativeRoundTrip)
{
    Document doc;
    Layout l = makeLayout("Piano terra", "A1", false, BBox::fromCorners({0, 0}, {20000, 9000}), 0.0, 1.0, 0);
    l.titleBlock = {"Pianta piano terra", "Casa Rossi", "2", "25/10/2026", "G. C."};
    l.viewports.front().story = doc.currentStory();
    doc.addLayout(l);
    Layout plain = makeLayout("Vuota", "A4", true, {}, 200, 10, 0);
    plain.hasTitleBlock = false;
    doc.addLayout(plain);

    Document back;
    ASSERT_TRUE(io::fromNativeJson(io::toNativeJson(doc), back));
    ASSERT_EQ(back.layouts().size(), 2u);
    const Layout& r = back.layouts()[0];
    EXPECT_EQ(r.name, "Piano terra");
    EXPECT_EQ(r.paper, "A1");
    EXPECT_FALSE(r.landscape);
    EXPECT_EQ(r.titleBlock.project, "Casa Rossi");
    EXPECT_EQ(r.titleBlock.author, "G. C.");
    ASSERT_EQ(r.viewports.size(), 1u);
    EXPECT_DOUBLE_EQ(r.viewports[0].scale, l.viewports[0].scale);
    EXPECT_EQ(r.viewports[0].center, l.viewports[0].center);
    EXPECT_EQ(r.viewports[0].sheet.min, l.viewports[0].sheet.min);
    EXPECT_EQ(r.viewports[0].story, doc.currentStory());
    const Layout& p = back.layouts()[1];
    EXPECT_FALSE(p.hasTitleBlock);
    EXPECT_DOUBLE_EQ(p.unitMm, 10);
    EXPECT_DOUBLE_EQ(p.viewports[0].scale, 200);
}
