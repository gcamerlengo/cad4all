// SPDX-License-Identifier: GPL-3.0-or-later
#include "dxf.h"

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

TEST(Dxf, ExplodesBlockReferences)
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
    const auto lines = shapes<Line>(doc);
    ASSERT_EQ(lines.size(), 2u);
    // Base point (1, 0) goes to (10, 10); scale 2 and 90 degrees turn +X into +Y.
    EXPECT_EQ(lines[0].first, "Porte");  // layer 0 takes the reference's layer
    EXPECT_NEAR(lines[0].second.a.x, 10, 1e-9);
    EXPECT_NEAR(lines[0].second.b.x, 10, 1e-9);
    EXPECT_NEAR(lines[0].second.b.y, 12, 1e-9);
    EXPECT_EQ(lines[1].first, "Arredi");
    EXPECT_NEAR(lines[1].second.b.x, 8, 1e-9);
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
    const auto texts = shapes<Text>(doc);
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[0].second.content, "Cucina");
    EXPECT_EQ(texts[1].second.content, "12 m\xC2\xB2");
    // Bottom-left attachment: the last line sits on the insertion point.
    EXPECT_NEAR(texts[1].second.position.y, 10, 1e-9);

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

    const QByteArray data = io::toDxf(doc);
    EXPECT_TRUE(data.contains("AC1009"));
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
