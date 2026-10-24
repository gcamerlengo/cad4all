// SPDX-License-Identifier: GPL-3.0-or-later
// Reading DWG, AutoCAD's own format, with LibreDWG (GPL 3). The drawing is
// converted to DXF in memory and read by the DXF reader, so DWG supports
// exactly what DXF does.
#pragma once

#include "document.h"

#include <QByteArray>
#include <QString>

namespace cad::io {

// False when CAD4All was built without LibreDWG.
bool dwgSupported();

bool fromDwg(const QByteArray& data, Document& doc, QString* message = nullptr);
bool loadDwg(Document& doc, const QString& path, QString* message = nullptr);

} // namespace cad::io
