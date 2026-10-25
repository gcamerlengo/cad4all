// SPDX-License-Identifier: GPL-3.0-or-later
// Blocks: named groups of objects drawn wherever a reference (Insert) puts
// them, like AutoCAD's BLOCK and INSERT.
#pragma once

#include "shapes.h"
#include "style.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cad {

// Names an object uses to take the linetype of the block reference.
inline const std::string kByBlock = "BYBLOCK";

// An object of a block, with its own layer and style. Objects on layer "0"
// take the layer of the reference; BYBLOCK colour, linetype and lineweight
// take those of the reference.
struct BlockItem {
    Shape shape;
    std::string layer = "0";
    // Unset: BYLAYER, or BYBLOCK when colorByBlock is set.
    std::optional<Color> color;
    bool colorByBlock = false;
    std::string linetype = kByLayer;
    int lineweight = kLineweightByLayer;
};

// A block definition. It never changes once made: redefining a block makes
// a new one, so a reference always draws what it was given.
struct Block {
    std::string name;
    Vec2 base;
    std::vector<BlockItem> items;
    // Of the items, in block coordinates.
    BBox extents;
};

std::shared_ptr<const Block> makeBlock(std::string name, Vec2 base, std::vector<BlockItem> items);

// From block coordinates to the drawing.
Transform insertTransform(const Insert& ref);
// The reference that places `block` through `t`, a combination of moves,
// rotations, mirrors and scales (with different scales along X and Y).
Insert placedInsert(std::shared_ptr<const Block> block, const Transform& t);
// What a reference draws, placed in the drawing; nested references are expanded.
std::vector<Shape> insertGeometry(const Insert& ref);
// True if the shape is, or contains at any depth, a reference to `block`.
bool referencesBlock(const Shape& shape, const Block* block);

} // namespace cad
