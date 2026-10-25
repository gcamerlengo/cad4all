// SPDX-License-Identifier: GPL-3.0-or-later
// The ArchiCAD-style panels, driven as a user would, without a display.
#include "commandengine.h"
#include "infobox.h"
#include "navigator.h"
#include "properties.h"
#include "toolbox.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QToolButton>

#include <gtest/gtest.h>

using namespace cad;

namespace {

class Ui : public ::testing::Test {
protected:
    void SetUp() override { toolSettings() = ToolSettings{}; }
    void TearDown() override { toolSettings() = saved_; }

    void type(std::initializer_list<const char*> inputs)
    {
        for (const char* in : inputs)
            engine.submit(QString::fromUtf8(in));
    }

    // Types a value in a field and leaves it, as Enter does.
    static void edit(QDoubleSpinBox* spin, double value)
    {
        spin->setValue(value);
        emit spin->editingFinished();
    }

    Document doc;
    CommandEngine engine{doc};

private:
    ToolSettings saved_ = toolSettings();
};

} // namespace

TEST_F(Ui, InfoBoxEditsToolSettings)
{
    InfoBox box(engine);
    type({"TETTO"});
    auto* pitch = box.findChild<QDoubleSpinBox*>(QStringLiteral("pitch"));
    ASSERT_NE(pitch, nullptr);
    EXPECT_DOUBLE_EQ(pitch->value(), 30.0);
    edit(pitch, 40.0);
    EXPECT_DOUBLE_EQ(toolSettings().roofPitch, 40.0);

    auto* style = box.findChild<QComboBox*>(QStringLiteral("style"));
    ASSERT_NE(style, nullptr);
    emit style->activated(1);
    EXPECT_EQ(toolSettings().roofStyle, RoofStyle::Gable);

    // Options typed on the command line show up in the box.
    type({"P", "35"});
    EXPECT_DOUBLE_EQ(pitch->value(), 35.0);
}

TEST_F(Ui, InfoBoxEditsSelectedWallInOneUndoStep)
{
    type({"MURO", "0,0", "5000,0", ""});
    ASSERT_EQ(doc.entities().size(), 1u);
    const EntityId id = doc.entities().begin()->first;
    InfoBox box(engine);
    engine.select({id});
    QApplication::processEvents();

    auto* height = box.findChild<QDoubleSpinBox*>(QStringLiteral("height"));
    ASSERT_NE(height, nullptr);
    EXPECT_DOUBLE_EQ(height->value(), 2700.0);
    edit(height, 3100.0);
    EXPECT_DOUBLE_EQ(std::get<Wall>(doc.entity(id)->shape).height, 3100.0);
    // The tool keeps its own value.
    EXPECT_DOUBLE_EQ(toolSettings().wallHeight, 2700.0);

    // Leaving the field again changes nothing and adds no undo step.
    emit height->editingFinished();
    type({"U"});
    EXPECT_DOUBLE_EQ(std::get<Wall>(doc.entity(id)->shape).height, 2700.0);
}

TEST_F(Ui, ToolboxRunsAndShowsCommand)
{
    Toolbox toolbox(engine);
    toolbox.addTool(QStringLiteral("Freccia"), QString(), QIcon());
    toolbox.addTool(QStringLiteral("Muro"), QStringLiteral("WALL"), QIcon());
    const auto buttons = toolbox.findChildren<QToolButton*>();
    ASSERT_EQ(buttons.size(), 2);
    EXPECT_TRUE(buttons[0]->isChecked());

    buttons[1]->click();
    EXPECT_EQ(engine.activeCommandName(), QStringLiteral("WALL"));
    EXPECT_TRUE(buttons[1]->isChecked());
    EXPECT_FALSE(buttons[0]->isChecked());

    buttons[0]->click();
    EXPECT_FALSE(engine.isActive());
    EXPECT_TRUE(buttons[0]->isChecked());

    // A command typed on the command line presses its button too.
    type({"MURO"});
    EXPECT_TRUE(buttons[1]->isChecked());
}

TEST_F(Ui, NavigatorListsStoriesFromTheTop)
{
    Navigator nav(doc);
    StoryId chosen = -1;
    QObject::connect(&nav, &Navigator::storyChosen, [&](StoryId id) { chosen = id; });
    type({"PIANO", "N", "Primo", ""});
    nav.refresh();
    QTreeWidgetItem* stories = nav.topLevelItem(0);
    ASSERT_EQ(stories->childCount(), static_cast<int>(doc.stories().size()));
    ASSERT_GE(stories->childCount(), 2);
    EXPECT_TRUE(stories->child(0)->text(0).startsWith(QStringLiteral("Primo")));
    emit nav.itemActivated(stories->child(1), 0);
    EXPECT_EQ(chosen, doc.stories().front().id);
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
