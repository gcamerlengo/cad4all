// SPDX-License-Identifier: GPL-3.0-or-later
// CAD4All native drawing format (.c4a): a readable JSON file.
#pragma once

#include "document.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace cad::io {

inline constexpr int kNativeFormatVersion = 5;

QByteArray toNativeJson(const Document& doc);
// Replaces the content of `doc`. On failure `doc` is left untouched.
bool fromNativeJson(const QByteArray& data, Document& doc, QString* error = nullptr);

// One shape as the file writes it; reading needs the document for the
// blocks a reference names. Also used for the objects kept in DXF XDATA.
QJsonObject shapeToNativeJson(const Shape& shape);
std::optional<Shape> shapeFromNativeJson(const QJsonObject& o, const Document& doc);

bool saveNative(const Document& doc, const QString& path, QString* error = nullptr);
bool loadNative(Document& doc, const QString& path, QString* error = nullptr);

} // namespace cad::io
