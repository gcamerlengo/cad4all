// SPDX-License-Identifier: GPL-3.0-or-later
// DXF exchange with AutoCAD and other CAD programs.
//
// Reading understands ASCII DXF of any version: lines, circles, arcs,
// polylines (with arcs), texts, multiline texts, dimensions, ellipses and
// splines (as polylines), solids, leaders, hatches and block references,
// with the layer table.
//
// Writing produces DXF 2000 (AC1015) by default, with lineweights, true
// colours, LWPOLYLINE, MTEXT and real HATCH entities; dimensions are real
// DIMENSION entities. Walls, slabs, roofs and stairs become anonymous blocks
// of their plan carrying the object in XDATA, so CAD4All reads them back as
// they were. DXF R12 is still available for old programs: hatches become
// blocks of lines, MTEXT becomes TEXT.
#pragma once

#include "document.h"

#include <QByteArray>
#include <QString>

namespace cad::io {

// Replaces `doc` with the drawing in `data`. On success `message` lists what
// was left out (unsupported entities), if anything. On failure `doc` is
// untouched and `message` says why.
bool fromDxf(const QByteArray& data, Document& doc, QString* message = nullptr);
enum class DxfVersion { R12, R2000 };
QByteArray toDxf(const Document& doc, DxfVersion version = DxfVersion::R2000);

bool loadDxf(Document& doc, const QString& path, QString* message = nullptr);
bool saveDxf(const Document& doc, const QString& path, QString* error = nullptr,
             DxfVersion version = DxfVersion::R2000);

// AutoCAD Color Index, now in the core (style.h).
using cad::aciToColor;
using cad::colorToAci;

} // namespace cad::io
