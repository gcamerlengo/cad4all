// SPDX-License-Identifier: GPL-3.0-or-later
#include "dxf.h"
#include "hatch.h"

#include <QList>

#include <gtest/gtest.h>

using namespace cad;

namespace {

// Builds a DXF from "code value" pairs separated by '|'.
QByteArray dxf(const char* pairs)
{
    QByteArray out;
    for (const QByteArray& part : QByteArray(pairs).split('|')) {
        const QByteArray item = part.trimmed();
        const qsizetype space = item.indexOf(' ');
        out += item.left(space) + "\n" + (space < 0 ? QByteArray() : item.mid(space + 1)) + "\n";
    }
    return out;
}

template <class T>
std::vector<std::pair<std::string, T>> shapes(const Document& doc)
{
    std::vector<std::pair<std::string, T>> out;
    for (const auto& [id, e] : doc.entities())
        if (const auto* s = std::get_if<T>(&e.shape))
            out.push_back({e.layer, *s});
    return out;
}

} // namespace

TEST(Dxf, ReadsLayersAndBasicEntities)
{
    const QByteArray data = dxf("0 SECTION|2 HEADER|9 $ACADVER|1 AC1015|0 ENDSEC|"
                                "0 SECTION|2 TABLES|0 TABLE|2 LAYER|"
                                "0 LAYER|2 Muri|70 4|62 1|"
                                "0 LAYER|2 Nascosto|70 0|62 -3|0 ENDTAB|0 ENDSEC|"
                                "0 SECTION|2 ENTITIES|"
                                "0 LINE|8 Muri|10 0|20 0|30 0|11 100|21 0|31 0|"
                                "0 CIRCLE|8 0|10 5|20 5|40 2.5|"
                                "0 ARC|8 0|10 0|20 0|40 10|50 0|51 90|"
                                "0 LWPOLYLINE|8 0|90 3|70 1|10 0|20 0|42 1|10 10|20 0|10 10|20 10|"
                                "0 TEXT|8 0|10 1|20 2|40 3|1 Diametro %%c20|"
                                "0 ENDSEC|0 EOF");
    Document doc;
    QString message;
    ASSERT_TRUE(io::fromDxf(data, doc, &message)) << message.toStdString();
    EXPECT_TRUE(message.isEmpty()) << message.toStdString();

    const Layer* muri = doc.layer("Muri");
    ASSERT_NE(muri, nullptr);
    EXPECT_EQ(muri->color, (Color{255, 0, 0}));
    EXPECT_TRUE(muri->locked);
    EXPECT_FALSE(doc.layer("Nascosto")->visible);

    const auto lines = shapes<Line>(doc);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].first, "Muri");
    EXPECT_EQ(lines[0].second.b, (Vec2{100, 0}));
    EXPECT_DOUBLE_EQ(shapes<Circle>(doc)[0].second.radius, 2.5);
    EXPECT_NEAR(shapes<Arc>(doc)[0].second.endAngle, kPi / 2, 1e-12);

    const auto pls = shapes<Polyline>(doc);
    ASSERT_EQ(pls.size(), 1u);
    EXPECT_TRUE(pls[0].second.closed);
    EXPECT_DOUBLE_EQ(pls[0].second.bulge(0), 1.0);
    EXPECT_DOUBLE_EQ(pls[0].second.bulge(1), 0.0);

    EXPECT_EQ(shapes<Text>(doc)[0].second.content, "Diametro \xC3\x98" "20");
}

TEST(Dxf, KeepsBlockReferences)
{
    const QByteArray data = dxf("0 SECTION|2 BLOCKS|"
                                "0 BLOCK|8 0|2 PORTA|70 0|10 1|20 0|"
                                "0 LINE|8 0|10 1|20 0|11 2|21 0|"
                                "0 LINE|8 Arredi|10 1|20 0|11 1|21 1|"
                                "0 ENDBLK|0 ENDSEC|"
                                "0 SECTION|2 ENTITIES|"
                                "0 INSERT|8 Porte|2 PORTA|10 10|20 10|41 2|42 2|50 90|"
                                "0 ENDSEC|0 EOF");
    Document doc;
    ASSERT_TRUE(io::fromDxf(data, doc));
    ASSERT_NE(doc.block("PORTA"), nullptr);
    EXPECT_EQ(doc.block("PORTA")->items.size(), 2u);
    const auto refs = shapes<Insert>(doc);
    ASSERT_EQ(refs.size(), 1u);
    EXPECT_EQ(refs[0].first, "Porte");
    EXPECT_EQ(refs[0].second.block, doc.block("PORTA"));
    EXPECT_NEAR(refs[0].second.rotation, kPi / 2, 1e-12);
    EXPECT_DOUBLE_EQ(refs[0].second.scaleX, 2.0);

    // Base point (1, 0) goes to (10, 10); scale 2 and 90 degrees turn +X into +Y.
    std::vector<Entity> parts;
    doc.forEachPart(doc.entities().begin()->second, [&](const Entity& p) { parts.push_back(p); });
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].layer, "Porte");  // layer 0 takes the reference's layer
    const Line first = std::get<Line>(parts[0].shape);
    EXPECT_NEAR(first.a.x, 10, 1e-9);
    EXPECT_NEAR(first.b.x, 10, 1e-9);
    EXPECT_NEAR(first.b.y, 12, 1e-9);
    EXPECT_EQ(parts[1].layer, "Arredi");
    EXPECT_NEAR(std::get<Line>(parts[1].shape).b.x, 8, 1e-9);
}

TEST(Dxf, AttributesAndPoints)
{
    const QByteArray data = dxf("0 SECTION|2 BLOCKS|"
                                "0 BLOCK|8 0|2 SEDIA|70 2|10 0|20 0|"
                                "0 CIRCLE|8 0|10 0|20 0|40 200|"
                                "0 ATTDEF|8 0|10 0|20 0|40 50|1 -|3 Nome|2 NOME|70 0|"
                                "0 ENDBLK|0 ENDSEC|"
                                "0 SECTION|2 ENTITIES|"
                                "0 INSERT|8 0|66 1|2 SEDIA|10 100|20 100|"
                                "0 ATTRIB|8 0|10 100|20 100|40 50|1 Sedia 1|2 NOME|70 0|"
                                "0 ATTRIB|8 0|10 100|20 100|40 50|1 CODICE NASCOSTO|2 COD|70 1|"
                                "0 SEQEND|8 0|"
                                "0 POINT|8 Rilievo|10 5|20 5|"
                                "0 ENDSEC|0 EOF");
    Document doc;
    QString message;
    ASSERT_TRUE(io::fromDxf(data, doc, &message)) << message.toStdString();
    // The visible attribute is kept with its height, the invisible one is not drawn.
    const auto texts = shapes<Text>(doc);
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_EQ(texts[0].second.content, "Sedia 1");
    EXPECT_DOUBLE_EQ(texts[0].second.height, 50.0);
    // Survey points come in as points, on their layer, and go back out.
    const auto points = shapes<Point>(doc);
    ASSERT_EQ(points.size(), 1u);
    EXPECT_EQ(points[0].first, "Rilievo");
    EXPECT_EQ(points[0].second.position, (Vec2{5, 5}));
    EXPECT_FALSE(message.contains(QStringLiteral("POINT"))) << message.toStdString();
    Document back;
    ASSERT_TRUE(io::fromDxf(io::toDxf(doc), back));
    EXPECT_EQ(shapes<Point>(back).size(), 1u);
}

TEST(Dxf, AngularDimensionsKeepTheirDrawing)
{
    const QByteArray data = dxf("0 SECTION|2 BLOCKS|"
                                "0 BLOCK|8 0|2 *D7|70 1|10 0|20 0|"
                                "0 ARC|8 0|10 0|20 0|40 50|50 0|51 45|"
                                "0 MTEXT|8 0|10 40|20 20|40 2.5|1 45°|"
                                "0 ENDBLK|0 ENDSEC|"
                                "0 SECTION|2 ENTITIES|"
                                "0 DIMENSION|8 Quote|2 *D7|70 34|10 0|20 0|"
                                "0 ENDSEC|0 EOF");
    Document doc;
    QString message;
    ASSERT_TRUE(io::fromDxf(data, doc, &message)) << message.toStdString();
    EXPECT_FALSE(message.contains(QStringLiteral("DIMENSION"))) << message.toStdString();
    const auto arcs = shapes<Arc>(doc);
    ASSERT_EQ(arcs.size(), 1u);
    EXPECT_EQ(arcs[0].first, "Quote");
    EXPECT_EQ(shapes<MText>(doc).size(), 1u);
}

TEST(Dxf, ExplodesAnonymousBlocksAndArrays)
{
    const QByteArray data = dxf("0 SECTION|2 BLOCKS|"
                                "0 BLOCK|8 0|2 *U1|70 1|10 0|20 0|"
                                "0 LINE|8 0|10 0|20 0|11 1|21 0|"
                                "0 ENDBLK|"
                                "0 BLOCK|8 0|2 P|70 0|10 0|20 0|"
                                "0 CIRCLE|8 0|10 0|20 0|40 1|"
                                "0 ENDBLK|0 ENDSEC|"
                                "0 SECTION|2 ENTITIES|"
                                "0 INSERT|8 0|2 *U1|10 5|20 5|"
                                "0 INSERT|8 0|2 P|10 0|20 0|70 3|71 2|44 10|45 20|"
                                "0 ENDSEC|0 EOF");
    Document doc;
    ASSERT_TRUE(io::fromDxf(data, doc));
    EXPECT_EQ(doc.block("*U1"), nullptr);
    ASSERT_EQ(shapes<Line>(doc).size(), 1u);
    EXPECT_EQ(shapes<Line>(doc)[0].second.a, (Vec2{5, 5}));
    // A MINSERT of 3 columns and 2 rows: one reference per cell.
    const auto refs = shapes<Insert>(doc);
    ASSERT_EQ(refs.size(), 6u);
    EXPECT_EQ(refs.back().second.position, (Vec2{20, 20}));
}

TEST(Dxf, BlocksRoundTrip)
{
    Document doc;
    doc.addLayer(Layer::named("Arredi"));
    BlockItem line;
    line.shape = Line{{0, 0}, {10, 0}};
    BlockItem circle;
    circle.shape = Circle{{5, 5}, 2};
    circle.layer = "Arredi";
    circle.colorByBlock = true;
    const auto table = makeBlock("Tavolo", {5, 0}, {line, circle});
    doc.defineBlock(table);
    BlockItem nested;
    nested.shape = Insert{table, {0, 0}};
    doc.defineBlock(makeBlock("Sala", {0, 0}, {nested}));
    Insert ref{doc.block("Sala"), {100, 50}, 2.0, 2.0, kPi / 2};
    doc.add(ref);

    Document back;
    ASSERT_TRUE(io::fromDxf(io::toDxf(doc), back));
    ASSERT_NE(back.block("TAVOLO"), nullptr);
    ASSERT_NE(back.block("Sala"), nullptr);
    EXPECT_EQ(back.block("Tavolo")->base, (Vec2{5, 0}));
    const BlockItem& c = back.block("Tavolo")->items[1];
    EXPECT_EQ(c.layer, "Arredi");
    EXPECT_TRUE(c.colorByBlock);
    const auto refs = shapes<Insert>(back);
    ASSERT_EQ(refs.size(), 1u);
    EXPECT_EQ(refs[0].second.position, (Vec2{100, 50}));
    EXPECT_DOUBLE_EQ(refs[0].second.scaleY, 2.0);
    EXPECT_NEAR(refs[0].second.rotation, kPi / 2, 1e-12);
    const auto& inner = std::get<Insert>(refs[0].second.block->items[0].shape);
    EXPECT_EQ(inner.block, back.block("Tavolo"));
}

TEST(Dxf, ReadsMtextAndDimensions)
{
    const QByteArray data = dxf("0 SECTION|2 HEADER|9 $DIMSCALE|40 10|9 $DIMTXT|40 2.5|0 ENDSEC|"
                                "0 SECTION|2 ENTITIES|"
                                "0 MTEXT|8 0|10 0|20 10|40 2|71 7|1 {\\fArial;\\H1.2x;Cucina}\\P12 m\\U+00B2|"
                                "0 DIMENSION|8 0|10 50|20 20|11 50|21 22|70 32|13 0|23 0|14 100|24 0|50 0|"
                                "0 DIMENSION|8 0|10 0|20 0|11 20|21 20|70 36|15 10|25 0|1 <> max|"
                                "0 ENDSEC|0 EOF");
    Document doc;
    ASSERT_TRUE(io::fromDxf(data, doc));
    const auto paragraphs = shapes<MText>(doc);
    ASSERT_EQ(paragraphs.size(), 1u);
    EXPECT_EQ(paragraphs[0].second.content, "Cucina\n12 m\xC2\xB2");
    EXPECT_EQ(paragraphs[0].second.vertical, MText::Vertical::Bottom);
    const auto texts = textLines(paragraphs[0].second);
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[0].content, "Cucina");
    // Bottom-left attachment: the last line sits on the insertion point.
    EXPECT_NEAR(texts[1].position.y, 10, 1e-9);

    const auto dims = shapes<Dimension>(doc);
    ASSERT_EQ(dims.size(), 2u);
    EXPECT_EQ(dims[0].second.kind, Dimension::Kind::Linear);
    EXPECT_DOUBLE_EQ(dims[0].second.measurement(), 100.0);
    EXPECT_DOUBLE_EQ(dims[0].second.textHeight, 25.0);
    EXPECT_EQ(dims[1].second.kind, Dimension::Kind::Radius);
    EXPECT_EQ(dimensionGeometry(dims[1].second).label.content, "R10 max");
}

TEST(Dxf, RoundTrip)
{
    Document doc;
    doc.addLayer({"Muri", {255, 0, 0}, true, false});
    doc.add(Entity{0, "Muri", Line{{0, 0}, {10, 0}}});
    doc.add(Circle{{1, 2}, 3});
    doc.add(Arc{{0, 0}, 5, 0.5, 2.0});
    doc.add(Polyline{{{0, 0}, {10, 0}, {10, 5}}, false, {0.0, 0.5}});
    doc.add(Text{{1, 1}, 2.5, 0.0, "Città", Text::Align::Center});
    Dimension d;
    d.p1 = {0, 0};
    d.p2 = {10, 0};
    d.location = {5, -5};
    doc.add(d);

    for (const auto version : {io::DxfVersion::R12, io::DxfVersion::R2000}) {
        const QByteArray data = io::toDxf(doc, version);
        EXPECT_TRUE(data.contains(version == io::DxfVersion::R12 ? "AC1009" : "AC1015"));
        EXPECT_TRUE(data.contains("Citt\xE0"));

        Document back;
        QString message;
        ASSERT_TRUE(io::fromDxf(data, back, &message)) << message.toStdString();
        EXPECT_TRUE(message.isEmpty()) << message.toStdString();
        EXPECT_EQ(back.entities().size(), doc.entities().size());
        EXPECT_EQ(shapes<Line>(back)[0].first, "Muri");
        EXPECT_EQ(back.layer("Muri")->color, (Color{255, 0, 0}));
        EXPECT_NEAR(shapes<Arc>(back)[0].second.startAngle, 0.5, 1e-9);
        EXPECT_DOUBLE_EQ(shapes<Polyline>(back)[0].second.bulge(1), 0.5);
        EXPECT_EQ(shapes<Text>(back)[0].second.content, "Città");
        EXPECT_EQ(shapes<Text>(back)[0].second.align, Text::Align::Center);
        const auto dims = shapes<Dimension>(back);
        ASSERT_EQ(dims.size(), 1u);
        EXPECT_DOUBLE_EQ(dims[0].second.measurement(), 10.0);
    }
}

TEST(Dxf, Dxf2000KeepsStyleAndDimensionSize)
{
    Document doc;
    Layer muri = Layer::named("Muri");
    muri.color = {200, 100, 50};  // not an ACI colour
    muri.lineweight = 50;
    doc.addLayer(muri);
    Entity e{0, "Muri", Line{{0, 0}, {10, 0}}};
    e.color = Color{10, 20, 30};
    e.lineweight = 35;
    doc.add(e);
    Dimension d;
    d.p1 = {0, 0};
    d.p2 = {1000, 0};
    d.location = {500, -500};
    d.textHeight = 250;
    d.arrowSize = 150;
    doc.add(d);

    Document back;
    ASSERT_TRUE(io::fromDxf(io::toDxf(doc), back));
    EXPECT_EQ(back.layer("Muri")->color, (Color{200, 100, 50}));
    EXPECT_EQ(back.layer("Muri")->lineweight, 50);
    const Entity& line = back.entities().begin()->second;
    EXPECT_EQ(line.color, (Color{10, 20, 30}));
    EXPECT_EQ(line.lineweight, 35);
    const auto dims = shapes<Dimension>(back);
    ASSERT_EQ(dims.size(), 1u);
    EXPECT_DOUBLE_EQ(dims[0].second.textHeight, 250);
}

TEST(Dxf, RejectsBinaryAndGarbage)
{
    Document doc;
    doc.add(Line{{0, 0}, {1, 1}});
    QString message;
    EXPECT_FALSE(io::fromDxf("AutoCAD Binary DXF\r\n\x1a", doc, &message));
    EXPECT_FALSE(message.isEmpty());
    EXPECT_FALSE(io::fromDxf("questo non e' un dxf\nciao\n", doc, &message));
    EXPECT_EQ(doc.entities().size(), 1u);  // untouched
}

TEST(Dxf, AciColours)
{
    EXPECT_EQ(io::aciToColor(1), (Color{255, 0, 0}));
    EXPECT_EQ(io::colorToAci({255, 0, 0}), 1);
    EXPECT_EQ(io::colorToAci({255, 255, 255}), 7);
    EXPECT_EQ(io::colorToAci(io::aciToColor(140)), 140);
}

TEST(Dxf, SplinesAndDimensionSizeFromItsBlock)
{
    const QByteArray data = dxf("0 SECTION|2 BLOCKS|"
                                "0 BLOCK|8 0|2 *D1|70 1|10 0|20 0|"
                                "0 MTEXT|8 0|10 50|20 5|40 20|1 100|"
                                "0 ENDBLK|0 ENDSEC|"
                                "0 SECTION|2 ENTITIES|"
                                // Fit points only: the curve passes through them smoothly.
                                "0 SPLINE|8 0|70 8|71 3|74 3|11 0|21 0|11 10|21 10|11 20|21 0|"
                                // Clamped quadratic B-spline: starts and ends on the control polygon.
                                "0 SPLINE|8 0|70 8|71 2|72 6|73 3|40 0|40 0|40 0|40 1|40 1|40 1|"
                                "10 0|20 0|10 10|20 10|10 20|20 0|"
                                "0 DIMENSION|8 0|2 *D1|3 STANDARD|70 0|10 0|20 5|11 50|21 5|13 0|23 0|14 100|24 0|50 0|"
                                "0 ENDSEC|0 EOF");
    Document doc;
    ASSERT_TRUE(io::fromDxf(data, doc));
    const auto curves = shapes<Polyline>(doc);
    ASSERT_EQ(curves.size(), 2u);
    for (const auto& [layer, pl] : curves) {
        EXPECT_GT(pl.points.size(), 10u);
        EXPECT_EQ(pl.points.front(), (Vec2{0, 0}));
        EXPECT_NEAR(pl.points.back().x, 20.0, 1e-9);
    }
    // Through the middle fit point; the B-spline peaks halfway at y = 5.
    EXPECT_NEAR(distanceTo(curves[0].second, {10, 10}), 0.0, 1e-9);
    EXPECT_NEAR(boundingBox(curves[1].second).max.y, 5.0, 1e-6);

    const auto dims = shapes<Dimension>(doc);
    ASSERT_EQ(dims.size(), 1u);
    EXPECT_NEAR(dims[0].second.textHeight, 20.0, 1e-9);
    EXPECT_NEAR(dims[0].second.measurement(), 100.0, 1e-9);
}

TEST(Dxf, ReadsHatches)
{
    const QByteArray data = dxf("0 SECTION|2 ENTITIES|"
                                // Polyline path with a hole, ANSI31 at scale 2: lines as stored in the file.
                                "0 HATCH|8 0|10 0|20 0|30 0|2 ANSI31|70 0|71 0|91 2|"
                                "92 3|72 0|73 1|93 4|10 0|20 0|10 100|20 0|10 100|20 50|10 0|20 50|97 0|"
                                "92 3|72 0|73 1|93 4|10 20|20 10|10 40|20 10|10 40|20 30|10 20|20 30|97 0|"
                                "75 1|76 1|52 0|41 2|77 0|78 1|53 45|43 0|44 0|45 -4.490128|46 4.490128|79 0|98 0|"
                                // Edge path: line, counter-clockwise half circle, line, clockwise half circle.
                                "0 HATCH|8 0|10 0|20 0|30 0|2 SOLID|70 1|71 0|91 1|92 1|93 5|"
                                "72 1|10 150|20 0|11 250|21 0|"
                                "72 2|10 250|20 25|40 25|50 -90|51 90|73 1|"
                                "72 1|10 250|20 50|11 200|21 50|"
                                "72 2|10 175|20 50|40 25|50 180|51 360|73 0|"
                                "72 1|10 150|20 50|11 150|21 0|97 0|75 1|76 1|98 0|"
                                "0 ENDSEC|0 EOF");
    Document doc;
    QString message;
    ASSERT_TRUE(io::fromDxf(data, doc, &message)) << message.toStdString();
    const auto hatches = shapes<Hatch>(doc);
    ASSERT_EQ(hatches.size(), 2u);

    const Hatch& lines = hatches[0].second;
    EXPECT_EQ(lines.pattern, "ANSI31");
    EXPECT_DOUBLE_EQ(lines.scale, 2.0);
    ASSERT_EQ(lines.loops.size(), 2u);
    ASSERT_EQ(lines.lines.size(), 1u);
    EXPECT_NEAR(lines.lines[0].offset.y, 3.175, 1e-6);  // back to the definition
    EXPECT_NEAR(hatchSpacing(lines), 6.35, 1e-6);
    EXPECT_FALSE(insideHatch(lines, {30, 20}));
    EXPECT_TRUE(insideHatch(lines, {10, 20}));

    const Hatch& solid = hatches[1].second;
    EXPECT_TRUE(solid.solid());
    ASSERT_EQ(solid.loops.size(), 1u);
    EXPECT_TRUE(insideHatch(solid, {260, 25}));   // inside the outward half circle
    EXPECT_FALSE(insideHatch(solid, {175, 45}));  // in the bite of the clockwise one
    EXPECT_TRUE(insideHatch(solid, {155, 10}));
}

TEST(Dxf, Dxf2000WritesRealHatches)
{
    Document doc;
    const Hatch pattern = makeHatch({Polyline{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}, true}}, "BRICK", 10, 0.5);
    doc.add(pattern);
    doc.add(makeHatch({Polyline{{{200, 0}, {300, 0}, {250, 80}}, true}}, "SOLID", 1, 0));
    Document back;
    ASSERT_TRUE(io::fromDxf(io::toDxf(doc), back));
    const auto hatches = shapes<Hatch>(back);
    ASSERT_EQ(hatches.size(), 2u);
    const Hatch& h = hatches[0].second;
    EXPECT_EQ(h.pattern, "BRICK");
    EXPECT_NEAR(h.scale, 10, 1e-9);
    EXPECT_NEAR(h.angle, 0.5, 1e-9);
    // The same lines are drawn.
    const BBox all = BBox::fromCorners({-1e9, -1e9}, {1e9, 1e9});
    const auto before = hatchLines(pattern, all, 100000);
    const auto after = hatchLines(h, all, 100000);
    ASSERT_EQ(before.size(), after.size());
    EXPECT_TRUE(hatches[1].second.solid());
}

TEST(Dxf, WritesHatchesAsBlocks)
{
    Document doc;
    doc.add(makeHatch({Polyline{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}, true}}, "ANSI31", 10, 0));
    doc.add(makeHatch({Polyline{{{200, 0}, {300, 0}, {250, 80}}, true}}, "SOLID", 1, 0));
    const QByteArray out = io::toDxf(doc, io::DxfVersion::R12);
    EXPECT_TRUE(out.contains("*X1"));
    EXPECT_TRUE(out.contains("*X2"));
    EXPECT_TRUE(out.contains("SOLID"));
    // Reading it back gives the drawn lines and fills.
    Document back;
    ASSERT_TRUE(io::fromDxf(out, back));
    EXPECT_GT(shapes<Line>(back).size(), 3u);
    EXPECT_FALSE(shapes<Polyline>(back).empty());
}

TEST(Dxf, WritesWallsAsTheirPlan)
{
    Document doc;
    Wall a;
    a.start = {0, 0};
    a.end = {3000, 0};
    Opening door;
    door.position = 1500;
    a.openings.push_back(door);
    Wall b = a;
    b.start = {3000, 0};
    b.end = {3000, 2000};
    b.openings.clear();
    doc.add(a);
    doc.add(b);
    Document back;
    ASSERT_TRUE(io::fromDxf(io::toDxf(doc, io::DxfVersion::R12), back));
    // Wall a: 4 face pieces, 1 free end, 2 jambs, the leaf; wall b: 2 faces, 1 end.
    EXPECT_EQ(shapes<Line>(back).size(), 11u);
    EXPECT_EQ(shapes<Arc>(back).size(), 1u);

    // DXF 2000: the same drawing in a block, and CAD4All reads the walls back.
    const QByteArray data = io::toDxf(doc);
    EXPECT_TRUE(data.contains("CAD4ALL"));
    Document walls;
    ASSERT_TRUE(io::fromDxf(data, walls));
    const auto read = shapes<Wall>(walls);
    ASSERT_EQ(read.size(), 2u);
    EXPECT_EQ(read[0].second.end, (Vec2{3000, 0}));
    ASSERT_EQ(read[0].second.openings.size(), 1u);
    EXPECT_DOUBLE_EQ(read[0].second.openings[0].position, 1500);

    // A program that moves the block moves the wall: the reference moved by 100 in X.
    QByteArray moved = data;
    const qsizetype insert = moved.indexOf("INSERT");
    const QByteArray origin = " 10\r\n0.0\r\n";
    const qsizetype x = moved.indexOf(origin, insert);
    ASSERT_GT(x, 0);
    moved.replace(x, origin.size(), " 10\r\n100.0\r\n");
    Document shifted;
    ASSERT_TRUE(io::fromDxf(moved, shifted));
    EXPECT_EQ(shapes<Wall>(shifted)[0].second.start, (Vec2{100, 0}));

    // Other programs see the plan: without CAD4All's data the block is drawn.
    QByteArray plain = data;
    plain.replace("CAD4ALL", "ALTRO");
    Document drawn;
    ASSERT_TRUE(io::fromDxf(plain, drawn));
    EXPECT_TRUE(shapes<Wall>(drawn).empty());
    EXPECT_EQ(shapes<Line>(drawn).size(), 11u);
}

TEST(Dxf, WritesOnlyTheCurrentStory)
{
    Document doc;
    doc.add(Line{{0, 0}, {1, 0}});
    doc.setCurrentStory(doc.addStory({0, "Primo piano", 3000, 3000}));
    doc.add(Circle{{0, 0}, 1});
    Roof roof;
    roof.footprint = Polyline{{{0, 0}, {6000, 0}, {6000, 4000}, {0, 4000}}, true};
    doc.add(roof);
    Document back;
    ASSERT_TRUE(io::fromDxf(io::toDxf(doc, io::DxfVersion::R12), back));
    EXPECT_TRUE(shapes<Line>(back).size() >= 4u);  // the roof: hips and the ridge
    EXPECT_EQ(shapes<Circle>(back).size(), 1u);
    for (const auto& [layer, l] : shapes<Line>(back))
        EXPECT_FALSE(l.a == (Vec2{0, 0}) && l.b == (Vec2{1, 0}));
}
