// SPDX-License-Identifier: GPL-3.0-or-later
#include "layout.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace cad {

const std::vector<PaperSize>& paperSizes()
{
    static const std::vector<PaperSize> sizes = {
        {"A4", 210.0, 297.0}, {"A3", 297.0, 420.0}, {"A2", 420.0, 594.0}, {"A1", 594.0, 841.0}, {"A0", 841.0, 1189.0},
    };
    return sizes;
}

const PaperSize* paperSize(const std::string& name)
{
    for (const PaperSize& p : paperSizes())
        if (p.name == name)
            return &p;
    return nullptr;
}

Vec2 Layout::size() const
{
    const PaperSize* p = paperSize(paper);
    const PaperSize& s = p ? *p : paperSizes()[1];
    return landscape ? Vec2{s.height, s.width} : Vec2{s.width, s.height};
}

BBox drawingArea(const Layout& layout)
{
    const Vec2 size = layout.size();
    const double bottom = kSheetMargin + (layout.hasTitleBlock ? kTitleBlockHeight : 0.0);
    return BBox::fromCorners({kSheetMargin, bottom}, {size.x - kSheetMargin, size.y - kSheetMargin});
}

namespace {

Text label(Vec2 at, double height, const std::string& content)
{
    Text t;
    t.position = at;
    t.height = height;
    t.content = content;
    return t;
}

std::string scaleText(const Layout& layout)
{
    if (layout.viewports.empty())
        return {};
    std::ostringstream out;
    out << "1:" << formatMeasurement(layout.viewports.front().scale);
    return out.str();
}

} // namespace

std::vector<Shape> sheetFrame(const Layout& layout)
{
    const Vec2 size = layout.size();
    const Vec2 lo{kSheetMargin, kSheetMargin};
    const Vec2 hi = size - lo;
    std::vector<Shape> out;
    out.push_back(Polyline({lo, {hi.x, lo.y}, hi, {lo.x, hi.y}}, true));
    if (!layout.hasTitleBlock)
        return out;
    // Title block in the bottom right corner: title and project on top, then
    // four cells for scale, date, sheet number and author.
    const double w = std::min(kTitleBlockWidth, hi.x - lo.x);
    const double x0 = hi.x - w;
    const double y0 = lo.y;
    const double y1 = y0 + kTitleBlockHeight;
    const double yMid = y0 + 12.0;
    out.push_back(Polyline({{x0, y0}, {hi.x, y0}, {hi.x, y1}, {x0, y1}}, true));
    out.push_back(Line{{lo.x, y1}, {x0, y1}});
    out.push_back(Line{{x0, yMid}, {hi.x, yMid}});
    const double cell = w / 4.0;
    for (int i = 1; i < 4; ++i)
        out.push_back(Line{{x0 + cell * i, y0}, {x0 + cell * i, yMid}});
    const TitleBlock& tb = layout.titleBlock;
    out.push_back(label({x0 + 3.0, y1 - 9.0}, 5.0, tb.title.empty() ? layout.name : tb.title));
    if (!tb.project.empty())
        out.push_back(label({x0 + 3.0, y1 - 18.0}, 3.0, tb.project));
    const std::pair<const char*, std::string> cells[] = {
        {"Scala", scaleText(layout)}, {"Data", tb.date}, {"Tavola", tb.number}, {"Disegnatore", tb.author}};
    for (int i = 0; i < 4; ++i) {
        const double x = x0 + cell * i + 2.0;
        out.push_back(label({x, yMid - 4.0}, 1.8, cells[i].first));
        if (!cells[i].second.empty())
            out.push_back(label({x, y0 + 2.5}, 3.0, cells[i].second));
    }
    return out;
}

BBox viewportArea(const Viewport& v, double unitMm)
{
    const double k = v.scale / unitMm;  // drawing units per sheet millimetre
    const Vec2 half{v.sheet.width() / 2.0 * k, v.sheet.height() / 2.0 * k};
    return BBox::fromCorners(v.center - half, v.center + half);
}

double standardScale(double n)
{
    if (!(n > 0.0) || !std::isfinite(n))
        return 1.0;
    // 1, 2, 2.5, 5 in every decade, as on architects' scale rules.
    static const double steps[] = {1.0, 2.0, 2.5, 5.0};
    double decade = std::pow(10.0, std::floor(std::log10(n)));
    for (;;) {
        for (double s : steps)
            if (s * decade >= n * (1.0 - 1e-9))
                return s * decade;
        decade *= 10.0;
    }
}

void fitViewport(Layout& layout, const BBox& area, double scale, int story)
{
    Viewport v;
    v.sheet = drawingArea(layout);
    v.story = story;
    if (area.valid()) {
        v.center = area.center();
        if (scale <= 0.0) {
            // Drawing units per sheet millimetre that fit, as 1:N.
            const double fit = std::max(area.width() / v.sheet.width(), area.height() / v.sheet.height()) * layout.unitMm;
            scale = standardScale(fit * 1.05);
        }
    }
    v.scale = scale > 0.0 ? scale : 100.0;
    layout.viewports = {v};
}

Layout makeLayout(std::string name, std::string paper, bool landscape, const BBox& area, double scale, double unitMm, int story)
{
    Layout layout;
    layout.name = std::move(name);
    layout.paper = paperSize(paper) ? std::move(paper) : std::string("A3");
    layout.landscape = landscape;
    layout.unitMm = unitMm > 0.0 ? unitMm : 1.0;
    fitViewport(layout, area, scale, story);
    return layout;
}

} // namespace cad
