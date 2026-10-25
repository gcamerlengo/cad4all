// SPDX-License-Identifier: GPL-3.0-or-later
// The parameters the Info Box shows and edits, for tools and objects.
#include "properties.h"
#include "commandengine.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <optional>

using namespace cad;

namespace {

const double kAuto = std::numeric_limits<double>::quiet_NaN();

// A copy: the lists are often temporaries.
std::optional<Property> find(const std::vector<Property>& props, const std::string& key)
{
    for (const Property& p : props)
        if (p.key == key)
            return p;
    return std::nullopt;
}

// The tool settings are shared by the whole program: every test starts
// from the defaults and leaves them as it found them.
class Properties : public ::testing::Test {
protected:
    void SetUp() override { toolSettings() = ToolSettings{}; }
    void TearDown() override { toolSettings() = saved_; }

private:
    ToolSettings saved_ = toolSettings();
};

} // namespace

TEST_F(Properties, ToolsWithSettings)
{
    for (const char* c : {"WALL", "DOOR", "WINDOW", "SLAB", "ROOF", "STAIR"}) {
        EXPECT_TRUE(hasToolProperties(QString::fromLatin1(c))) << c;
        EXPECT_FALSE(toolTitle(QString::fromLatin1(c)).isEmpty()) << c;
        EXPECT_FALSE(toolProperties(QString::fromLatin1(c)).empty()) << c;
    }
    EXPECT_FALSE(hasToolProperties(QStringLiteral("LINE")));
    EXPECT_TRUE(toolProperties(QStringLiteral("LINE")).empty());
}

TEST_F(Properties, ToolValuesRoundTrip)
{
    EXPECT_TRUE(setToolProperty(QStringLiteral("WALL"), "thickness", 250));
    EXPECT_TRUE(setToolProperty(QStringLiteral("WALL"), "justification", 2));
    EXPECT_FALSE(setToolProperty(QStringLiteral("WALL"), "thickness", -5));
    EXPECT_FALSE(setToolProperty(QStringLiteral("WALL"), "pitch", 30));
    const auto wall = toolProperties(QStringLiteral("WALL"));
    EXPECT_DOUBLE_EQ(find(wall, "thickness")->value, 250);
    EXPECT_DOUBLE_EQ(find(wall, "justification")->value, 2);
    EXPECT_EQ(find(wall, "justification")->type, Property::Type::Choice);
    EXPECT_EQ(find(wall, "justification")->choices.size(), 3);

    EXPECT_TRUE(setToolProperty(QStringLiteral("WINDOW"), "sill", 1000));
    EXPECT_DOUBLE_EQ(toolSettings().window.sill, 1000);
    EXPECT_FALSE(find(toolProperties(QStringLiteral("DOOR")), "sill"));

    // A height left to the program shows as automatic.
    const auto height = find(toolProperties(QStringLiteral("STAIR")), "height");
    ASSERT_TRUE(height);
    EXPECT_TRUE(std::isnan(height->value));
    EXPECT_FALSE(height->automatic.isEmpty());
    EXPECT_TRUE(setToolProperty(QStringLiteral("STAIR"), "height", 2800));
    EXPECT_EQ(toolSettings().stairHeight, 2800.0);
    EXPECT_TRUE(setToolProperty(QStringLiteral("STAIR"), "height", kAuto));
    EXPECT_FALSE(toolSettings().stairHeight);

    EXPECT_TRUE(setToolProperty(QStringLiteral("ROOF"), "style", 1));
    EXPECT_EQ(toolSettings().roofStyle, RoofStyle::Gable);
    EXPECT_EQ(find(toolProperties(QStringLiteral("ROOF")), "pitch")->type, Property::Type::Angle);
}

TEST_F(Properties, WallCommandUsesToolSettings)
{
    Document doc;
    CommandEngine engine(doc);
    setToolProperty(QStringLiteral("WALL"), "thickness", 200);
    setToolProperty(QStringLiteral("WALL"), "height", 3000);
    for (const char* in : {"MURO", "0,0", "5000,0", ""})
        engine.submit(QString::fromUtf8(in));
    ASSERT_EQ(doc.entities().size(), 1u);
    const auto& w = std::get<Wall>(doc.entities().begin()->second.shape);
    EXPECT_DOUBLE_EQ(w.thickness, 200);
    EXPECT_DOUBLE_EQ(w.height, 3000);

    // Options typed on the command line show up in the Info Box too.
    for (const char* in : {"MURO", "S", "350", ""})
        engine.submit(QString::fromUtf8(in));
    EXPECT_DOUBLE_EQ(find(toolProperties(QStringLiteral("WALL")), "thickness")->value, 350);
}

TEST_F(Properties, EditWall)
{
    Wall w;
    w.end = {4000, 0};
    EXPECT_EQ(shapeTitle(w), QStringLiteral("Muro"));
    const auto edited = withProperty(w, "height", 3200);
    ASSERT_TRUE(edited);
    EXPECT_DOUBLE_EQ(std::get<Wall>(*edited).height, 3200);
    EXPECT_FALSE(withProperty(w, "height", 0));
    EXPECT_FALSE(withProperty(w, "going", 300));
    EXPECT_TRUE(shapeProperties(Line{{0, 0}, {1, 1}}).empty());
    EXPECT_TRUE(shapeTitle(Line{{0, 0}, {1, 1}}).isEmpty());
}

TEST_F(Properties, EditRoofStyle)
{
    Roof r;
    r.footprint = Polyline{{{0, 0}, {8000, 0}, {8000, 4000}, {0, 4000}}, true};
    EXPECT_DOUBLE_EQ(find(shapeProperties(r), "style")->value, 0);  // hip: no gables
    EXPECT_NEAR(find(shapeProperties(r), "pitch")->value, 30, 1e-9);

    const auto gable = withProperty(r, "style", 1);
    ASSERT_TRUE(gable);
    const Roof& g = std::get<Roof>(*gable);
    EXPECT_EQ(g.gables, (std::vector<bool>{false, true, false, true}));
    EXPECT_DOUBLE_EQ(find(shapeProperties(g), "style")->value, 1);

    // Gables that match no style: "Personalizzato", one more choice.
    Roof custom = r;
    custom.gables = {true, false, false, false};
    const auto style = find(shapeProperties(custom), "style");
    EXPECT_DOUBLE_EQ(style->value, 4);
    EXPECT_EQ(style->choices.size(), 5);

    const auto steeper = withProperty(r, "pitch", 45);
    ASSERT_TRUE(steeper);
    EXPECT_NEAR(std::get<Roof>(*steeper).pitch, std::acos(-1.0) / 4, 1e-12);
}

TEST_F(Properties, StairHeightSetsRisers)
{
    Stair s;
    const auto taller = withProperty(s, "height", 3240);
    ASSERT_TRUE(taller);
    EXPECT_EQ(std::get<Stair>(*taller).risers, 18);
    EXPECT_EQ(stairRisers(3000), 17);
    const auto u = withProperty(s, "kind", 2);
    ASSERT_TRUE(u);
    EXPECT_EQ(std::get<Stair>(*u).kind, Stair::Kind::U);
}
