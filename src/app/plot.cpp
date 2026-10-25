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


// The visible objects of `story` with their colours, lineweights (the
// others `lineWidthMm` wide) and linetypes.
void drawStory(QPainter& p, DrawingPainter& dp, const Document& doc, StoryId story, bool monochrome, double lineWidthMm,
               double devicePerMm, double scale, const BBox& area)
{
    PlotSettings colours;
    colours.monochrome = monochrome;
    const double width = std::max(lineWidthMm * devicePerMm, 0.5);
    const std::map<EntityId, WallFaces> walls = joinedWalls(doc);
    for (const auto& [id, e] : doc.entities()) {
        if (e.story != story || !doc.isLayerVisible(e) || !drawnBox(e.shape).intersects(area))
            continue;
        doc.forEachPart(e, [&](const Entity& part) {
            if (&part != &e && !doc.isLayerVisible(part))
                return;
            // Objects with a lineweight print with it, the others with the width of the plot settings.
            const int weight = doc.lineweightOf(part);
            const double penWidth = weight >= 0 ? std::max(weight / 100.0 * devicePerMm, 0.5) : width;
            QPen pen(plotColor(colours, doc.colorOf(part)), penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            if (usesLinetype(part.shape))
                applyLinetype(pen, doc.linetypeOf(part), doc.linetypeScale(), scale);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            if (const auto* w = std::get_if<Wall>(&part.shape); w && &part == &e)
                dp.drawWall(*w, walls.at(id));
            else
                dp.drawShape(part.shape);
        });
    }
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

    drawStory(p, dp, doc, doc.currentStory(), settings.monochrome, settings.lineWidthMm, devicePerMm, scale, sheet);
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

void paintLayout(QPainter& p, const Document& doc, const Layout& layout, double devicePerMm, bool monochrome)
{
    const Vec2 size = layout.size();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF sheetCenter(size.x / 2.0 * devicePerMm, size.y / 2.0 * devicePerMm);
    DrawingPainter sheet(p, size * 0.5, devicePerMm, sheetCenter);
    for (const Viewport& v : layout.viewports) {
        const QPointF a = sheet.toDevice(v.sheet.min);
        const QPointF b = sheet.toDevice(v.sheet.max);
        const QRectF rect = QRectF(a, b).normalized();
        p.save();
        p.setClipRect(rect);
        const double scale = devicePerMm * layout.unitMm / v.scale;
        DrawingPainter dp(p, v.center, scale, rect.center());
        const BBox area = viewportArea(v, layout.unitMm);
        dp.setVisibleArea(area);
        const StoryId story = v.story != 0 && doc.story(v.story) ? v.story : doc.currentStory();
        drawStory(p, dp, doc, story, monochrome, 0.25, devicePerMm, scale, area);
        p.restore();
    }
    // Frame and title block in black, the frame thicker.
    const std::vector<Shape> frame = sheetFrame(layout);
    for (size_t i = 0; i < frame.size(); ++i) {
        p.setPen(QPen(Qt::black, std::max((i == 0 ? 0.5 : 0.25) * devicePerMm, 0.5)));
        p.setBrush(Qt::NoBrush);
        sheet.drawShape(frame[i]);
    }
    p.restore();
}

QPageLayout layoutPageLayout(const Layout& layout)
{
    const Vec2 size = layout.size();
    return QPageLayout(QPageSize(QSizeF(std::min(size.x, size.y), std::max(size.x, size.y)), QPageSize::Millimeter),
                       layout.landscape ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF(0, 0, 0, 0),
                       QPageLayout::Millimeter);
}

bool writeLayoutsPdf(const QString& path, const Document& doc, const std::vector<Layout>& layouts, bool monochrome, QString* error)
{
    if (layouts.empty()) {
        if (error)
            *error = QStringLiteral("Nessuna tavola da esportare.");
        return false;
    }
    QPdfWriter pdf(path);
    pdf.setCreator(QStringLiteral("CAD4All"));
    pdf.setTitle(QFileInfo(path).completeBaseName());
    pdf.setResolution(1200);
    pdf.setPageLayout(layoutPageLayout(layouts.front()));
    QPainter p;
    if (!p.begin(&pdf)) {
        if (error)
            *error = QStringLiteral("Impossibile scrivere il file.");
        return false;
    }
    for (size_t i = 0; i < layouts.size(); ++i) {
        if (i > 0) {
            pdf.setPageLayout(layoutPageLayout(layouts[i]));
            pdf.newPage();
        }
        paintLayout(p, doc, layouts[i], pdf.resolution() / 25.4, monochrome);
    }
    p.end();
    return true;
}
