// SPDX-License-Identifier: GPL-3.0-or-later
// Building elements: how walls join each other, what they draw in plan
// (faces, openings, door swings, window panes) and their 3D model.
#pragma once

#include "document.h"
#include "shapes.h"

#include <array>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace cad {

// Unit direction of the wall axis and the normal to its left.
Vec2 wallDirection(const Wall& w);
inline Vec2 wallNormal(const Wall& w) { return perpendicular(wallDirection(w)); }
inline double wallLength(const Wall& w) { return distance(w.start, w.end); }
// Offsets of the left and right faces from the reference line, along wallNormal().
double wallLeftOffset(const Wall& w);
double wallRightOffset(const Wall& w);
// Distance along the axis from the wall start of the projection of `p`.
double wallParam(const Wall& w, Vec2 p);
// Point at `t` along the axis, `offset` to the left of the reference line.
Vec2 wallPoint(const Wall& w, double t, double offset = 0.0);

// Range along the axis taken by an opening.
inline double openingStart(const Opening& o) { return o.position - o.width / 2.0; }
inline double openingEnd(const Opening& o) { return o.position + o.width / 2.0; }
// `o` moved inside the wall if needed. Nullopt when it does not fit there
// or overlaps another opening of the wall.
std::optional<Opening> fitOpening(const Wall& w, Opening o);

// The outline of a wall in plan once joined to its neighbours.
struct WallFaces {
    // Start right, end right, end left, start left.
    std::array<Vec2, 4> corners;
    // A joined end has no end face; a straight end continues into a
    // collinear wall, so not even the corner edges show.
    bool startJoined = false;
    bool endJoined = false;
    bool startStraight = false;
    bool endStraight = false;
    // Ranges along the axis where the left or right face is hidden by a
    // wall meeting it in T.
    std::vector<std::pair<double, double>> leftGaps;
    std::vector<std::pair<double, double>> rightGaps;
};

// The wall on its own: square ends.
WallFaces wallFaces(const Wall& w);
// One entry per wall: L corners mitred, T junctions trimmed to the face of
// the wall they meet, collinear walls continued.
std::vector<WallFaces> joinWalls(const std::vector<Wall>& walls);
// The joined faces of every wall of the drawing (walls join only on their own story).
std::map<EntityId, WallFaces> joinedWalls(const Document& doc);

// What the wall draws in plan: the faces with gaps at the openings, the
// jambs, door leaves and swings, window panes.
std::vector<Shape> wallPlan(const Wall& w, const WallFaces& faces);
inline std::vector<Shape> wallPlan(const Wall& w) { return wallPlan(w, wallFaces(w)); }
// The footprint as a closed polygon.
Polyline wallOutline(const WallFaces& faces);
// The footprint without the openings: the parts of the wall cut by the plan.
std::vector<std::vector<Vec2>> wallSolidParts(const Wall& w, const WallFaces& faces);

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    constexpr Vec3(Vec2 p, double z_) : x(p.x), y(p.y), z(z_) {}

    constexpr Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    bool operator==(const Vec3&) const = default;
};

inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double length(Vec3 v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalized(Vec3 v)
{
    const double len = length(v);
    return len < kEps ? Vec3{} : v * (1.0 / len);
}

// Triangles and visible edges of the building, z up from the base of the walls.
struct Model3D {
    enum class Material { Wall, Glass, Wood, Roof };
    struct Triangle {
        std::array<Vec3, 3> v;
        Material material = Material::Wall;
    };

    std::vector<Triangle> triangles;
    std::vector<std::array<Vec3, 2>> edges;

    bool empty() const { return triangles.empty(); }
    // Corners of the box around the model.
    std::pair<Vec3, Vec3> bounds() const;
};

// Height above the story of one plane of a roof: z = c + grad . p.
struct RoofPlane {
    Vec2 grad;
    double c = 0.0;

    double at(Vec2 p) const { return c + dot(grad, p); }
};

// A convex part of a roof, a roof of its own: each sloping edge of its
// footprint gives a plane, its height is the lowest of them.
struct RoofPiece {
    std::vector<Vec2> footprint;
    std::vector<Vec2> outline;
    std::vector<bool> gables;
    std::vector<RoofPlane> planes;  // empty: flat, at the base
    std::vector<int> edges;         // the footprint edge of each plane

    double height(Vec2 p, double base) const;
    bool contains(Vec2 p) const;
};

// A visible face of a roof in plan: a convex polygon on one plane. `edge`
// is the sloping edge of its piece, -1 on a flat roof.
struct RoofFace {
    std::vector<Vec2> polygon;
    int edge = -1;
    int piece = 0;
    RoofPlane plane;
};

// A roof broken down. A convex footprint is one piece; a footprint with
// right angles only (L, T, U plans...) is covered by the largest rectangles
// that fit in it, each a roof of its own, and the roof is the highest of
// them: the wings meet along valleys, like roofs drawn one per wing.
struct RoofGeometry {
    std::vector<Vec2> footprint;  // corners without repeats; `gables` refers to its edges
    std::vector<Vec2> outline;    // grown by the overhang
    std::vector<RoofPiece> pieces;
    std::vector<RoofFace> faces;
    // Ridges, hips and valleys: where two faces meet.
    std::vector<std::array<Vec2, 2>> lines;
    double base = 0.0;

    // Height of the underside of the roof at `p`, above the story.
    double height(Vec2 p) const;
    bool flat() const;
};
// Nullopt when the footprint has less than three corners, or is neither
// convex nor made of right angles.
std::optional<RoofGeometry> roofGeometry(const Roof& roof);
// Height of the underside of the roof at `p`, above its story.
double roofHeight(const Roof& roof, Vec2 p);
// What the roof draws in plan: its outline and the ridges, hips and valleys.
std::vector<Shape> roofPlan(const Roof& roof);

// A step of a stair (or its landing): its plan and the height of its top.
struct StairStep {
    std::vector<Vec2> polygon;
    double top = 0.0;
};
std::vector<StairStep> stairSteps(const Stair& stair);
// What a stair draws in plan: the steps and the walking line with its arrow.
std::vector<Shape> stairPlan(const Stair& stair);

enum class RoofStyle { Hip, Gable, Shed, Flat };
// The gable ends of a footprint for each style: none for a hip roof; the
// ends of the wings for a gable roof; all but the longest edge for a shed
// roof; all for a flat roof.
std::vector<bool> roofGables(const std::vector<Vec2>& footprint, RoofStyle style);

Model3D buildingModel(const std::vector<Wall>& walls);
// Walls, slabs and roofs of the visible layers, each story at its elevation.
Model3D buildingModel(const Document& doc);

} // namespace cad
