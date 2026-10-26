// SPDX-License-Identifier: GPL-3.0-or-later
// Factories for the built-in commands. Each command lives in commands.cpp.
#pragma once

#include "geometry.h"

#include <memory>

namespace cad {

class Command;

std::unique_ptr<Command> makeLineCommand();
std::unique_ptr<Command> makePointCommand();
std::unique_ptr<Command> makePlanTo3dCommand();
std::unique_ptr<Command> makePolylineCommand();
std::unique_ptr<Command> makeCircleCommand();
std::unique_ptr<Command> makeArcCommand();
std::unique_ptr<Command> makeRectangleCommand();
std::unique_ptr<Command> makeEraseCommand();
std::unique_ptr<Command> makeMoveCommand(bool copy);
std::unique_ptr<Command> makeRotateCommand();
std::unique_ptr<Command> makeScaleCommand();
std::unique_ptr<Command> makeMirrorCommand();
std::unique_ptr<Command> makeOffsetCommand();
std::unique_ptr<Command> makeTrimCommand();
std::unique_ptr<Command> makeExtendCommand();
std::unique_ptr<Command> makeFilletCommand();
std::unique_ptr<Command> makeChamferCommand();
std::unique_ptr<Command> makeStretchCommand();
std::unique_ptr<Command> makeTextCommand();
std::unique_ptr<Command> makeTextEditCommand();
std::unique_ptr<Command> makeMTextCommand();
std::unique_ptr<Command> makeDimLinearCommand(bool aligned);
std::unique_ptr<Command> makeDimRadialCommand(bool diameter);
std::unique_ptr<Command> makeDimScaleCommand();
std::unique_ptr<Command> makeHatchCommand();
std::unique_ptr<Command> makeHatchEditCommand();
std::unique_ptr<Command> makeWallCommand();
std::unique_ptr<Command> makeDoorCommand();
std::unique_ptr<Command> makeWindowCommand();
std::unique_ptr<Command> makeWallEditCommand();
std::unique_ptr<Command> makeStoryCommand();
std::unique_ptr<Command> makeSlabCommand();
std::unique_ptr<Command> makeRoofCommand();
std::unique_ptr<Command> makeStairCommand();
std::unique_ptr<Command> makeView3dCommand();
std::unique_ptr<Command> makeZoomCommand();
std::unique_ptr<Command> makeLayerCommand();
std::unique_ptr<Command> makeLayoutCommand();
std::unique_ptr<Command> makeSectionCommand();
std::unique_ptr<Command> makeElevationCommand();
std::unique_ptr<Command> makePlotCommand();
std::unique_ptr<Command> makeColorCommand();
std::unique_ptr<Command> makeLinetypeCommand();
std::unique_ptr<Command> makeLineweightCommand();
std::unique_ptr<Command> makeLinetypeScaleCommand();
std::unique_ptr<Command> makeChangePropertiesCommand();
std::unique_ptr<Command> makeMatchPropertiesCommand();
std::unique_ptr<Command> makeArrayCommand();
std::unique_ptr<Command> makeExplodeCommand();
std::unique_ptr<Command> makeJoinCommand();
std::unique_ptr<Command> makeEllipseCommand();
std::unique_ptr<Command> makeSplineCommand();
std::unique_ptr<Command> makeLeaderCommand();
std::unique_ptr<Command> makeDistanceCommand();
std::unique_ptr<Command> makeAreaCommand();
std::unique_ptr<Command> makeListCommand();
std::unique_ptr<Command> makeBlockCommand();
std::unique_ptr<Command> makeInsertCommand();
// Drags the grips at `grip` of the selected objects (started by the view).
std::unique_ptr<Command> makeGripCommand(Vec2 grip);

} // namespace cad
