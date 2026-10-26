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
    linetypes_ = standardLinetypes();
    linetypeScale_ = 1.0;
    units_ = 4;
    precision_ = 2;
    currentStyle_ = {};
    stories_.clear();
    nextStory_ = 1;
    currentStory_ = addStory({0, "Piano terra", 0.0, 3000.0});
    blocks_.clear();
    layouts_.clear();
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

EntityStyle styleOf(const Entity& e)
{
    return {e.color, e.linetype, e.lineweight};
}

void applyStyle(Entity& e, const EntityStyle& style)
{
    e.color = style.color;
    e.linetype = style.linetype;
    e.lineweight = style.lineweight;
}

const Linetype* Document::linetype(const std::string& name) const
{
    const std::string key = linetypeKey(name);
    auto it = std::find_if(linetypes_.begin(), linetypes_.end(), [&](const Linetype& t) { return t.name == key; });
    return it == linetypes_.end() ? nullptr : &*it;
}

bool Document::addLinetype(Linetype type)
{
    type.name = linetypeKey(type.name);
    if (type.name.empty() || type.name == kByLayer || linetype(type.name))
        return false;
    linetypes_.push_back(std::move(type));
    modified_ = true;
    touch();
    return true;
}

double unitInMillimetres(int units)
{
    switch (units) {
    case 1: return 25.4;
    case 2: return 304.8;
    case 4: return 1.0;
    case 5: return 10.0;
    case 6: return 1000.0;
    default: return 0.0;
    }
}

const char* unitName(int units)
{
    switch (units) {
    case 1: return "Pollici";
    case 2: return "Piedi";
    case 4: return "Millimetri";
    case 5: return "Centimetri";
    case 6: return "Metri";
    default: return "Senza unit\xC3\xA0";
    }
}

void Document::setUnits(int units)
{
    if (units != 0 && unitInMillimetres(units) == 0.0)
        return;
    if (units == units_)
        return;
    units_ = units;
    modified_ = true;
    touch();
}

void Document::setPrecision(int decimals)
{
    decimals = std::clamp(decimals, 0, 8);
    if (decimals == precision_)
        return;
    precision_ = decimals;
    modified_ = true;
    touch();
}

void Document::setLinetypeScale(double scale)
{
    if (scale <= 0.0 || scale == linetypeScale_)
        return;
    linetypeScale_ = scale;
    modified_ = true;
    touch();
}

void Document::setCurrentStyle(const EntityStyle& style)
{
    currentStyle_ = style;
    if (linetypeKey(currentStyle_.linetype) == kByLayer || !linetype(currentStyle_.linetype))
        currentStyle_.linetype = kByLayer;
    else
        currentStyle_.linetype = linetypeKey(currentStyle_.linetype);
    touch();
}

Color Document::colorOf(const Entity& e) const
{
    if (e.color)
        return *e.color;
    const Layer* l = layer(e.layer);
    return l ? l->color : Color{};
}

const Linetype& Document::linetypeOf(const Entity& e) const
{
    std::string name = e.linetype;
    if (linetypeKey(name) == kByLayer) {
        const Layer* l = layer(e.layer);
        name = l ? l->linetype : kContinuous;
    }
    const Linetype* t = linetype(name);
    return t ? *t : standardLinetypes().front();
}

int Document::lineweightOf(const Entity& e) const
{
    if (e.lineweight >= 0 || e.lineweight == kLineweightDefault)
        return e.lineweight;
    const Layer* l = layer(e.layer);
    return l ? l->lineweight : kLineweightDefault;
}

const Layout* Document::layout(const std::string& name) const
{
    auto it = std::find_if(layouts_.begin(), layouts_.end(), [&](const Layout& l) { return l.name == name; });
    return it == layouts_.end() ? nullptr : &*it;
}

bool Document::addLayout(const Layout& value)
{
    if (value.name.empty() || layout(value.name))
        return false;
    layouts_.push_back(value);
    modified_ = true;
    touch();
    return true;
}

bool Document::updateLayout(const std::string& name, const Layout& value)
{
    auto it = std::find_if(layouts_.begin(), layouts_.end(), [&](const Layout& l) { return l.name == name; });
    if (it == layouts_.end() || value.name.empty())
        return false;
    const Layout* same = layout(value.name);
    if (same && same != &*it)
        return false;
    *it = value;
    modified_ = true;
    touch();
    return true;
}

bool Document::removeLayout(const std::string& name)
{
    const auto removed = std::erase_if(layouts_, [&](const Layout& l) { return l.name == name; });
    if (removed == 0)
        return false;
    modified_ = true;
    touch();
    return true;
}

std::shared_ptr<const Block> Document::block(const std::string& name) const
{
    auto it = blocks_.find(linetypeKey(name));
    return it == blocks_.end() ? nullptr : it->second;
}

namespace {

// The shape with references to `from` (at any depth) turned into references
// to `to`, or nothing if it has none. `rebuilt` keeps the blocks already
// made again, so a block used twice is rebuilt once.
std::optional<Shape> rebound(const Shape& shape, const Block* from, const std::shared_ptr<const Block>& to,
                             std::map<const Block*, std::shared_ptr<const Block>>& rebuilt)
{
    const auto* ref = std::get_if<Insert>(&shape);
    if (!ref || !referencesBlock(shape, from))
        return std::nullopt;
    Insert r = *ref;
    if (ref->block.get() == from) {
        r.block = to;
        return r;
    }
    auto& done = rebuilt[ref->block.get()];
    if (!done) {
        std::vector<BlockItem> items = ref->block->items;
        for (BlockItem& item : items)
            if (auto s = rebound(item.shape, from, to, rebuilt))
                item.shape = std::move(*s);
        done = makeBlock(ref->block->name, ref->block->base, std::move(items));
    }
    r.block = done;
    return r;
}

} // namespace

bool Document::defineBlock(std::shared_ptr<const Block> block)
{
    if (!block || block->name.empty())
        return false;
    const std::string key = linetypeKey(block->name);
    const std::shared_ptr<const Block> old = this->block(key);
    for (const BlockItem& item : block->items)
        if (referencesBlock(item.shape, block.get()) || (old && referencesBlock(item.shape, old.get())))
            return false;
    beginTransaction("");
    record(Change(key, old, block));
    if (old) {
        // Blocks that contain the old one get a copy with the new one.
        std::map<const Block*, std::shared_ptr<const Block>> rebuilt;
        for (const auto& [name, b] : std::map(blocks_)) {
            if (name == key)
                continue;
            bool uses = false;
            for (const BlockItem& item : b->items)
                uses = uses || referencesBlock(item.shape, old.get());
            if (!uses)
                continue;
            auto& copy = rebuilt[b.get()];
            if (!copy) {
                std::vector<BlockItem> items = b->items;
                for (BlockItem& item : items)
                    if (auto s = rebound(item.shape, old.get(), block, rebuilt))
                        item.shape = std::move(*s);
                copy = makeBlock(b->name, b->base, std::move(items));
            }
            record(Change(name, b, copy));
        }
        for (const auto& [id, e] : std::map(entities_))
            if (auto s = rebound(e.shape, old.get(), block, rebuilt))
                replace(id, std::move(*s));
    }
    commitTransaction();
    return true;
}

void Document::forEachPart(const Entity& e, const std::function<void(const Entity&)>& f) const
{
    forEachPart(e, e.shape, f);
}

void Document::forEachPart(const Entity& e, const Shape& shape, const std::function<void(const Entity&)>& f) const
{
    const auto* ref = std::get_if<Insert>(&shape);
    if (!ref) {
        if (&shape == &e.shape) {
            f(e);
            return;
        }
        Entity part = e;
        part.shape = shape;
        f(part);
        return;
    }
    if (!ref->block)
        return;
    const Transform t = insertTransform(*ref);
    for (const BlockItem& item : ref->block->items) {
        Entity part;
        part.id = e.id;
        part.story = e.story;
        part.layer = item.layer == "0" ? e.layer : item.layer;
        part.color = item.colorByBlock ? std::optional(colorOf(e)) : item.color;
        part.linetype = linetypeKey(item.linetype) == kByBlock ? linetypeOf(e).name : item.linetype;
        part.lineweight = item.lineweight == kLineweightByBlock ? lineweightOf(e) : item.lineweight;
        part.shape = transformed(item.shape, t);
        forEachPart(part, part.shape, f);
    }
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
    applyStyle(e, currentStyle_);
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

bool Document::restyle(EntityId id, const std::string& layerName, const EntityStyle& style)
{
    const Entity* e = entity(id);
    if (!e)
        return false;
    Entity updated = *e;
    updated.layer = layerName.empty() ? e->layer : layerName;
    applyStyle(updated, style);
    if (styleOf(updated) == styleOf(*e) && updated.layer == e->layer)
        return true;
    if (!layer(updated.layer))
        addLayer(Layer::named(updated.layer));
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

std::vector<EntityId> Document::entitiesIn(const BBox& box) const
{
    if (indexRevision_ != revision_) {
        std::vector<std::pair<EntityId, BBox>> items;
        items.reserve(entities_.size());
        for (const auto& [id, e] : entities_) {
            BBox b = boundingBox(e.shape);
            // A joined wall reaches past its own box by its mitred corners.
            if (const auto* w = std::get_if<Wall>(&e.shape))
                b = b.inflated(4.0 * w->thickness);
            items.emplace_back(id, b);
        }
        index_.build(std::move(items));
        indexRevision_ = revision_;
    }
    return index_.query(box);
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
        apply(*it, false);
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
        apply(c, true);
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
    apply(change, true);
    if (open_) {
        open_->changes.push_back(std::move(change));
    } else {
        undo_.push_back(Transaction{"", {std::move(change)}});
        redo_.clear();
    }
    modified_ = true;
    touch();
}

void Document::apply(const Change& change, bool forward)
{
    if (!change.block.empty()) {
        const auto& state = forward ? change.blockAfter : change.blockBefore;
        if (state)
            blocks_[change.block] = state;
        else
            blocks_.erase(change.block);
        return;
    }
    const auto& state = forward ? change.after : change.before;
    if (state)
        entities_[change.id] = *state;
    else
        entities_.erase(change.id);
}

void Document::touch()
{
    ++revision_;
}

} // namespace cad
