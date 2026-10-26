// SPDX-License-Identifier: GPL-3.0-or-later
// Toolbar icons drawn in code, so they stay sharp at any size and follow the
// palette (light or dark theme).
#pragma once

#include <QIcon>
#include <QString>

namespace icons {

// Names: line, polyline, circle, arc, rectangle, hatch, erase, move, copy, rotate,
// scale, mirror, offset, trim, extend, fillet, chamfer, stretch, text, dimLinear, dimAligned,
// dimRadius, dimDiameter, zoomExtents, wall, door, window, slab, roof, stair, story, view3d,
// select, layers, new, open, save, print, export, undo, redo, plan, script,
// visible, hidden, locked, unlocked.
QIcon make(const QString& name);

} // namespace icons
