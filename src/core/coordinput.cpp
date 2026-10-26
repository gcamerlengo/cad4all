// SPDX-License-Identifier: GPL-3.0-or-later
#include "coordinput.h"

#include <cctype>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>

namespace cad {

namespace {

// A typed angle on a quadrant (0, 90, 180, 270...) gives exact
// coordinates, without the rounding noise of cos(90 degrees).
Vec2 polarDegrees(double dist, double degrees)
{
    const double quadrants = degrees / 90.0;
    if (quadrants == std::floor(quadrants)) {
        static constexpr Vec2 kAxes[] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        const int q = static_cast<int>(std::fmod(std::fmod(quadrants, 4.0) + 4.0, 4.0));
        return kAxes[q] * dist;
    }
    return polar(dist, degToRad(degrees));
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

} // namespace

std::optional<double> parseNumber(std::string_view text)
{
    text = trim(text);
    if (text.empty())
        return std::nullopt;
    // Qt sets the C locale from the environment, so strtod would expect a
    // decimal comma on Italian systems. A classic-locale stream does not.
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    double value = 0.0;
    in >> value;
    if (in.fail())
        return std::nullopt;
    in >> std::ws;
    if (!in.eof())
        return std::nullopt;
    return value;
}

std::optional<Vec2> parsePoint(std::string_view text, std::optional<Vec2> lastPoint)
{
    text = trim(text);
    if (text.empty())
        return std::nullopt;

    bool relative = false;
    if (text.front() == '@') {
        if (!lastPoint)
            return std::nullopt;
        relative = true;
        text.remove_prefix(1);
        // "@" alone means the last point itself.
        if (trim(text).empty())
            return lastPoint;
    } else if (text.front() == '#') {
        text.remove_prefix(1);
    }

    Vec2 v;
    if (const auto lt = text.find('<'); lt != std::string_view::npos) {
        const auto dist = parseNumber(text.substr(0, lt));
        const auto ang = parseNumber(text.substr(lt + 1));
        if (!dist || !ang)
            return std::nullopt;
        v = polarDegrees(*dist, *ang);
    } else {
        const auto comma = text.find(',');
        if (comma == std::string_view::npos)
            return std::nullopt;
        const auto x = parseNumber(text.substr(0, comma));
        const auto y = parseNumber(text.substr(comma + 1));
        if (!x || !y)
            return std::nullopt;
        v = {*x, *y};
    }
    return relative ? *lastPoint + v : v;
}

} // namespace cad
