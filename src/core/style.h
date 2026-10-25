// SPDX-License-Identifier: GPL-3.0-or-later
// How lines look: colours (with the AutoCAD Color Index), linetypes and
// lineweights, for layers and for single objects.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cad {

struct Color {
    std::uint8_t r = 255, g = 255, b = 255;
    bool operator==(const Color&) const = default;
};

// AutoCAD Color Index: the RGB of index 1..255, and the index nearest to a colour.
Color aciToColor(int index);
int colorToAci(Color color);
// The index of a colour that is exactly one of the ACI colours, if it is.
std::optional<int> exactAci(Color color);

// A dashed pattern, as in AutoCAD's .lin files: a dash for a positive
// length, a gap for a negative one, a dot for zero. Empty: continuous.
struct Linetype {
    std::string name;
    std::string description;
    std::vector<double> pattern;

    double patternLength() const;
};

inline const std::string kContinuous = "CONTINUOUS";
// Names an object uses to take the linetype of its layer.
inline const std::string kByLayer = "BYLAYER";

// The linetypes every drawing has, with the ISO lengths in millimetres
// (acadiso.lin): CONTINUOUS, DASHED, HIDDEN, CENTER, DASHDOT, PHANTOM, DOT.
const std::vector<Linetype>& standardLinetypes();
bool isStandardLinetype(const std::string& name);
// Upper case, so names compare like AutoCAD's.
std::string linetypeKey(const std::string& name);

// Lineweights in hundredths of a millimetre, as in DXF group 370.
inline constexpr int kLineweightByLayer = -1;
inline constexpr int kLineweightByBlock = -2;
inline constexpr int kLineweightDefault = -3;
// What "Default" prints and shows as (AutoCAD's LWDEFAULT).
inline constexpr int kDefaultLineweight = 25;
// The lineweights AutoCAD offers, from 0.00 to 2.11 mm.
const std::vector<int>& standardLineweights();
// The standard lineweight nearest to `hundredths`.
int nearestLineweight(int hundredths);

} // namespace cad
