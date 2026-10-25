// SPDX-License-Identifier: GPL-3.0-or-later
// Sheets for printing (AutoCAD's layouts):
// a paper size, viewports that show part of the drawing at a scale, and a
// title block. Sheet coordinates are millimetres from the bottom left.
#pragma once

#include "shapes.h"

#include <string>
#include <vector>

namespace cad {

struct PaperSize {
    std::string name;
    double width = 0.0;  // portrait, millimetres
    double height = 0.0;
};
// A4 to A0.
const std::vector<PaperSize>& paperSizes();
const PaperSize* paperSize(const std::string& name);

// A window on the drawing: the part of the sheet it covers, the drawing point
// at its centre and the N of its 1:N scale. Story 0 shows the current story.
struct Viewport {
    BBox sheet;
    Vec2 center;
    double scale = 100.0;
    int story = 0;
};

struct TitleBlock {
    std::string title;
    std::string project;
    std::string number;
    std::string date;
    std::string author;
};

struct Layout {
    std::string name;
    std::string paper = "A3";
    bool landscape = true;
    // Millimetres in one drawing unit (1: the drawing is in millimetres).
    double unitMm = 1.0;
    std::vector<Viewport> viewports;
    bool hasTitleBlock = true;
    TitleBlock titleBlock;

    // Width and height of the sheet in millimetres.
    Vec2 size() const;
};

inline constexpr double kSheetMargin = 10.0;
inline constexpr double kTitleBlockWidth = 180.0;
inline constexpr double kTitleBlockHeight = 36.0;

// Inside the frame, above the title block: where the drawing goes.
BBox drawingArea(const Layout& layout);
// The frame and the title block, lines and texts in sheet millimetres.
std::vector<Shape> sheetFrame(const Layout& layout);
// The part of the drawing a viewport shows.
BBox viewportArea(const Viewport& viewport, double unitMm);
// The standard 1:N scale (1, 2, 5, 10, 20, 25, 50, 100...) at least `n`.
double standardScale(double n);
// Replaces the viewports with one filling the drawing area and showing `area`
// at 1:`scale`, or at the standard scale that fits when `scale` is 0.
void fitViewport(Layout& layout, const BBox& area, double scale, int story);
// A sheet with one viewport showing `area` of the drawing: at 1:`scale`,
// or at the standard scale that fits when `scale` is 0.
Layout makeLayout(std::string name, std::string paper, bool landscape, const BBox& area, double scale, double unitMm, int story);

} // namespace cad
