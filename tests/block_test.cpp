// SPDX-License-Identifier: GPL-3.0-or-later
#include "commandengine.h"
#include "document.h"
#include "editing.h"
#include "nativeformat.h"
#include "properties.h"

#include <gtest/gtest.h>

using namespace cad;

namespace {

void expectNear(Vec2 a, Vec2 b, double eps = 1e-9)
{
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
}

std::shared_ptr<const Block> square(const std::string& name = "Q")
{
    BlockItem item;
    item.shape = Polyline({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true);
    return makeBlock(name, {5, 5}, {item});
}

template <class T>
std::vector<T> shapes(const Document& doc)
{
    std::vector<T> out;
    for (const auto& [id, e] : doc.entities())
        if (const auto* s = std::get_if<T>(&e.shape))
            out.push_back(*s);
    return out;
}

} // namespace

TEST(Block, PlacementRoundTrip)
{
    const auto block = square();
    Insert ref{block, {100, 50}, 2.0, -3.0, 0.7};
    const Insert back = placedInsert(block, insertTransform(ref));
    expectNear(back.position, ref.position);
    EXPECT_NEAR(back.scaleX, 2.0, 1e-12);
    EXPECT_NEAR(back.scaleY, -3.0, 1e-12);
    EXPECT_NEAR(back.rotation, 0.7, 1e-12);
    // The base point lands on the insertion point.
    expectNear(insertTransform(ref).apply(block->base), {100, 50});
}

TEST(Block, ReferenceGeometryAndQueries)
{
    Insert ref{square(), {0, 0}, 1.0, 1.0, kPi / 2};
    const BBox box = boundingBox(ref);
    expectNear(box.min, {-5, -5});
    expectNear(box.max, {5, 5});
    EXPECT_NEAR(distanceTo(ref, {5, 0}), 0.0, 1e-9);
    EXPECT_NEAR(distanceTo(ref, {0, 0}), 5.0, 1e-9);
    EXPECT_TRUE(crossesBox(ref, BBox::fromCorners({4, -1}, {6, 1})));
    EXPECT_FALSE(crossesBox(ref, BBox::fromCorners({-1, -1}, {1, 1})));

    std::vector<SnapPoint> snaps;
    collectSnapPoints(ref, SnapInsertion | SnapEndpoint, snaps);
    ASSERT_EQ(snaps.size(), 5u);
    EXPECT_EQ(snaps[0].type, SnapInsertion);

    // Moving and mirroring a reference moves and mirrors what it draws.
    const Shape moved = transformed(ref, Transform::translation({10, 0}));
    expectNear(std::get<Insert>(moved).position, {10, 0});
    const Shape mirrored = transformed(ref, Transform::mirror({0, 0}, {0, 1}));
    const auto parts = insertGeometry(std::get<Insert>(mirrored));
    ASSERT_EQ(parts.size(), 1u);
    const BBox mbox = boundingBox(parts[0]);
    expectNear(mbox.min, {-5, -5});
    expectNear(mbox.max, {5, 5});

    ASSERT_EQ(gripPoints(ref).size(), 1u);
    expectNear(std::get<Insert>(*moveGrip(ref, 0, {3, 4})).position, {3, 4});
    EXPECT_EQ(explodeShape(ref).size(), 1u);
}

TEST(Block, PartsTakeTheLayerAndStyleOfTheReference)
{
    Document doc;
    doc.addLayer({"Arredi", Color{255, 0, 0}, true, false, kContinuous, kLineweightDefault});
    doc.addLayer({"Porte", Color{0, 0, 255}, true, false, "DASHED", 35});
    BlockItem onZero;
    onZero.shape = Line{{0, 0}, {1, 0}};
    BlockItem own;
    own.shape = Line{{0, 1}, {1, 1}};
    own.layer = "Arredi";
    BlockItem byBlock;
    byBlock.shape = Line{{0, 2}, {1, 2}};
    byBlock.layer = "Arredi";
    byBlock.colorByBlock = true;
    byBlock.linetype = kByBlock;
    byBlock.lineweight = kLineweightByBlock;
    doc.defineBlock(makeBlock("B", {0, 0}, {onZero, own, byBlock}));

    Entity e;
    e.layer = "Porte";
    e.shape = Insert{doc.block("B"), {0, 0}};
    std::vector<Entity> parts;
    doc.forEachPart(e, [&](const Entity& p) { parts.push_back(p); });
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0].layer, "Porte");
    EXPECT_EQ(doc.colorOf(parts[0]), (Color{0, 0, 255}));
    EXPECT_EQ(doc.colorOf(parts[1]), (Color{255, 0, 0}));
    EXPECT_EQ(doc.linetypeOf(parts[1]).name, kContinuous);
    // BYBLOCK: what the reference shows, here from its layer.
    EXPECT_EQ(doc.colorOf(parts[2]), (Color{0, 0, 255}));
    EXPECT_EQ(doc.linetypeOf(parts[2]).name, "DASHED");
    EXPECT_EQ(doc.lineweightOf(parts[2]), 35);
}

TEST(Block, RedefinitionUpdatesReferencesAndUndoes)
{
    Document doc;
    ASSERT_TRUE(doc.defineBlock(square("Q")));
    BlockItem nested;
    nested.shape = Insert{doc.block("Q"), {0, 0}};
    ASSERT_TRUE(doc.defineBlock(makeBlock("Gruppo", {0, 0}, {nested})));
    const EntityId direct = doc.add(Insert{doc.block("q"), {0, 0}});
    const EntityId group = doc.add(Insert{doc.block("GRUPPO"), {50, 0}});

    BlockItem circle;
    circle.shape = Circle{{0, 0}, 1};
    doc.beginTransaction("redefine");
    ASSERT_TRUE(doc.defineBlock(makeBlock("Q", {0, 0}, {circle})));
    doc.commitTransaction();

    const auto q = doc.block("Q");
    EXPECT_EQ(std::get<Insert>(doc.entity(direct)->shape).block, q);
    const auto& g = std::get<Insert>(doc.entity(group)->shape);
    EXPECT_EQ(g.block, doc.block("Gruppo"));
    EXPECT_EQ(std::get<Insert>(g.block->items[0].shape).block, q);
    EXPECT_TRUE(std::holds_alternative<Circle>(insertGeometry(g)[0]));

    doc.undo();
    EXPECT_TRUE(std::holds_alternative<Polyline>(insertGeometry(std::get<Insert>(doc.entity(group)->shape))[0]));
    EXPECT_TRUE(std::holds_alternative<Polyline>(doc.block("Q")->items[0].shape));

    // A block cannot contain itself, not even through another block.
    BlockItem loop;
    loop.shape = Insert{doc.block("Gruppo"), {0, 0}};
    EXPECT_FALSE(doc.defineBlock(makeBlock("Q", {0, 0}, {loop})));
}

TEST(Block, NativeFormatRoundTrip)
{
    Document doc;
    doc.addLayer(Layer::named("Arredi"));
    BlockItem item;
    item.shape = Circle{{0, 0}, 3};
    item.layer = "Arredi";
    item.colorByBlock = true;
    item.lineweight = 50;
    doc.defineBlock(makeBlock("Sedia", {1, 2}, {item}));
    BlockItem nested;
    nested.shape = Insert{doc.block("Sedia"), {10, 0}, 1.0, 1.0, 0.5};
    doc.defineBlock(makeBlock("Tavolo", {0, 0}, {nested}));
    doc.add(Insert{doc.block("Tavolo"), {7, 8}, 2.0, -2.0, 1.0});

    Document back;
    ASSERT_TRUE(io::fromNativeJson(io::toNativeJson(doc), back));
    ASSERT_EQ(back.blocks().size(), 2u);
    const BlockItem& c = back.block("Sedia")->items[0];
    EXPECT_EQ(c.layer, "Arredi");
    EXPECT_TRUE(c.colorByBlock);
    EXPECT_EQ(c.lineweight, 50);
    EXPECT_EQ(back.block("Sedia")->base, (Vec2{1, 2}));
    const auto refs = shapes<Insert>(back);
    ASSERT_EQ(refs.size(), 1u);
    EXPECT_EQ(refs[0].block, back.block("Tavolo"));
    EXPECT_DOUBLE_EQ(refs[0].scaleY, -2.0);
    const auto& inner = std::get<Insert>(refs[0].block->items[0].shape);
    EXPECT_EQ(inner.block, back.block("Sedia"));
    EXPECT_DOUBLE_EQ(inner.rotation, 0.5);
}

TEST(Block, BlockAndInsertCommands)
{
    Document doc;
    CommandEngine engine{doc};
    auto type = [&](std::initializer_list<const char*> inputs) {
        for (const char* in : inputs)
            engine.submit(QString::fromUtf8(in));
    };
    type({"RETTANGOLO", "0,0", "10,10", "CERCHIO", "5,5", "2"});
    ASSERT_EQ(doc.entities().size(), 2u);

    std::vector<EntityId> all;
    for (const auto& [id, e] : doc.entities())
        all.push_back(id);
    engine.select(all);
    type({"BLOCCO", "Sedia", "5,5"});
    EXPECT_FALSE(engine.isActive());
    ASSERT_NE(doc.block("Sedia"), nullptr);
    EXPECT_EQ(doc.block("Sedia")->items.size(), 2u);
    // The objects became one reference to the block.
    ASSERT_EQ(shapes<Insert>(doc).size(), 1u);
    EXPECT_EQ(doc.entities().size(), 1u);

    type({"INSERISCI", "", "S", "2", "R", "90", "100,0"});
    EXPECT_FALSE(engine.isActive());
    const auto refs = shapes<Insert>(doc);
    ASSERT_EQ(refs.size(), 2u);
    EXPECT_DOUBLE_EQ(refs[1].scaleX, 2.0);
    expectNear(boundingBox(refs[1]).min, {90, -10});

    // Redefining asks first; No keeps the block.
    type({"B", "Sedia", "N"});
    EXPECT_FALSE(engine.isActive());
    EXPECT_EQ(doc.block("Sedia")->items.size(), 2u);

    engine.select({doc.entities().rbegin()->first});
    type({"ESPLODI"});
    EXPECT_EQ(shapes<Polyline>(doc).size(), 1u);
    EXPECT_EQ(shapes<Circle>(doc).size(), 1u);
    EXPECT_NEAR(shapes<Circle>(doc)[0].radius, 4.0, 1e-9);

    engine.select({doc.entities().begin()->first});
    type({"LISTA"});
    EXPECT_FALSE(engine.isActive());

    // Undo brings back the reference, then the objects before BLOCCO.
    type({"U", "U", "U"});
    EXPECT_EQ(doc.entities().size(), 2u);
    EXPECT_EQ(doc.block("Sedia"), nullptr);
}

TEST(Block, InfoBoxProperties)
{
    const Insert ref{square(), {0, 0}, 2.0, 2.0, 0.0};
    const auto props = shapeProperties(ref);
    ASSERT_EQ(props.size(), 3u);
    EXPECT_EQ(props[0].key, "scaleX");
    EXPECT_DOUBLE_EQ(props[0].value, 2.0);
    const auto turned = withProperty(ref, "rotation", 90.0);
    ASSERT_TRUE(turned);
    EXPECT_NEAR(std::get<Insert>(*turned).rotation, kPi / 2, 1e-12);
    EXPECT_FALSE(withProperty(ref, "scaleY", 0.0));
    EXPECT_EQ(shapeTitle(ref), QStringLiteral("Blocco Q"));
}
