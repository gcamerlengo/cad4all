// SPDX-License-Identifier: GPL-3.0-or-later
// The 3D model of the building as a mesh for other programs: Wavefront OBJ
// (with its .mtl colours, for Blender, SketchUp and renderers) and binary
// STL (for 3D printing). Sizes are in drawing units.
#pragma once

#include "building.h"

#include <QString>

namespace cad::io {

// Writes `path` and, next to it, the .mtl file with the materials. OBJ is
// Y-up: the height of the building goes along Y.
bool saveObj(const Model3D& model, const QString& path, QString* error = nullptr);
// Z-up, as slicers expect. The window panes are left out: they have no
// thickness to print.
bool saveStl(const Model3D& model, const QString& path, QString* error = nullptr);

} // namespace cad::io
