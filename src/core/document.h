// SPDX-License-Identifier: GPL-3.0-or-later
// The drawing: layers, entities and the undo history.
#pragma once

#include "shapes.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cad {

using EntityId = std::uint64_t;

struct Color {
    std::uint8_t r = 255, g = 255, b = 255;
    bool operator==(const Color&) const = default;
};

struct Layer {
    std::string name;
    Color color;
    bool visible = true;
    bool locked = false;

    static Layer named(std::string name)
    {
        Layer l;
        l.name = std::move(name);
        return l;
    }
};

struct Entity {
    EntityId id = 0;
    std::string layer = "0";
    Shape shape;
};

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
    // Visible and not locked: the entity can be picked and edited.
    bool isEditable(const Entity& e) const;
    bool isVisible(const Entity& e) const;

    // Entities, kept in creation order (which is also the drawing order).
    const std::map<EntityId, Entity>& entities() const { return entities_; }
    const Entity* entity(EntityId id) const;
    EntityId add(Shape shape);
    EntityId add(Entity entity);
    bool remove(EntityId id);
    bool replace(EntityId id, Shape shape);
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
    struct Change {
        EntityId id = 0;
        std::optional<Entity> before;
        std::optional<Entity> after;
    };
    struct Transaction {
        std::string label;
        std::vector<Change> changes;
    };

    void record(Change change);
    void applyState(EntityId id, const std::optional<Entity>& state);
    void touch();

    std::vector<Layer> layers_;
    std::string currentLayer_;
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
