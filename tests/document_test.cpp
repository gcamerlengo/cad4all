// SPDX-License-Identifier: GPL-3.0-or-later
#include "document.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace cad;

TEST(Document, StartsWithLayerZero)
{
    Document doc;
    ASSERT_EQ(doc.layers().size(), 1u);
    EXPECT_EQ(doc.currentLayer(), "0");
    EXPECT_FALSE(doc.isModified());
}

TEST(Document, AddUsesCurrentLayer)
{
    Document doc;
    ASSERT_TRUE(doc.addLayer({"Muri", {255, 0, 0}}));
    ASSERT_TRUE(doc.setCurrentLayer("Muri"));
    const EntityId id = doc.add(Line{{0, 0}, {1, 0}});
    EXPECT_EQ(doc.entity(id)->layer, "Muri");
    EXPECT_FALSE(doc.addLayer(Layer::named("Muri")));
    EXPECT_FALSE(doc.setCurrentLayer("Inesistente"));
}

TEST(Document, UndoRedoSingleChanges)
{
    Document doc;
    const EntityId a = doc.add(Line{{0, 0}, {1, 0}});
    doc.add(Circle{{0, 0}, 1});
    EXPECT_EQ(doc.entities().size(), 2u);
    doc.undo();
    EXPECT_EQ(doc.entities().size(), 1u);
    doc.undo();
    EXPECT_TRUE(doc.entities().empty());
    EXPECT_FALSE(doc.undo());
    doc.redo();
    ASSERT_NE(doc.entity(a), nullptr);
    doc.redo();
    EXPECT_EQ(doc.entities().size(), 2u);
}

TEST(Document, TransactionUndoesAsOneStep)
{
    Document doc;
    const EntityId a = doc.add(Line{{0, 0}, {1, 0}});
    doc.beginTransaction("MOVE");
    doc.replace(a, Line{{5, 5}, {6, 5}});
    doc.add(Line{{0, 1}, {1, 1}});
    doc.remove(a);
    doc.commitTransaction();
    EXPECT_EQ(doc.entities().size(), 1u);

    EXPECT_EQ(doc.undo().value(), "MOVE");
    ASSERT_EQ(doc.entities().size(), 1u);
    const auto& line = std::get<Line>(doc.entity(a)->shape);
    EXPECT_EQ(line.a, (Vec2{0, 0}));
}

TEST(Document, NewChangeClearsRedo)
{
    Document doc;
    doc.add(Line{{0, 0}, {1, 0}});
    doc.undo();
    EXPECT_TRUE(doc.canRedo());
    doc.add(Line{{0, 0}, {2, 0}});
    EXPECT_FALSE(doc.canRedo());
}

TEST(Document, EmptyTransactionLeavesNoUndoStep)
{
    Document doc;
    doc.beginTransaction("ZOOM");
    doc.commitTransaction();
    EXPECT_FALSE(doc.canUndo());
}

TEST(Document, HiddenAndLockedLayers)
{
    Document doc;
    doc.addLayer({"Nascosto", {}, false, false});
    doc.addLayer({"Bloccato", {}, true, true});
    Entity hidden{0, "Nascosto", Line{{0, 0}, {1, 0}}};
    Entity locked{0, "Bloccato", Line{{0, 0}, {1, 0}}};
    const EntityId h = doc.add(hidden);
    const EntityId l = doc.add(locked);
    EXPECT_FALSE(doc.isVisible(*doc.entity(h)));
    EXPECT_TRUE(doc.isVisible(*doc.entity(l)));
    EXPECT_FALSE(doc.isEditable(*doc.entity(l)));
    // Hidden entities do not count for the extents.
    EXPECT_NEAR(doc.extents().width(), 1.0, 1e-12);
}

TEST(Document, StoriesKeepTheirOrderAndOwnTheEntities)
{
    Document doc;
    ASSERT_EQ(doc.stories().size(), 1u);
    const StoryId ground = doc.currentStory();
    EXPECT_EQ(doc.story(ground)->name, "Piano terra");

    const StoryId first = doc.addStory({0, "Primo piano", 3000, 3000});
    const StoryId basement = doc.addStory({0, "Interrato", -2800, 2800});
    ASSERT_NE(first, 0);
    ASSERT_NE(basement, 0);
    EXPECT_EQ(doc.addStory({0, "Primo piano", 6000, 3000}), 0);  // name taken
    EXPECT_EQ(doc.addStory({0, "", 6000, 3000}), 0);
    ASSERT_EQ(doc.stories().size(), 3u);
    EXPECT_EQ(doc.stories().front().id, basement);
    EXPECT_EQ(doc.storyBelow(ground)->id, basement);
    EXPECT_EQ(doc.storyAbove(ground)->id, first);
    EXPECT_EQ(doc.storyAbove(first), nullptr);

    const EntityId a = doc.add(Line{{0, 0}, {1, 0}});
    ASSERT_TRUE(doc.setCurrentStory(first));
    const EntityId b = doc.add(Line{{0, 0}, {0, 1}});
    EXPECT_EQ(doc.entity(a)->story, ground);
    EXPECT_EQ(doc.entity(b)->story, first);
    // The plan shows the current story, the 3D view every story.
    EXPECT_FALSE(doc.isVisible(*doc.entity(a)));
    EXPECT_FALSE(doc.isEditable(*doc.entity(a)));
    EXPECT_TRUE(doc.isLayerVisible(*doc.entity(a)));
    EXPECT_TRUE(doc.isVisible(*doc.entity(b)));

    // Raising a story keeps the order by elevation.
    Story s = *doc.story(basement);
    s.elevation = 9000;
    ASSERT_TRUE(doc.updateStory(basement, s));
    EXPECT_EQ(doc.stories().back().id, basement);

    EXPECT_FALSE(doc.removeStory(first));  // not empty
    EXPECT_TRUE(doc.removeStory(basement));
    EXPECT_FALSE(doc.setCurrentStory(basement));
    doc.remove(b);
    EXPECT_TRUE(doc.removeStory(first));
    EXPECT_EQ(doc.currentStory(), ground);
    EXPECT_FALSE(doc.removeStory(ground));  // the last one
}

TEST(SpatialIndex, FindsTheEntitiesInAnArea)
{
    Document doc;
    std::vector<EntityId> grid;
    for (int i = 0; i < 50; ++i)
        for (int j = 0; j < 50; ++j)
            grid.push_back(doc.add(Circle{{i * 10.0, j * 10.0}, 1.0}));
    const EntityId longLine = doc.add(Line{{-100, -100}, {600, 600}});
    // A box around one circle: that circle and the long diagonal line.
    auto found = doc.entitiesIn(BBox::fromCorners({99, 199}, {101, 201}));
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0], grid[10 * 50 + 20]);
    EXPECT_EQ(found[1], longLine);
    EXPECT_TRUE(doc.entitiesIn(BBox::fromCorners({1000, 1000}, {1100, 1100})).empty());
    // The four circles of a 2 x 2 block and the line, in drawing order.
    found = doc.entitiesIn(BBox::fromCorners({195, 195}, {215, 215}));
    EXPECT_EQ(found.size(), 5u);
    EXPECT_TRUE(std::is_sorted(found.begin(), found.end()));

    // The index follows the changes.
    doc.replace(grid[0], Circle{{1050, 1050}, 1.0});
    found = doc.entitiesIn(BBox::fromCorners({1000, 1000}, {1100, 1100}));
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0], grid[0]);
    doc.undo();
    EXPECT_TRUE(doc.entitiesIn(BBox::fromCorners({1000, 1000}, {1100, 1100})).empty());
}
