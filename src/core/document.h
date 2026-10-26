// SPDX-License-Identifier: GPL-3.0-or-later
// The drawing: layers, building stories, entities and the undo history.
#pragma once

#include "block.h"
#include "layout.h"
#include "shapes.h"
#include "style.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cad {

using EntityId = std::uint64_t;

struct Layer {
    std::string name;
    Color color;
    bool visible = true;
    bool locked = false;
    std::string linetype = kContinuous;
    int lineweight = kLineweightDefault;

    static Layer named(std::string name)
    {
        Layer l;
        l.name = std::move(name);
        return l;
    }
};

using StoryId = int;

// A floor of the building (a story). Every entity belongs to one;
// the plan shows the current story, the 3D view all of them, each raised to
// its elevation.
struct Story {
    StoryId id = 0;
    std::string name;
    double elevation = 0.0;
    double height = 3000.0;
};

struct Entity {
    EntityId id = 0;
    std::string layer = "0";
    Shape shape;
    // 0: the current story when added.
    StoryId story = 0;
    // Unset, BYLAYER and kLineweightByLayer: what the layer says.
    std::optional<Color> color;
    std::string linetype = kByLayer;
    int lineweight = kLineweightByLayer;
};

// The look an object takes from its layer or has of its own: the colour,
// linetype and lineweight of AutoCAD's Properties.
struct EntityStyle {
    std::optional<Color> color;
    std::string linetype = kByLayer;
    int lineweight = kLineweightByLayer;
    bool operator==(const EntityStyle&) const = default;
};
EntityStyle styleOf(const Entity& e);
void applyStyle(Entity& e, const EntityStyle& style);

// Millimetres in one drawing unit (INSUNITS), 0 when it has no unit.
double unitInMillimetres(int units);
// Its Italian name ("Millimetri"...).
const char* unitName(int units);

// Every change to the entities goes through add/remove/replace and is
// recorded, so any group of changes can be undone as one step. Changes made
// outside an explicit transaction become a transaction of their own.
class Document {
public:
    Document();

    // Layers. Layer "0" always exists, like in AutoCAD.
    const std::vector<Layer>& layers() const { return layers_; }
    const Layer* layer(const std::string& name) const;
    bool addLayer(const Layer& layer);
    bool updateLayer(const std::string& name, const Layer& value);
    const std::string& currentLayer() const { return currentLayer_; }
    bool setCurrentLayer(const std::string& name);
    // On the current story, visible and not locked: the entity can be picked and edited.
    bool isEditable(const Entity& e) const;
    // On the current story and on a visible layer: drawn in plan.
    bool isVisible(const Entity& e) const;
    // On a visible layer, whatever its story (the 3D view).
    bool isLayerVisible(const Entity& e) const;

    // Stories, from the lowest. There is always at least one.
    const std::vector<Story>& stories() const { return stories_; }
    const Story* story(StoryId id) const;
    const Story* storyNamed(const std::string& name) const;
    // Returns the id of the new story, 0 if the name is empty or taken.
    StoryId addStory(Story story);
    bool updateStory(StoryId id, const Story& value);
    // Only an empty story can go, and never the last one.
    bool removeStory(StoryId id);
    StoryId currentStory() const { return currentStory_; }
    bool setCurrentStory(StoryId id);
    // The next story down or up from `id`, if any.
    const Story* storyBelow(StoryId id) const;
    const Story* storyAbove(StoryId id) const;

    // Linetypes: the standard ones and those read from drawings.
    const std::vector<Linetype>& linetypes() const { return linetypes_; }
    // By name, whatever the case.
    const Linetype* linetype(const std::string& name) const;
    // False if the name is empty or taken.
    bool addLinetype(Linetype linetype);
    // Multiplies every dash length (AutoCAD's LTSCALE).
    double linetypeScale() const { return linetypeScale_; }
    void setLinetypeScale(double scale);

    // Drawing units, as AutoCAD's INSUNITS (0 none, 1 inches, 2 feet,
    // 4 millimetres, 5 centimetres, 6 metres), and the decimals shown for
    // lengths and coordinates (LUPREC, 0 to 8).
    int units() const { return units_; }
    void setUnits(int units);
    int precision() const { return precision_; }
    void setPrecision(int decimals);

    // What new objects get (CECOLOR, CELTYPE, CELWEIGHT); BYLAYER at first.
    const EntityStyle& currentStyle() const { return currentStyle_; }
    void setCurrentStyle(const EntityStyle& style);

    // What an object looks like once its layer is taken into account. The
    // lineweight may still be kLineweightDefault.
    Color colorOf(const Entity& e) const;
    const Linetype& linetypeOf(const Entity& e) const;
    int lineweightOf(const Entity& e) const;

    // Blocks, by name whatever the case.
    const std::map<std::string, std::shared_ptr<const Block>>& blocks() const { return blocks_; }
    std::shared_ptr<const Block> block(const std::string& name) const;
    // Adds a block, or redefines the one with the same name: every reference
    // to it, in the drawing and in other blocks, then shows the new objects.
    // Recorded for undo like the entities. False for an empty name or a
    // block that would contain itself.
    bool defineBlock(std::shared_ptr<const Block> block);

    // Sheets for printing, in the order of the book. Names are unique.
    const std::vector<Layout>& layouts() const { return layouts_; }
    const Layout* layout(const std::string& name) const;
    bool addLayout(const Layout& layout);
    // Replaces the sheet called `name` (the new one may have another name).
    bool updateLayout(const std::string& name, const Layout& layout);
    bool removeLayout(const std::string& name);

    // Calls `f` with what the entity draws: the entity itself, or each object
    // of a block reference placed in the drawing (nested blocks too), with
    // the layer and style it shows with (layer 0 and BYBLOCK resolved).
    void forEachPart(const Entity& e, const std::function<void(const Entity&)>& f) const;

    // Entities, kept in creation order (which is also the drawing order).
    const std::map<EntityId, Entity>& entities() const { return entities_; }
    const Entity* entity(EntityId id) const;
    EntityId add(Shape shape);
    EntityId add(Entity entity);
    bool remove(EntityId id);
    bool replace(EntityId id, Shape shape);
    // Changes the layer and style of an entity; its id, shape and story stay.
    bool restyle(EntityId id, const std::string& layer, const EntityStyle& style);
    BBox extents() const;

    // Undo history.
    void beginTransaction(const std::string& label);
    void commitTransaction();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    std::optional<std::string> undo();
    std::optional<std::string> redo();
    void clearHistory();

    // Removes everything and starts a fresh drawing.
    void reset();

    bool isModified() const { return modified_; }
    void setModified(bool modified) { modified_ = modified; }
    // Increases on every change; views use it to know when to repaint.
    std::uint64_t revision() const { return revision_; }

private:
    // Of an entity, or of a block definition when `block` is set.
    struct Change {
        Change(EntityId entity, std::optional<Entity> from, std::optional<Entity> to)
            : id(entity), before(std::move(from)), after(std::move(to))
        {
        }
        Change(std::string name, std::shared_ptr<const Block> from, std::shared_ptr<const Block> to)
            : block(std::move(name)), blockBefore(std::move(from)), blockAfter(std::move(to))
        {
        }

        EntityId id = 0;
        std::optional<Entity> before;
        std::optional<Entity> after;
        std::string block;
        std::shared_ptr<const Block> blockBefore;
        std::shared_ptr<const Block> blockAfter;
    };
    struct Transaction {
        std::string label;
        std::vector<Change> changes;
    };

    void record(Change change);
    void apply(const Change& change, bool forward);
    void forEachPart(const Entity& e, const Shape& shape, const std::function<void(const Entity&)>& f) const;
    void touch();

    std::vector<Layer> layers_;
    std::string currentLayer_;
    std::vector<Story> stories_;
    StoryId currentStory_ = 0;
    StoryId nextStory_ = 1;
    std::vector<Linetype> linetypes_;
    double linetypeScale_ = 1.0;
    int units_ = 4;
    int precision_ = 2;
    EntityStyle currentStyle_;
    std::map<std::string, std::shared_ptr<const Block>> blocks_;
    std::vector<Layout> layouts_;
    std::map<EntityId, Entity> entities_;
    EntityId nextId_ = 1;

    std::vector<Transaction> undo_;
    std::vector<Transaction> redo_;
    std::optional<Transaction> open_;
    int depth_ = 0;

    bool modified_ = false;
    std::uint64_t revision_ = 0;
};

} // namespace cad
