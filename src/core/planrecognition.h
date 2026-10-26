// SPDX-License-Identifier: GPL-3.0-or-later
// Turns a floor plan drawn with plain lines and arcs, as most DWG plans are,
// into walls with their doors and windows:
// - two parallel lines at a wall's distance are the faces of a wall (the
//   closest pairs win, so each line serves one wall);
// - a gap in a wall with a door swing (an arc hinged on a jamb) is a door,
//   a gap or a stretch of wall with glass lines between the faces is a window;
// - wall ends are carried to the axis of the wall they meet, so corners and
//   T junctions join.
#pragma once

#include "shapes.h"

#include <cstddef>
#include <vector>

namespace cad {

struct PlanRecognitionOptions {
    // Lines this far apart are the two faces of a wall.
    double minThickness = 80.0;
    double maxThickness = 600.0;
    // Gaps in a wall narrower than this are joints, not openings.
    double minOpening = 500.0;
    double maxOpening = 3500.0;
    double wallHeight = 2700.0;
    Opening door{Opening::Kind::Door, 0.0, 900.0, 2100.0, 0.0};
    Opening window{Opening::Kind::Window, 0.0, 1200.0, 1400.0, 900.0};

    // The same options for a drawing whose unit is `mmPerUnit` millimetres.
    PlanRecognitionOptions inUnits(double mmPerUnit) const;
};

struct RecognizedPlan {
    std::vector<Wall> walls;
    // Indices of the input shapes that the walls now stand for.
    std::vector<size_t> used;
    int doors = 0;
    int windows = 0;
};

RecognizedPlan recognizePlan(const std::vector<Shape>& shapes, const PlanRecognitionOptions& options = {});

// Millimetres per drawing unit that best explains the walls of the plan:
// 1 (millimetres), 10 (centimetres) or 1000 (metres).
double guessPlanUnit(const std::vector<Shape>& shapes);

} // namespace cad
