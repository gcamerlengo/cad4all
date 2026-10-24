// SPDX-License-Identifier: GPL-3.0-or-later
// Factories for the built-in commands. Each command lives in commands.cpp.
#pragma once

#include <memory>

namespace cad {

class Command;

std::unique_ptr<Command> makeLineCommand();
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
std::unique_ptr<Command> makeDimLinearCommand(bool aligned);
std::unique_ptr<Command> makeDimRadialCommand(bool diameter);
std::unique_ptr<Command> makeDimScaleCommand();
std::unique_ptr<Command> makeHatchCommand();
std::unique_ptr<Command> makeHatchEditCommand();
std::unique_ptr<Command> makeZoomCommand();
std::unique_ptr<Command> makeLayerCommand();

} // namespace cad
