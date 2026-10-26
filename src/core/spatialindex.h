// SPDX-License-Identifier: GPL-3.0-or-later
// Finds the entities in an area without looking at all of them: a uniform
// grid over the drawing, each cell listing the entities whose box meets it.
// Entities larger than many cells are kept apart and always checked.
#pragma once

#include "geometry.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace cad {

using EntityId = std::uint64_t;

class SpatialIndex {
public:
    // `items` in any order; ids must be unique.
    void build(std::vector<std::pair<EntityId, BBox>> items);
    // The ids whose box meets `box`, in ascending order (the drawing order).
    std::vector<EntityId> query(const BBox& box) const;
    size_t size() const { return items_.size(); }

private:
    struct Range {
        int x0, y0, x1, y1;
    };
    Range cells(const BBox& box) const;

    std::vector<std::pair<EntityId, BBox>> items_;  // sorted by id
    std::vector<std::vector<std::uint32_t>> grid_;  // per cell: indices into items_
    std::vector<std::uint32_t> large_;
    BBox extent_;
    int columns_ = 0;
    int rows_ = 0;
    double cell_ = 1.0;
    mutable std::vector<std::uint32_t> seen_;  // query stamp per item
    mutable std::uint32_t stamp_ = 0;
};

} // namespace cad
