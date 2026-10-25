// SPDX-License-Identifier: GPL-3.0-or-later
// Colours, linetypes and lineweights as the user reads and types them:
// "DaLayer", "Rosso", "30", "120,45,200", "0.35 mm".
#pragma once

#include "style.h"

#include <QString>

#include <optional>
#include <string>

namespace cad {

// Unset is DaLayer; the seven basic colours have their Italian name, the
// other ACI colours their number, the rest R,G,B.
QString colorName(const std::optional<Color>& color);
// The outer optional is empty when `text` is not a colour; the inner one is
// empty for DaLayer.
std::optional<std::optional<Color>> parseColor(const QString& text);

QString linetypeName(const std::string& name);

QString lineweightName(int weight);
// A width in millimetres (rounded to the nearest standard one), DaLayer or Predefinito.
std::optional<int> parseLineweight(const QString& text);

} // namespace cad
