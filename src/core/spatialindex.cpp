// SPDX-License-Identifier: GPL-3.0-or-later
#include "spatialindex.h"

#include <algorithm>
#include <cmath>

namespace cad {

namespace {

// Items covering more cells than this go to the list checked every time.
constexpr long kMaxCellsPerItem = 64;
constexpr int kMaxSide = 1024;

} // namespace

SpatialIndex::Range SpatialIndex::cells(const BBox& box) const
{
    const auto clampX = [&](double x) { return std::clamp(static_cast<int>(std::floor((x - extent_.min.x) / cell_)), 0, columns_ - 1); };
    const auto clampY = [&](double y) { return std::clamp(static_cast<int>(std::floor((y - extent_.min.y) / cell_)), 0, rows_ - 1); };
    return {clampX(box.min.x), clampY(box.min.y), clampX(box.max.x), clampY(box.max.y)};
}

void SpatialIndex::build(std::vector<std::pair<EntityId, BBox>> items)
{
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    items_ = std::move(items);
    grid_.clear();
    large_.clear();
    seen_.assign(items_.size(), 0);
    stamp_ = 0;
    extent_ = BBox{};
    for (const auto& [id, box] : items_)
        if (box.valid())
            extent_.add(box);
    if (!extent_.valid()) {
        columns_ = rows_ = 0;
        for (std::uint32_t i = 0; i < items_.size(); ++i)
            large_.push_back(i);
        return;
    }
    // About one item per cell, square cells, at most kMaxSide a side.
    const double w = std::max(extent_.width(), 1e-9);
    const double h = std::max(extent_.height(), 1e-9);
    cell_ = std::max({std::sqrt(w * h / std::max<double>(1.0, double(items_.size()))), w / kMaxSide, h / kMaxSide, 1e-9});
    columns_ = std::clamp(static_cast<int>(std::ceil(w / cell_)), 1, kMaxSide);
    rows_ = std::clamp(static_cast<int>(std::ceil(h / cell_)), 1, kMaxSide);
    grid_.resize(size_t(columns_) * size_t(rows_));
    for (std::uint32_t i = 0; i < items_.size(); ++i) {
        const BBox& box = items_[i].second;
        if (!box.valid()) {
            large_.push_back(i);
            continue;
        }
        const Range r = cells(box);
        if (long(r.x1 - r.x0 + 1) * long(r.y1 - r.y0 + 1) > kMaxCellsPerItem) {
            large_.push_back(i);
            continue;
        }
        for (int y = r.y0; y <= r.y1; ++y)
            for (int x = r.x0; x <= r.x1; ++x)
                grid_[size_t(y) * size_t(columns_) + size_t(x)].push_back(i);
    }
}

std::vector<EntityId> SpatialIndex::query(const BBox& box) const
{
    std::vector<std::uint32_t> found;
    if (++stamp_ == 0) {
        std::fill(seen_.begin(), seen_.end(), 0);
        stamp_ = 1;
    }
    const auto consider = [&](std::uint32_t i) {
        if (seen_[i] == stamp_)
            return;
        seen_[i] = stamp_;
        const BBox& b = items_[i].second;
        // Items without a box (empty text...) are always given back: the caller decides.
        if (!b.valid() || b.intersects(box))
            found.push_back(i);
    };
    for (std::uint32_t i : large_)
        consider(i);
    if (columns_ > 0 && box.valid() && box.intersects(extent_)) {
        const Range r = cells(box);
        for (int y = r.y0; y <= r.y1; ++y)
            for (int x = r.x0; x <= r.x1; ++x)
                for (std::uint32_t i : grid_[size_t(y) * size_t(columns_) + size_t(x)])
                    consider(i);
    }
    std::sort(found.begin(), found.end());
    std::vector<EntityId> out;
    out.reserve(found.size());
    for (std::uint32_t i : found)
        out.push_back(items_[i].first);
    return out;
}

} // namespace cad
