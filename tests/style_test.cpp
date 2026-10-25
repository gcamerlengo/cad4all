// SPDX-License-Identifier: GPL-3.0-or-later
#include "document.h"
#include "dxf.h"
#include "nativeformat.h"
#include "stylenames.h"

#include <QList>

#include <gtest/gtest.h>

using namespace cad;

namespace {

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

const Entity& only(const Document& doc)
{
    return doc.entities().begin()->second;
}

} // namespace

TEST(Style, NamesAndParsing)
{
    EXPECT_EQ(colorName(std::nullopt), QStringLiteral("DaLayer"));
    EXPECT_EQ(colorName(aciToColor(1)), QStringLiteral("Rosso"));
    EXPECT_EQ(colorName(aciToColor(30)), QStringLiteral("30"));
    EXPECT_EQ(colorName(Color{12, 34, 56}), QStringLiteral("12,34,56"));

    EXPECT_EQ(parseColor(QStringLiteral("rosso")), std::optional<std::optional<Color>>(aciToColor(1)));
    EXPECT_EQ(parseColor(QStringLiteral("BLUE")), std::optional<std::optional<Color>>(aciToColor(5)));
    EXPECT_EQ(parseColor(QStringLiteral("30")), std::optional<std::optional<Color>>(aciToColor(30)));
    EXPECT_EQ(parseColor(QStringLiteral("12, 34,56")), std::optional<std::optional<Color>>(Color{12, 34, 56}));
    ASSERT_TRUE(parseColor(QStringLiteral("DaLayer")).has_value());
    EXPECT_FALSE(parseColor(QStringLiteral("DaLayer"))->has_value());
    EXPECT_FALSE(parseColor(QStringLiteral("0")).has_value());
    EXPECT_FALSE(parseColor(QStringLiteral("300,0,0")).has_value());
    EXPECT_FALSE(parseColor(QStringLiteral("viola")).has_value());

    EXPECT_EQ(parseLineweight(QStringLiteral("0.35")), 35);
    EXPECT_EQ(parseLineweight(QStringLiteral("0.33 mm")), 35);  // the nearest standard one
    EXPECT_EQ(parseLineweight(QStringLiteral("DaLayer")), kLineweightByLayer);
    EXPECT_EQ(parseLineweight(QStringLiteral("Predefinito")), kLineweightDefault);
    EXPECT_FALSE(parseLineweight(QStringLiteral("3")).has_value());
    EXPECT_EQ(lineweightName(50), QStringLiteral("0.50 mm"));
}

TEST(Style, EntitiesTakeTheLayerUnlessTheyHaveTheirOwn)
{
    Document doc;
    doc.addLayer({"Assi", aciToColor(1), true, false, "CENTER", 18});
    doc.setCurrentLayer("Assi");
    const EntityId byLayer = doc.add(Line{{0, 0}, {10, 0}});
    doc.setCurrentStyle({aciToColor(3), "dashed", 50});
    const EntityId own = doc.add(Line{{0, 1}, {10, 1}});

    const Entity& a = *doc.entity(byLayer);
    EXPECT_EQ(doc.colorOf(a), aciToColor(1));
    EXPECT_EQ(doc.linetypeOf(a).name, "CENTER");
    EXPECT_EQ(doc.lineweightOf(a), 18);

    const Entity& b = *doc.entity(own);
    EXPECT_EQ(doc.colorOf(b), aciToColor(3));
    EXPECT_EQ(doc.linetypeOf(b).name, "DASHED");  // names are kept in upper case
    EXPECT_EQ(doc.lineweightOf(b), 50);

    // Unknown linetypes fall back to continuous.
    doc.setCurrentStyle({std::nullopt, "NONESISTE", kLineweightDefault});
    EXPECT_EQ(doc.currentStyle().linetype, kByLayer);
}

TEST(Style, RestyleIsUndone)
{
    Document doc;
    const EntityId id = doc.add(Circle{{0, 0}, 5});
    doc.beginTransaction("PROPRIETA");
    doc.restyle(id, "Nuovo", {aciToColor(2), "HIDDEN", 70});
    doc.commitTransaction();
    EXPECT_EQ(doc.entity(id)->layer, "Nuovo");
    ASSERT_NE(doc.layer("Nuovo"), nullptr);
    EXPECT_EQ(doc.entity(id)->lineweight, 70);
    doc.undo();
    EXPECT_EQ(doc.entity(id)->layer, "0");
    EXPECT_FALSE(doc.entity(id)->color.has_value());
    EXPECT_EQ(doc.entity(id)->linetype, kByLayer);
}

TEST(Style, NativeRoundTrip)
{
    Document doc;
    doc.addLinetype({"BORDO", "Bordo __ __ .", {12.0, -6.0, 12.0, -6.0, 0.0, -6.0}});
    doc.addLayer({"Assi", aciToColor(1), true, false, "CENTER", 18});
    doc.setLinetypeScale(50.0);
    Entity e;
    e.layer = "Assi";
    e.shape = Line{{0, 0}, {1, 0}};
    e.color = Color{10, 20, 30};
    e.linetype = "BORDO";
    e.lineweight = 100;
    doc.add(e);
    doc.setCurrentStyle({aciToColor(4), "HIDDEN", 35});

    Document loaded;
    ASSERT_TRUE(io::fromNativeJson(io::toNativeJson(doc), loaded));
    const Entity& r = only(loaded);
    EXPECT_EQ(r.color, (std::optional<Color>{Color{10, 20, 30}}));
    EXPECT_EQ(r.linetype, "BORDO");
    EXPECT_EQ(r.lineweight, 100);
    ASSERT_NE(loaded.linetype("bordo"), nullptr);
    EXPECT_EQ(loaded.linetype("BORDO")->pattern.size(), 6u);
    EXPECT_EQ(loaded.layer("Assi")->linetype, "CENTER");
    EXPECT_EQ(loaded.layer("Assi")->lineweight, 18);
    EXPECT_DOUBLE_EQ(loaded.linetypeScale(), 50.0);
    EXPECT_EQ(loaded.currentStyle(), (EntityStyle{aciToColor(4), "HIDDEN", 35}));
}

TEST(Style, DxfReadsColoursLinetypesAndLineweights)
{
    const QByteArray data = dxf("0 SECTION|2 HEADER|9 $ACADVER|1 AC1015|9 $LTSCALE|40 20|0 ENDSEC|"
                                "0 SECTION|2 TABLES|0 TABLE|2 LTYPE|"
                                "0 LTYPE|2 BORDO|3 Bordo|72 65|73 3|40 30|49 12|49 -6|49 0|"
                                "0 ENDTAB|0 TABLE|2 LAYER|"
                                "0 LAYER|2 Assi|70 0|62 1|6 CENTER|370 18|"
                                "0 ENDTAB|0 ENDSEC|"
                                "0 SECTION|2 BLOCKS|"
                                "0 BLOCK|8 0|2 B|70 0|10 0|20 0|"
                                "0 LINE|8 0|62 0|6 BYBLOCK|370 -2|10 0|20 0|11 1|21 0|"
                                "0 ENDBLK|0 ENDSEC|"
                                "0 SECTION|2 ENTITIES|"
                                "0 LINE|8 Assi|10 0|20 0|11 1|21 0|"
                                "0 LINE|8 Assi|62 3|6 BORDO|370 50|10 0|20 1|11 1|21 1|"
                                "0 LINE|8 Assi|420 1193046|10 0|20 2|11 1|21 2|"
                                "0 INSERT|8 Assi|62 5|6 HIDDEN|370 70|2 B|10 0|20 3|"
                                "0 ENDSEC|0 EOF");
    Document doc;
    ASSERT_TRUE(io::fromDxf(data, doc));
    EXPECT_DOUBLE_EQ(doc.linetypeScale(), 20.0);
    ASSERT_NE(doc.linetype("BORDO"), nullptr);
    EXPECT_EQ(doc.linetype("BORDO")->pattern, (std::vector<double>{12, -6, 0}));
    EXPECT_EQ(doc.layer("Assi")->linetype, "CENTER");
    EXPECT_EQ(doc.layer("Assi")->lineweight, 18);

    std::vector<Entity> lines;
    for (const auto& [id, e] : doc.entities())
        lines.push_back(e);
    ASSERT_EQ(lines.size(), 4u);
    EXPECT_EQ(styleOf(lines[0]), EntityStyle{});
    EXPECT_EQ(styleOf(lines[1]), (EntityStyle{aciToColor(3), "BORDO", 50}));
    EXPECT_EQ(lines[2].color, (std::optional<Color>{Color{0x12, 0x34, 0x56}}));
    // BYBLOCK takes what the reference has.
    EXPECT_EQ(styleOf(lines[3]), (EntityStyle{aciToColor(5), "HIDDEN", 70}));
}

TEST(Style, DxfWritesColoursAndLinetypes)
{
    Document doc;
    doc.addLayer({"Assi", aciToColor(1), true, false, "CENTER", 18});
    Entity e;
    e.layer = "Assi";
    e.shape = Line{{0, 0}, {1, 0}};
    e.color = aciToColor(3);
    e.linetype = "HIDDEN";
    doc.add(e);
    doc.setLinetypeScale(10.0);

    Document back;
    ASSERT_TRUE(io::fromDxf(io::toDxf(doc), back));
    EXPECT_EQ(back.layer("Assi")->linetype, "CENTER");
    const Entity& r = only(back);
    EXPECT_EQ(r.color, std::optional<Color>(aciToColor(3)));
    EXPECT_EQ(r.linetype, "HIDDEN");
    EXPECT_DOUBLE_EQ(back.linetypeScale(), 10.0);
}
