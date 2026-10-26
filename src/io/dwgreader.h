// SPDX-License-Identifier: GPL-3.0-or-later
// DWG, AutoCAD's own format, with LibreDWG (GPL 3). Reading converts the
// drawing to DXF in memory for the DXF reader, so DWG supports exactly what
// DXF does. Writing goes the other way: the DXF 2000 that CAD4All writes is
// encoded as a DWG 2000. LibreDWG's encoder is still young, so DXF remains
// the safer way to hand a drawing over.
#pragma once

#include "document.h"

#include <QByteArray>
#include <QString>

namespace cad::io {

// False when CAD4All was built without LibreDWG.
bool dwgSupported();

bool fromDwg(const QByteArray& data, Document& doc, QString* message = nullptr);
bool loadDwg(Document& doc, const QString& path, QString* message = nullptr);

bool toDwg(const Document& doc, QByteArray& out, QString* error = nullptr);
bool saveDwg(const Document& doc, const QString& path, QString* error = nullptr);

} // namespace cad::io
