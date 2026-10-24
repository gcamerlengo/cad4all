// SPDX-License-Identifier: GPL-3.0-or-later
#include "plot.h"

#include "drawingpainter.h"

#include <QFileInfo>
#include <QPainter>
#include <QPdfWriter>

#include <algorithm>

using namespace cad;

namespace {

QColor plotColor(const PlotSettings& settings, Color c)
{
    // White and near-white lines would vanish on paper: print them black,
    // as AutoCAD does with colour 7.
    if (settings.monochrome || (c.r > 230 && c.g > 230 && c.b > 230))
        return Qt::black;
    return QColor(c.r, c.g, c.b);
}

QSizeF printableMm(const PlotSettings& settings, const BBox& area)
{
    const QPageLayout layout = plotPageLayout(settings, area);
    return layout.paintRect(QPageLayout::Millimeter).size();
}

} // namespace

QPageLayout plotPageLayout(const PlotSettings& settings, const BBox& area)
{
    QPageLayout::Orientation orientation = QPageLayout::Portrait;
    if (settings.orientation == PlotSettings::Orientation::Landscape
        || (settings.orientation == PlotSettings::Orientation::Auto && area.valid() && area.width() > area.height()))
        orientation = QPageLayout::Landscape;
    const double m = settings.marginMm;
    return QPageLayout(QPageSize(settings.paper), orientation, QMarginsF(m, m, m, m), QPageLayout::Millimeter);
}

double plotPaperPerUnit(const PlotSettings& settings, const BBox& area)
{
    if (settings.scale > 0.0)
        return settings.unitMm / settings.scale;
    const QSizeF paper = printableMm(settings, area);
    if (!area.valid())
        return 1.0;
    const double w = std::max(area.width(), 1e-9);
    const double h = std::max(area.height(), 1e-9);
    return std::min(paper.width() / w, paper.height() / h);
}

double plotScaleDenominator(const PlotSettings& settings, const BBox& area)
{
    return settings.unitMm / plotPaperPerUnit(settings, area);
}

void paintPlot(QPainter& p, const Document& doc, const PlotSettings& settings, const BBox& area, QSizeF deviceSize,
               double devicePerMm)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(QRectF(QPointF(0, 0), deviceSize));
    const double scale = plotPaperPerUnit(settings, area) * devicePerMm;
    const Vec2 center = area.valid() ? area.center() : Vec2{0, 0};
    DrawingPainter dp(p, center, scale, QPointF(deviceSize.width() / 2.0, deviceSize.height() / 2.0));
    // What fits on the sheet around the centre.
    const Vec2 half{deviceSize.width() / 2.0 / scale, deviceSize.height() / 2.0 / scale};
    const BBox sheet = BBox::fromCorners(center - half, center + half);
    dp.setVisibleArea(sheet);

    const double width = std::max(settings.lineWidthMm * devicePerMm, 0.5);
    const std::map<EntityId, WallFaces> walls = joinedWalls(doc);
    for (const auto& [id, e] : doc.entities()) {
        if (!doc.isVisible(e) || !drawnBox(e.shape).intersects(sheet))
            continue;
        const Layer* layer = doc.layer(e.layer);
        QPen pen(plotColor(settings, layer ? layer->color : Color{255, 255, 255}), width, Qt::SolidLine, Qt::RoundCap,
                 Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        if (const auto* w = std::get_if<Wall>(&e.shape))
            dp.drawWall(*w, walls.at(id));
        else
            dp.drawShape(e.shape);
    }
    p.restore();
}

bool writePdf(const QString& path, const Document& doc, const PlotSettings& settings, const BBox& area, QString* error)
{
    QPdfWriter pdf(path);
    pdf.setCreator(QStringLiteral("CAD4All"));
    pdf.setTitle(QFileInfo(path).completeBaseName());
    pdf.setResolution(1200);
    if (!pdf.setPageLayout(plotPageLayout(settings, area))) {
        if (error)
            *error = QStringLiteral("Formato di pagina non valido.");
        return false;
    }
    QPainter p;
    if (!p.begin(&pdf)) {
        if (error)
            *error = QStringLiteral("Impossibile scrivere il file.");
        return false;
    }
    const QRect paint = pdf.pageLayout().paintRectPixels(pdf.resolution());
    paintPlot(p, doc, settings, area, paint.size(), pdf.resolution() / 25.4);
    p.end();
    return true;
}
