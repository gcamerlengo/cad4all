// SPDX-License-Identifier: GPL-3.0-or-later
#include "block.h"

#include <cmath>

namespace cad {

std::shared_ptr<const Block> makeBlock(std::string name, Vec2 base, std::vector<BlockItem> items)
{
    auto block = std::make_shared<Block>();
    block->name = std::move(name);
    block->base = base;
    block->items = std::move(items);
    for (const BlockItem& item : block->items)
        block->extents.add(boundingBox(item.shape));
    return block;
}

Transform insertTransform(const Insert& ref)
{
    Transform scale;
    scale.a = ref.scaleX;
    scale.d = ref.scaleY;
    const Vec2 base = ref.block ? ref.block->base : Vec2{};
    return Transform::translation(ref.position) * Transform::rotation({0.0, 0.0}, ref.rotation) * scale
         * Transform::translation(base * -1.0);
}

Insert placedInsert(std::shared_ptr<const Block> block, const Transform& t)
{
    Insert ref;
    const Vec2 base = block ? block->base : Vec2{};
    ref.position = t.apply(base);
    // The X axis of the block gives the rotation and the X scale; what is
    // left of the Y axis once rotated back is the Y scale (negative if mirrored).
    const Vec2 x = t.applyVector({1.0, 0.0});
    const Vec2 y = t.applyVector({0.0, 1.0});
    ref.scaleX = length(x);
    ref.rotation = ref.scaleX > 0.0 ? normalizeAngle(angleOf(x)) : 0.0;
    ref.scaleY = -std::sin(ref.rotation) * y.x + std::cos(ref.rotation) * y.y;
    ref.block = std::move(block);
    return ref;
}

namespace {

void appendGeometry(const Insert& ref, const Transform& outer, std::vector<Shape>& out)
{
    if (!ref.block)
        return;
    const Transform t = outer * insertTransform(ref);
    for (const BlockItem& item : ref.block->items) {
        if (const auto* nested = std::get_if<Insert>(&item.shape))
            appendGeometry(*nested, t, out);
        else
            out.push_back(transformed(item.shape, t));
    }
}

} // namespace

std::vector<Shape> insertGeometry(const Insert& ref)
{
    std::vector<Shape> out;
    appendGeometry(ref, Transform{}, out);
    return out;
}

bool referencesBlock(const Shape& shape, const Block* block)
{
    const auto* ref = std::get_if<Insert>(&shape);
    if (!ref || !ref->block)
        return false;
    if (ref->block.get() == block)
        return true;
    for (const BlockItem& item : ref->block->items)
        if (referencesBlock(item.shape, block))
            return true;
    return false;
}

} // namespace cad
