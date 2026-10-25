// SPDX-License-Identifier: GPL-3.0-or-later
#include "style.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace cad {

namespace {

// HSV to RGB, all in [0, 1] (hue in turns).
Color hsv(double h, double s, double v)
{
    const double i = std::floor(h * 6.0);
    const double f = h * 6.0 - i;
    const double p = v * (1.0 - s);
    const double q = v * (1.0 - f * s);
    const double t = v * (1.0 - (1.0 - f) * s);
    double r = v, g = t, b = p;
    switch (static_cast<int>(i) % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
    }
    auto byte = [](double x) { return static_cast<std::uint8_t>(std::lround(std::clamp(x, 0.0, 1.0) * 255.0)); };
    return {byte(r), byte(g), byte(b)};
}

} // namespace

Color aciToColor(int index)
{
    switch (index) {
    case 1: return {255, 0, 0};
    case 2: return {255, 255, 0};
    case 3: return {0, 255, 0};
    case 4: return {0, 255, 255};
    case 5: return {0, 0, 255};
    case 6: return {255, 0, 255};
    case 7: return {255, 255, 255};
    case 8: return {128, 128, 128};
    case 9: return {192, 192, 192};
    default: break;
    }
    if (index >= 250 && index <= 255) {
        static const std::uint8_t grays[] = {51, 80, 105, 130, 190, 255};
        const std::uint8_t g = grays[index - 250];
        return {g, g, g};
    }
    if (index < 10 || index > 249)
        return {255, 255, 255};
    // 10..249: 24 hues of 15 degrees, each in five shades, full and pale.
    const int hue = (index - 10) / 10;
    const int shade = index % 10;
    static const double values[] = {1.0, 0.65, 0.5, 0.3, 0.15};
    return hsv(hue / 24.0, shade % 2 == 0 ? 1.0 : 0.5, values[shade / 2]);
}

int colorToAci(Color color)
{
    int best = 7;
    int bestDist = std::numeric_limits<int>::max();
    for (int i = 1; i <= 255; ++i) {
        const Color c = aciToColor(i);
        const int dr = c.r - color.r, dg = c.g - color.g, db = c.b - color.b;
        const int d = dr * dr + dg * dg + db * db;
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

std::optional<int> exactAci(Color color)
{
    const int aci = colorToAci(color);
    if (aciToColor(aci) == color)
        return aci;
    return std::nullopt;
}

double Linetype::patternLength() const
{
    double total = 0.0;
    for (double d : pattern)
        total += std::abs(d);
    return total;
}

const std::vector<Linetype>& standardLinetypes()
{
    static const std::vector<Linetype> types = {
        {kContinuous, "Continua ________", {}},
        {"DASHED", "Tratteggiata __ __ __", {12.7, -6.35}},
        {"HIDDEN", "Nascosta _ _ _ _", {6.35, -3.175}},
        {"CENTER", "Asse ____ _ ____ _", {31.75, -6.35, 6.35, -6.35}},
        {"DASHDOT", "Tratto punto __ . __ .", {12.7, -6.35, 0.0, -6.35}},
        {"PHANTOM", "Fantasma ____ _ _ ____", {31.75, -6.35, 6.35, -6.35, 6.35, -6.35}},
        {"DOT", "Punti . . . . .", {0.0, -6.35}},
    };
    return types;
}

std::string linetypeKey(const std::string& name)
{
    std::string key = name;
    for (char& c : key)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return key;
}

bool isStandardLinetype(const std::string& name)
{
    const std::string key = linetypeKey(name);
    return std::any_of(standardLinetypes().begin(), standardLinetypes().end(),
                       [&](const Linetype& t) { return t.name == key; });
}

const std::vector<int>& standardLineweights()
{
    static const std::vector<int> weights = {0, 5, 9, 13, 15, 18, 20, 25, 30, 35, 40, 50,
                                             53, 60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211};
    return weights;
}

int nearestLineweight(int hundredths)
{
    int best = 0;
    for (int w : standardLineweights())
        if (std::abs(w - hundredths) < std::abs(best - hundredths))
            best = w;
    return best;
}

} // namespace cad
