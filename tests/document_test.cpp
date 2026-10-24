// SPDX-License-Identifier: GPL-3.0-or-later
#include "document.h"

#include <gtest/gtest.h>

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
