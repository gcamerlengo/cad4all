// SPDX-License-Identifier: GPL-3.0-or-later
// Printing and PDF output (AutoCAD's PLOT): the drawing on a sheet of
// paper, fitted or at a scale such as 1:50, in colour or black and white.
#pragma once

#include "document.h"

#include <QPageLayout>
#include <QPageSize>
#include <QString>

class QPainter;

struct PlotSettings {
    enum class Orientation { Auto, Portrait, Landscape };

    QPageSize::PageSizeId paper = QPageSize::A4;
    Orientation orientation = Orientation::Auto;
    // 0: fit to the sheet. Otherwise N of a 1:N scale.
    double scale = 0.0;
    // Size of one drawing unit in millimetres (1 = millimetres, 10 = centimetres...).
    double unitMm = 1.0;
    bool monochrome = true;
    double lineWidthMm = 0.25;
    double marginMm = 10.0;
};

// Sheet size and orientation for printing `area`.
QPageLayout plotPageLayout(const PlotSettings& settings, const cad::BBox& area);
// Millimetres of paper per drawing unit.
double plotPaperPerUnit(const PlotSettings& settings, const cad::BBox& area);
// N of the 1:N scale actually used (the fitted one when fitting).
double plotScaleDenominator(const PlotSettings& settings, const cad::BBox& area);

// Draws `area` of the drawing centred on a printable area of `deviceSize`
// device units, `devicePerMm` device units per millimetre of paper.
void paintPlot(QPainter& painter, const cad::Document& doc, const PlotSettings& settings, const cad::BBox& area,
               QSizeF deviceSize, double devicePerMm);

bool writePdf(const QString& path, const cad::Document& doc, const PlotSettings& settings, const cad::BBox& area,
              QString* error = nullptr);

// A sheet (layout) drawn on a device with its top left at the origin,
// `devicePerMm` device units per millimetre of paper.
void paintLayout(QPainter& painter, const cad::Document& doc, const cad::Layout& layout, double devicePerMm, bool monochrome);
QPageLayout layoutPageLayout(const cad::Layout& layout);
// One page per sheet.
bool writeLayoutsPdf(const QString& path, const cad::Document& doc, const std::vector<cad::Layout>& layouts, bool monochrome,
                     QString* error = nullptr);
