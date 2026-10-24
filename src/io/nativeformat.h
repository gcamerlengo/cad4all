// SPDX-License-Identifier: GPL-3.0-or-later
// CAD4All native drawing format (.c4a): a readable JSON file.
#pragma once

#include "document.h"

#include <QByteArray>
#include <QString>

namespace cad::io {

inline constexpr int kNativeFormatVersion = 1;

QByteArray toNativeJson(const Document& doc);
// Replaces the content of `doc`. On failure `doc` is left untouched.
bool fromNativeJson(const QByteArray& data, Document& doc, QString* error = nullptr);

bool saveNative(const Document& doc, const QString& path, QString* error = nullptr);
bool loadNative(Document& doc, const QString& path, QString* error = nullptr);

} // namespace cad::io
