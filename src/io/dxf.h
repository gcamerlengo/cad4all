// SPDX-License-Identifier: GPL-3.0-or-later
// DXF exchange with AutoCAD and other CAD programs.
//
// Reading understands ASCII DXF of any version: lines, circles, arcs,
// polylines (with arcs), texts, multiline texts, dimensions, ellipses and
// splines (as polylines), solids, leaders and block references (exploded),
// with the layer table. Writing produces DXF R12, which every CAD program
// opens; dimensions are written as real DIMENSION entities.
#pragma once

#include "document.h"

#include <QByteArray>
#include <QString>

namespace cad::io {

// Replaces `doc` with the drawing in `data`. On success `message` lists what
// was left out (unsupported entities), if anything. On failure `doc` is
// untouched and `message` says why.
bool fromDxf(const QByteArray& data, Document& doc, QString* message = nullptr);
QByteArray toDxf(const Document& doc);

bool loadDxf(Document& doc, const QString& path, QString* message = nullptr);
bool saveDxf(const Document& doc, const QString& path, QString* error = nullptr);

// AutoCAD Color Index, now in the core (style.h).
using cad::aciToColor;
using cad::colorToAci;

} // namespace cad::io
