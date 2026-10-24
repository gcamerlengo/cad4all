// SPDX-License-Identifier: GPL-3.0-or-later
#include "document.h"

#include <algorithm>

namespace cad {

Document::Document()
{
    reset();
}

void Document::reset()
{
    layers_.clear();
    layers_.push_back(Layer::named("0"));
    currentLayer_ = "0";
    entities_.clear();
    nextId_ = 1;
    clearHistory();
    modified_ = false;
    touch();
}

const Layer* Document::layer(const std::string& name) const
{
    auto it = std::find_if(layers_.begin(), layers_.end(), [&](const Layer& l) { return l.name == name; });
    return it == layers_.end() ? nullptr : &*it;
}

bool Document::addLayer(const Layer& layer)
{
    if (layer.name.empty() || this->layer(layer.name))
        return false;
    layers_.push_back(layer);
    modified_ = true;
    touch();
    return true;
}

bool Document::updateLayer(const std::string& name, const Layer& value)
{
    auto it = std::find_if(layers_.begin(), layers_.end(), [&](const Layer& l) { return l.name == name; });
    if (it == layers_.end())
        return false;
    // Renaming would also have to move every entity on the layer; not yet supported.
    Layer updated = value;
    updated.name = name;
    *it = updated;
    modified_ = true;
    touch();
    return true;
}

bool Document::setCurrentLayer(const std::string& name)
{
    if (!layer(name))
        return false;
    currentLayer_ = name;
    touch();
    return true;
}

bool Document::isVisible(const Entity& e) const
{
    const Layer* l = layer(e.layer);
    return !l || l->visible;
}

bool Document::isEditable(const Entity& e) const
{
    const Layer* l = layer(e.layer);
    return !l || (l->visible && !l->locked);
}

const Entity* Document::entity(EntityId id) const
{
    auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : &it->second;
}

EntityId Document::add(Shape shape)
{
    Entity e;
    e.layer = currentLayer_;
    e.shape = std::move(shape);
    return add(std::move(e));
}

EntityId Document::add(Entity entity)
{
    if (entity.id == 0 || entities_.count(entity.id))
        entity.id = nextId_;
    nextId_ = std::max(nextId_, entity.id + 1);
    if (!layer(entity.layer))
        addLayer(Layer::named(entity.layer));
    const EntityId id = entity.id;
    record({id, std::nullopt, std::move(entity)});
    return id;
}

bool Document::remove(EntityId id)
{
    const Entity* e = entity(id);
    if (!e)
        return false;
    record({id, *e, std::nullopt});
    return true;
}

bool Document::replace(EntityId id, Shape shape)
{
    const Entity* e = entity(id);
    if (!e)
        return false;
    Entity updated = *e;
    updated.shape = std::move(shape);
    record({id, *e, std::move(updated)});
    return true;
}

BBox Document::extents() const
{
    BBox box;
    for (const auto& [id, e] : entities_) {
        if (isVisible(e))
            box.add(boundingBox(e.shape));
    }
    return box;
}

void Document::beginTransaction(const std::string& label)
{
    if (depth_++ == 0)
        open_ = Transaction{label, {}};
}

void Document::commitTransaction()
{
    if (depth_ == 0 || --depth_ > 0)
        return;
    if (open_ && !open_->changes.empty()) {
        undo_.push_back(std::move(*open_));
        redo_.clear();
    }
    open_.reset();
}

std::optional<std::string> Document::undo()
{
    if (undo_.empty() || depth_ > 0)
        return std::nullopt;
    Transaction t = std::move(undo_.back());
    undo_.pop_back();
    for (auto it = t.changes.rbegin(); it != t.changes.rend(); ++it)
        applyState(it->id, it->before);
    std::string label = t.label;
    redo_.push_back(std::move(t));
    modified_ = true;
    touch();
    return label;
}

std::optional<std::string> Document::redo()
{
    if (redo_.empty() || depth_ > 0)
        return std::nullopt;
    Transaction t = std::move(redo_.back());
    redo_.pop_back();
    for (const Change& c : t.changes)
        applyState(c.id, c.after);
    std::string label = t.label;
    undo_.push_back(std::move(t));
    modified_ = true;
    touch();
    return label;
}

void Document::clearHistory()
{
    undo_.clear();
    redo_.clear();
    open_.reset();
    depth_ = 0;
}

void Document::record(Change change)
{
    applyState(change.id, change.after);
    if (open_) {
        open_->changes.push_back(std::move(change));
    } else {
        undo_.push_back(Transaction{"", {std::move(change)}});
        redo_.clear();
    }
    modified_ = true;
    touch();
}

void Document::applyState(EntityId id, const std::optional<Entity>& state)
{
    if (state)
        entities_[id] = *state;
    else
        entities_.erase(id);
}

void Document::touch()
{
    ++revision_;
}

} // namespace cad
