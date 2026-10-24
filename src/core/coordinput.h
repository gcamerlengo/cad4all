// SPDX-License-Identifier: GPL-3.0-or-later
// Parsing of what the user types on the command line, with AutoCAD syntax:
//   10,20      absolute point
//   @5,0       relative to the last point
//   @10<45     relative polar: distance 10 at 45 degrees
//   10<45      absolute polar
//   #10,20     explicitly absolute (same as 10,20)
// Decimals always use a dot; the comma separates X and Y.
#pragma once

#include "geometry.h"

#include <optional>
#include <string_view>

namespace cad {

std::optional<double> parseNumber(std::string_view text);
std::optional<Vec2> parsePoint(std::string_view text, std::optional<Vec2> lastPoint);

} // namespace cad
