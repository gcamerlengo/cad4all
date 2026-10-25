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
    stories_.clear();
    nextStory_ = 1;
    currentStory_ = addStory({0, "Piano terra", 0.0, 3000.0});
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

bool Document::isLayerVisible(const Entity& e) const
{
    const Layer* l = layer(e.layer);
    return !l || l->visible;
}

bool Document::isVisible(const Entity& e) const
{
    return e.story == currentStory_ && isLayerVisible(e);
}

bool Document::isEditable(const Entity& e) const
{
    const Layer* l = layer(e.layer);
    return isVisible(e) && (!l || !l->locked);
}

const Story* Document::story(StoryId id) const
{
    auto it = std::find_if(stories_.begin(), stories_.end(), [&](const Story& s) { return s.id == id; });
    return it == stories_.end() ? nullptr : &*it;
}

const Story* Document::storyNamed(const std::string& name) const
{
    auto it = std::find_if(stories_.begin(), stories_.end(), [&](const Story& s) { return s.name == name; });
    return it == stories_.end() ? nullptr : &*it;
}

namespace {

void sortStories(std::vector<Story>& stories)
{
    std::stable_sort(stories.begin(), stories.end(), [](const Story& a, const Story& b) { return a.elevation < b.elevation; });
}

} // namespace

StoryId Document::addStory(Story story)
{
    if (story.name.empty() || storyNamed(story.name))
        return 0;
    // Files keep their ids, so entities find their story again.
    if (story.id <= 0 || this->story(story.id))
        story.id = nextStory_;
    nextStory_ = std::max(nextStory_, story.id + 1);
    stories_.push_back(story);
    sortStories(stories_);
    modified_ = true;
    touch();
    return story.id;
}

bool Document::updateStory(StoryId id, const Story& value)
{
    auto it = std::find_if(stories_.begin(), stories_.end(), [&](const Story& s) { return s.id == id; });
    if (it == stories_.end() || value.name.empty())
        return false;
    const Story* sameName = storyNamed(value.name);
    if (sameName && sameName->id != id)
        return false;
    Story updated = value;
    updated.id = id;
    *it = updated;
    sortStories(stories_);
    modified_ = true;
    touch();
    return true;
}

bool Document::removeStory(StoryId id)
{
    if (stories_.size() <= 1 || !story(id))
        return false;
    if (std::any_of(entities_.begin(), entities_.end(), [&](const auto& kv) { return kv.second.story == id; }))
        return false;
    std::erase_if(stories_, [&](const Story& s) { return s.id == id; });
    if (currentStory_ == id)
        currentStory_ = stories_.front().id;
    modified_ = true;
    touch();
    return true;
}

bool Document::setCurrentStory(StoryId id)
{
    if (!story(id))
        return false;
    currentStory_ = id;
    touch();
    return true;
}

const Story* Document::storyBelow(StoryId id) const
{
    for (size_t i = 1; i < stories_.size(); ++i)
        if (stories_[i].id == id)
            return &stories_[i - 1];
    return nullptr;
}

const Story* Document::storyAbove(StoryId id) const
{
    for (size_t i = 0; i + 1 < stories_.size(); ++i)
        if (stories_[i].id == id)
            return &stories_[i + 1];
    return nullptr;
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
    if (!story(entity.story))
        entity.story = currentStory_;
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
