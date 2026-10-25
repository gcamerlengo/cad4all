// SPDX-License-Identifier: GPL-3.0-or-later
// The parameters of the architectural tools and objects, as a list of
// properties the windows can show and edit (the Info Box): the
// defaults of WALL, DOOR, WINDOW, SLAB, ROOF and STAIR, and the values of a
// selected wall, slab, roof or stair.
#pragma once

#include "building.h"
#include "document.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <string>
#include <vector>

namespace cad {

// What the architectural commands use for new objects, remembered between
// runs and shared with the windows.
struct ToolSettings {
    double wallThickness = 300.0;
    double wallHeight = 2700.0;
    Wall::Justification wallJustification = Wall::Justification::Center;
    Opening door{Opening::Kind::Door, 0.0, 900.0, 2100.0, 0.0};
    Opening window{Opening::Kind::Window, 0.0, 1200.0, 1400.0, 900.0};
    double slabThickness = 300.0;
    double slabOffset = 0.0;
    RoofStyle roofStyle = RoofStyle::Hip;
    double roofPitch = 30.0;  // degrees
    double roofOverhang = 500.0;
    double roofThickness = 250.0;
    std::optional<double> roofBase;  // unset: the top of the walls
    Stair::Kind stairKind = Stair::Kind::Straight;
    bool stairTurnRight = false;
    double stairWidth = 1000.0;
    double stairGoing = 280.0;
    std::optional<double> stairHeight;  // unset: the height of the story
};
ToolSettings& toolSettings();

// Steps no higher than this.
inline constexpr double kMaxRiser = 180.0;
int stairRisers(double height);

struct Property {
    enum class Type { Length, Angle, Factor, Choice };

    std::string key;
    QString label;
    Type type = Type::Length;
    double value = 0.0;  // a choice: its index; NaN: automatic
    QStringList choices;
    QString automatic;  // not empty: the value can be left to the program, shown with this text
    double minimum = 0.0;
    double maximum = 1e7;
};

// The tools that have settings, by command name (WALL, DOOR, ...).
bool hasToolProperties(const QString& command);
QString toolTitle(const QString& command);
std::vector<Property> toolProperties(const QString& command);
bool setToolProperty(const QString& command, const std::string& key, double value);

// A selected object's parameters; empty for shapes without any.
QString shapeTitle(const Shape& shape);
std::vector<Property> shapeProperties(const Shape& shape);
std::optional<Shape> withProperty(const Shape& shape, const std::string& key, double value);

} // namespace cad
