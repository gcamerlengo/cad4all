// SPDX-License-Identifier: GPL-3.0-or-later
// Toolbar icons drawn in code, so they stay sharp at any size and follow the
// palette (light or dark theme).
#pragma once

#include <QIcon>
#include <QString>

namespace icons {

// Names: line, polyline, circle, arc, rectangle, erase, move, copy, rotate,
// scale, mirror, offset, trim, extend, fillet, chamfer, stretch, text, dimLinear, dimAligned,
// dimRadius, dimDiameter, zoomExtents.
QIcon make(const QString& name);

} // namespace icons
