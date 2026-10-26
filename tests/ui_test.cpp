// SPDX-License-Identifier: GPL-3.0-or-later
// The panels of the parametric interface, driven as a user would, without a display.
#include "commandengine.h"
#include "infobox.h"
#include "layerpanel.h"
#include "navigator.h"
#include "properties.h"
#include "stylecontrols.h"
#include "theme.h"
#include "toolbox.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QToolButton>
#include <QTreeWidget>

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

TEST_F(Ui, StyleControlsChangeSelectionOrCurrentStyle)
{
    QWidget parent;
    StyleControls controls(engine, &parent);
    type({"L", "0,0", "10,0", ""});
    const EntityId id = doc.entities().begin()->first;

    // Nothing selected: the choice is for new objects.
    QComboBox* weight = controls.lineweightCombo();
    const int half = weight->findData(50);
    ASSERT_GE(half, 0);
    weight->setCurrentIndex(half);
    emit weight->activated(half);
    EXPECT_EQ(doc.currentStyle().lineweight, 50);
    EXPECT_EQ(doc.entity(id)->lineweight, kLineweightByLayer);

    // With a selection it changes the objects and shows their value.
    engine.select({id});
    EXPECT_EQ(weight->currentData().toInt(), kLineweightByLayer);
    QComboBox* linetype = controls.linetypeCombo();
    const int hidden = linetype->findData(QStringLiteral("HIDDEN"));
    ASSERT_GE(hidden, 0);
    linetype->setCurrentIndex(hidden);
    emit linetype->activated(hidden);
    QComboBox* color = controls.colorCombo();
    const int red = color->findText(QStringLiteral("Rosso"));
    ASSERT_GE(red, 0);
    color->setCurrentIndex(red);
    emit color->activated(red);
    EXPECT_EQ(doc.entity(id)->linetype, "HIDDEN");
    EXPECT_EQ(doc.entity(id)->color, std::optional<Color>(aciToColor(1)));
    EXPECT_EQ(color->currentText(), QStringLiteral("Rosso"));
}

TEST_F(Ui, LayerPanelSwitchesVisibilityAndLockWithAClick)
{
    LayerPanel panel(engine);
    auto* tree = panel.findChild<QTreeWidget*>();
    ASSERT_NE(tree, nullptr);
    QTreeWidgetItem* item = tree->topLevelItem(0);
    ASSERT_NE(item, nullptr);
    const std::string name = item->text(LayerPanel::NameColumn).toStdString();
    EXPECT_EQ(item->text(LayerPanel::ColorColumn), QStringLiteral("Bianco"));

    emit tree->itemClicked(item, LayerPanel::VisibleColumn);
    EXPECT_FALSE(doc.layer(name)->visible);
    item = tree->topLevelItem(0);
    emit tree->itemClicked(item, LayerPanel::LockedColumn);
    EXPECT_TRUE(doc.layer(name)->locked);
    // A click on the name only selects it.
    item = tree->topLevelItem(0);
    emit tree->itemClicked(item, LayerPanel::NameColumn);
    EXPECT_FALSE(doc.layer(name)->visible);
    EXPECT_TRUE(doc.layer(name)->locked);
}

TEST(Theme, NamesAndPalette)
{
    theme::Theme t = theme::Theme::Light;
    EXPECT_TRUE(theme::fromName(QStringLiteral("Scuro"), &t));
    EXPECT_EQ(t, theme::Theme::Dark);
    EXPECT_TRUE(theme::fromName(QStringLiteral("chiaro"), &t));
    EXPECT_EQ(t, theme::Theme::Light);
    EXPECT_FALSE(theme::fromName(QStringLiteral("viola"), &t));
    EXPECT_EQ(theme::name(theme::Theme::Dark), QStringLiteral("scuro"));

    theme::apply(theme::Theme::Dark);
    EXPECT_LT(QApplication::palette().color(QPalette::Window).lightness(), 100);
    EXPECT_GT(QApplication::palette().color(QPalette::WindowText).lightness(), 180);
    theme::apply(theme::Theme::Light);
    EXPECT_GT(QApplication::palette().color(QPalette::Window).lightness(), 200);
    EXPECT_EQ(theme::current(), theme::Theme::Light);
    EXPECT_FALSE(qApp->styleSheet().contains(QLatin1Char('@')));
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
