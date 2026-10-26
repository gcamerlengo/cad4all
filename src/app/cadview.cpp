// SPDX-License-Identifier: GPL-3.0-or-later
#include "cadview.h"

#include "drawingpainter.h"

#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

using namespace cad;

namespace {

const QColor kBackground(33, 40, 48);
// The story below, as a reference for drawing the current one.
const QColor kStoryBelow(88, 96, 106);
const QColor kGridMinor(42, 50, 60);
const QColor kGridMajor(54, 63, 75);
const QColor kAxisX(150, 60, 60);
const QColor kAxisY(60, 140, 70);
const QColor kPreview(120, 190, 255);
const QColor kSelected(78, 161, 255);
const QColor kSnap(240, 195, 48);
const QColor kCrosshair(220, 220, 220);
const QColor kGrip(30, 120, 255);
const QColor kGripHover(240, 70, 70);

constexpr double kPickPixels = 5.0;
constexpr double kSnapPixels = 10.0;
// Half the side of a grip square; a click this close picks it.
constexpr double kGripPixels = 5.0;
constexpr double kZoomStep = 1.2;

QColor toQColor(Color c)
{
    return QColor(c.r, c.g, c.b);
}

QString snapLabel(SnapType type)
{
    switch (type) {
    case SnapEndpoint: return CadView::tr("Fine");
    case SnapMidpoint: return CadView::tr("Punto medio");
    case SnapCenter: return CadView::tr("Centro");
    case SnapQuadrant: return CadView::tr("Quadrante");
    case SnapIntersection: return CadView::tr("Intersezione");
    case SnapNearest: return CadView::tr("Vicino");
    case SnapInsertion: return CadView::tr("Inserimento");
    case SnapNode: return CadView::tr("Nodo");
    }
    return {};
}

} // namespace

CadView::CadView(CommandEngine& engine, QWidget* parent)
    : QWidget(parent), engine_(engine)
{
    setMouseTracking(true);
    // Keyboard input stays on the command line, as in AutoCAD.
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::BlankCursor);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 240);
    connect(&engine_, &CommandEngine::changed, this, [this] {
        // A command may have started or ended: refresh snap and preview.
        if (mouseInside_)
            updateCursor(mouse_);
        update();
    });
    connect(&engine_, &CommandEngine::zoomExtentsRequested, this, &CadView::zoomExtents);
    connect(&engine_, &CommandEngine::zoomWindowRequested, this, &CadView::zoomWindow);
    connect(&engine_, &CommandEngine::zoomFactorRequested, this, &CadView::zoomFactor);
    connect(&engine_, &CommandEngine::zoomPreviousRequested, this, &CadView::zoomPrevious);
    settle_.setSingleShot(true);
    settle_.setInterval(150);
    connect(&settle_, &QTimer::timeout, this, [this] {
        moving_ = false;
        update();
    });
}

void CadView::viewMoving()
{
    moving_ = true;
    settle_.start();
}

Vec2 CadView::toWorld(QPointF s) const
{
    return {center_.x + (s.x() - width() / 2.0) / scale_, center_.y - (s.y() - height() / 2.0) / scale_};
}

QPointF CadView::toScreen(Vec2 w) const
{
    return {width() / 2.0 + (w.x - center_.x) * scale_, height() / 2.0 - (w.y - center_.y) * scale_};
}

BBox CadView::visibleArea() const
{
    return BBox::fromCorners(toWorld({0, 0}), toWorld(QPointF(width(), height())));
}

void CadView::setGridVisible(bool on)
{
    grid_ = on;
    update();
}

void CadView::setLineweightsVisible(bool on)
{
    lineweights_ = on;
    update();
}

void CadView::zoomExtents()
{
    const BBox box = engine_.document().extents();
    if (!box.valid()) {
        rememberView();
        center_ = {0.0, 0.0};
        scale_ = 20.0;
        update();
        return;
    }
    zoomWindow(box);
}

void CadView::zoomWindow(const BBox& box)
{
    rememberView();
    center_ = box.center();
    const double w = std::max(box.width(), 1e-6);
    const double h = std::max(box.height(), 1e-6);
    // Leave a 5% margin around the area.
    scale_ = std::clamp(0.9 * std::min(width() / w, height() / h), 1e-6, 1e9);
    update();
}

void CadView::zoomFactor(double factor)
{
    rememberView();
    scale_ = std::clamp(scale_ * factor, 1e-6, 1e9);
    update();
}

void CadView::zoomPrevious()
{
    if (previousViews_.empty()) {
        engine_.message(tr("Nessuna vista precedente."));
        return;
    }
    std::tie(center_, scale_) = previousViews_.back();
    previousViews_.pop_back();
    update();
}

void CadView::rememberView()
{
    previousViews_.push_back({center_, scale_});
    if (previousViews_.size() > 10)
        previousViews_.erase(previousViews_.begin());
}

void CadView::updateCursor(QPointF pos)
{
    mouse_ = pos;
    const Vec2 raw = toWorld(pos);
    engine_.setPickTolerance(pixels(kPickPixels));
    snap_ = engine_.findSnap(raw, pixels(kSnapPixels));
    effective_ = snap_ ? snap_->point : engine_.wantsObjectPick() ? raw : engine_.constrain(raw);
    engine_.setCursor(effective_);
    emit cursorMoved(effective_);
}

void CadView::mousePressEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        panLast_ = pos;
        return;
    }
    if (event->button() == Qt::RightButton) {
        // Right click acts as Enter, a common AutoCAD setting.
        engine_.submit(QString());
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    if (!engine_.wantsSelection()) {
        updateCursor(pos);
        engine_.pickPoint(effective_);
        return;
    }
    // A click on a grip of the selection starts dragging it.
    if (!engine_.isActive() && !windowStart_ && engine_.startGrip(toWorld(pos), pixels(kGripPixels + 1.0))) {
        gripPress_ = pos;
        updateCursor(pos);
        return;
    }

    const bool remove = event->modifiers() & Qt::ShiftModifier;
    if (windowStart_) {
        // Second click of a click-click selection window.
        finishWindowSelection(pos, remove);
        return;
    }
    if (const auto id = engine_.pick(toWorld(pos), pixels(kPickPixels))) {
        engine_.select({*id}, remove);
        return;
    }
    windowStart_ = pos;
    windowDragged_ = false;
    update();
}

void CadView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        panning_ = false;
        return;
    }
    if (event->button() == Qt::LeftButton && windowStart_ && windowDragged_)
        finishWindowSelection(event->position(), event->modifiers() & Qt::ShiftModifier);
    // A grip dragged with the button held goes where the button is released;
    // otherwise the next click places it.
    if (event->button() == Qt::LeftButton && gripPress_) {
        const bool dragged = QLineF(*gripPress_, event->position()).length() > 4.0;
        gripPress_.reset();
        if (dragged && engine_.isActive()) {
            updateCursor(event->position());
            engine_.pickPoint(effective_);
        }
    }
}

void CadView::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    if (panning_) {
        const QPointF d = pos - panLast_;
        center_.x -= d.x() / scale_;
        center_.y += d.y() / scale_;
        panLast_ = pos;
        viewMoving();
    }
    if (windowStart_ && QLineF(*windowStart_, pos).length() > 4.0 && (event->buttons() & Qt::LeftButton))
        windowDragged_ = true;
    updateCursor(pos);
    update();
}

void CadView::mouseDoubleClickEvent(QMouseEvent* event)
{
    // Double click with the wheel: zoom extents, as in AutoCAD.
    if (event->button() == Qt::MiddleButton) {
        zoomExtents();
        return;
    }
    // Double click on a text, a dimension or a hatch edits it, as in AutoCAD.
    if (event->button() == Qt::LeftButton && (!engine_.isActive() || gripPress_)) {
        const Vec2 p = toWorld(event->position());
        if (const auto id = engine_.pick(p, pixels(kPickPixels))) {
            const Shape& shape = engine_.document().entity(*id)->shape;
            QString command;
            if (std::holds_alternative<Text>(shape) || std::holds_alternative<MText>(shape)
                || std::holds_alternative<Dimension>(shape))
                command = QStringLiteral("TEXTEDIT");
            else if (std::holds_alternative<Hatch>(shape))
                command = QStringLiteral("HATCHEDIT");
            if (!command.isEmpty()) {
                // The first click may have started dragging a grip.
                gripPress_.reset();
                if (engine_.isActive())
                    engine_.cancel();
                engine_.clearSelection();
                engine_.run(command);
                engine_.pickPoint(p);
                return;
            }
        }
    }
    mousePressEvent(event);
}

void CadView::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0)
        return;
    const QPointF pos = event->position();
    const Vec2 anchor = toWorld(pos);
    scale_ = std::clamp(scale_ * std::pow(kZoomStep, steps), 1e-6, 1e9);
    // Keep the point under the cursor still while zooming.
    center_.x = anchor.x - (pos.x() - width() / 2.0) / scale_;
    center_.y = anchor.y + (pos.y() - height() / 2.0) / scale_;
    viewMoving();
    updateCursor(pos);
    update();
}

void CadView::enterEvent(QEnterEvent* event)
{
    mouseInside_ = true;
    updateCursor(event->position());
    update();
}

void CadView::leaveEvent(QEvent*)
{
    mouseInside_ = false;
    snap_.reset();
    update();
}

void CadView::finishWindowSelection(QPointF end, bool remove)
{
    const QPointF start = *windowStart_;
    windowStart_.reset();
    windowDragged_ = false;
    // Left to right: objects fully inside. Right to left: objects touched.
    const bool crossing = end.x() < start.x();
    const BBox box = BBox::fromCorners(toWorld(start), toWorld(end));
    engine_.select(engine_.pickBox(box, crossing), remove);
    update();
}

void CadView::renderDrawing(QPainter& p)
{
    p.fillRect(rect(), kBackground);
    p.setRenderHint(QPainter::Antialiasing);

    if (grid_)
        drawGrid(p);

    const Document& doc = engine_.document();
    const BBox visible = visibleArea();
    const auto& selection = engine_.selection();
    DrawingPainter dp(p, center_, scale_, QPointF(width() / 2.0, height() / 2.0));
    dp.setVisibleArea(visible);

    if (wallsRevision_ != doc.revision()) {
        walls_ = joinedWalls(doc);
        wallsRevision_ = doc.revision();
    }
    const std::vector<EntityId> shown = doc.entitiesIn(visible);
    if (const Story* below = doc.storyBelow(doc.currentStory())) {
        p.setPen(QPen(kStoryBelow, 0));
        for (EntityId id : shown) {
            const Entity& e = *doc.entity(id);
            if (e.story != below->id || !doc.isLayerVisible(e))
                continue;
            if (const auto* w = std::get_if<Wall>(&e.shape))
                dp.drawWall(*w, walls_.at(id));
            else
                doc.forEachPart(e, [&](const Entity& part) { dp.drawShape(part.shape); });
        }
    }
    for (EntityId id : shown) {
        const Entity& e = *doc.entity(id);
        if (!doc.isVisible(e))
            continue;
        const bool selected = selection.count(id) > 0;
        doc.forEachPart(e, [&](const Entity& part) {
            if (&part != &e && !doc.isLayerVisible(part))
                return;
            QPen pen(toQColor(doc.colorOf(part)), 0);
            if (lineweights_) {
                // About 96 pixels per inch, as AutoCAD shows them: 0.25 mm is one pixel.
                const int weight = doc.lineweightOf(part);
                const double mm = (weight >= 0 ? weight : kDefaultLineweight) / 100.0;
                const double pixels = std::max(1.0, std::round(mm * 96.0 / 25.4));
                if (pixels > 1.0)
                    pen.setWidthF(pixels);
                pen.setCosmetic(true);
            }
            if (usesLinetype(part.shape))
                applyLinetype(pen, doc.linetypeOf(part), doc.linetypeScale(), scale_);
            if (selected) {
                pen = QPen(kSelected, 1.5, Qt::DashLine);
                pen.setCosmetic(true);
            }
            p.setPen(pen);
            if (const auto* w = std::get_if<Wall>(&part.shape); w && &part == &e)
                dp.drawWall(*w, walls_.at(id));
            else
                dp.drawShape(part.shape);
        });
    }
}

void CadView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const Document& doc = engine_.document();
    CacheKey key;
    key.revision = doc.revision();
    key.center = center_;
    key.scale = scale_;
    key.size = size();
    key.ratio = devicePixelRatioF();
    key.grid = grid_;
    key.lineweights = lineweights_;
    key.selection = engine_.selection();

    if (!cache_.isNull() && key.sameContent(cacheKey_) && key.sameView(cacheKey_)) {
        p.drawImage(0, 0, cache_);
    } else if (!cache_.isNull() && moving_ && renderMs_ > 25.0 && key.sameContent(cacheKey_)) {
        // The old image, moved and scaled to the new view, until the view settles.
        p.fillRect(rect(), kBackground);
        const double k = scale_ / cacheKey_.scale;
        const QPointF c(width() / 2.0, height() / 2.0);
        const QPointF shift((cacheKey_.center.x - center_.x) * scale_, -(cacheKey_.center.y - center_.y) * scale_);
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.translate(c + shift);
        p.scale(k, k);
        p.translate(-c);
        p.drawImage(0, 0, cache_);
        p.restore();
    } else {
        cache_ = QImage(size() * key.ratio, QImage::Format_ARGB32_Premultiplied);
        cache_.setDevicePixelRatio(key.ratio);
        QElapsedTimer timer;
        timer.start();
        {
            QPainter cp(&cache_);
            renderDrawing(cp);
        }
        renderMs_ = double(timer.nsecsElapsed()) / 1e6;
        cacheKey_ = key;
        p.drawImage(0, 0, cache_);
    }
    p.setRenderHint(QPainter::Antialiasing);
    DrawingPainter dp(p, center_, scale_, QPointF(width() / 2.0, height() / 2.0));
    dp.setVisibleArea(visibleArea());

    if (mouseInside_) {
        std::vector<Shape> preview;
        engine_.preview(effective_, preview);
        p.setPen(QPen(kPreview, 0));
        for (const Shape& s : preview)
            dp.drawShape(s);
    }

    if (windowStart_ && mouseInside_) {
        const bool crossing = mouse_.x() < windowStart_->x();
        const QRectF r = QRectF(*windowStart_, mouse_).normalized();
        p.fillRect(r, crossing ? QColor(60, 200, 90, 45) : QColor(60, 120, 255, 45));
        p.setPen(QPen(crossing ? QColor(60, 200, 90) : QColor(60, 120, 255), 1, crossing ? Qt::DashLine : Qt::SolidLine));
        p.drawRect(r);
    }

    drawGrips(p);
    if (snap_ && mouseInside_)
        drawSnapMarker(p, *snap_);
    if (mouseInside_)
        drawCrosshair(p);
}

void CadView::drawGrid(QPainter& p) const
{
    // Grid step: a power of ten that keeps lines at least 12 pixels apart.
    const double minStep = 12.0 / scale_;
    const double step = std::pow(10.0, std::ceil(std::log10(minStep)));
    const Vec2 a = toWorld({0, static_cast<qreal>(height())});
    const Vec2 b = toWorld(QPointF(width(), 0));
    const auto first = [&](double v) { return std::floor(v / step); };

    p.setRenderHint(QPainter::Antialiasing, false);
    for (double i = first(a.x); i * step <= b.x; ++i) {
        const bool major = std::fmod(std::abs(i), 10.0) < 0.5;
        p.setPen(major ? kGridMajor : kGridMinor);
        const double x = toScreen({i * step, 0}).x();
        p.drawLine(QPointF(x, 0), QPointF(x, height()));
    }
    for (double j = first(a.y); j * step <= b.y; ++j) {
        const bool major = std::fmod(std::abs(j), 10.0) < 0.5;
        p.setPen(major ? kGridMajor : kGridMinor);
        const double y = toScreen({0, j * step}).y();
        p.drawLine(QPointF(0, y), QPointF(width(), y));
    }

    const QPointF origin = toScreen({0, 0});
    p.setPen(kAxisX);
    p.drawLine(QPointF(0, origin.y()), QPointF(width(), origin.y()));
    p.setPen(kAxisY);
    p.drawLine(QPointF(origin.x(), 0), QPointF(origin.x(), height()));
    p.setRenderHint(QPainter::Antialiasing, true);
}

void CadView::drawGrips(QPainter& p) const
{
    const std::vector<Vec2> grips = engine_.grips();
    if (grips.empty())
        return;
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(Qt::black, 0));
    const double s = kGripPixels;
    for (Vec2 g : grips) {
        const QPointF c = toScreen(g);
        const bool hover = mouseInside_ && std::abs(c.x() - mouse_.x()) <= s + 1.0 && std::abs(c.y() - mouse_.y()) <= s + 1.0;
        p.setBrush(hover ? kGripHover : kGrip);
        p.drawRect(QRectF(c.x() - s, c.y() - s, 2 * s, 2 * s));
    }
    p.setBrush(Qt::NoBrush);
    p.setRenderHint(QPainter::Antialiasing, true);
}

void CadView::drawSnapMarker(QPainter& p, const SnapPoint& snap) const
{
    const QPointF c = toScreen(snap.point);
    constexpr double s = 6.0;
    p.setPen(QPen(kSnap, 2));
    p.setBrush(Qt::NoBrush);
    switch (snap.type) {
    case SnapEndpoint:
        p.drawRect(QRectF(c.x() - s, c.y() - s, 2 * s, 2 * s));
        break;
    case SnapMidpoint:
        p.drawPolygon(QPolygonF({{c.x() - s, c.y() + s}, {c.x() + s, c.y() + s}, {c.x(), c.y() - s}}));
        break;
    case SnapCenter:
        p.drawEllipse(c, s, s);
        break;
    case SnapQuadrant:
        p.drawPolygon(QPolygonF({{c.x() - s, c.y()}, {c.x(), c.y() - s}, {c.x() + s, c.y()}, {c.x(), c.y() + s}}));
        break;
    case SnapIntersection:
        p.drawLine(QPointF(c.x() - s, c.y() - s), QPointF(c.x() + s, c.y() + s));
        p.drawLine(QPointF(c.x() - s, c.y() + s), QPointF(c.x() + s, c.y() - s));
        break;
    case SnapNearest:
        p.drawPolygon(QPolygonF({{c.x() - s, c.y() - s}, {c.x() + s, c.y() - s}, {c.x() - s, c.y() + s}, {c.x() + s, c.y() + s}}));
        break;
    case SnapInsertion:
        // Two overlapping squares, as in AutoCAD.
        p.drawRect(QRectF(c.x() - s, c.y() - s, s * 1.3, s * 1.3));
        p.drawRect(QRectF(c.x() - s * 0.3, c.y() - s * 0.3, s * 1.3, s * 1.3));
        break;
    case SnapNode:
        // A circle with a cross, as in AutoCAD.
        p.drawEllipse(c, s, s);
        p.drawLine(QPointF(c.x() - s, c.y() - s), QPointF(c.x() + s, c.y() + s));
        p.drawLine(QPointF(c.x() - s, c.y() + s), QPointF(c.x() + s, c.y() - s));
        break;
    }
    p.setPen(kSnap);
    p.drawText(c + QPointF(s + 6, s + 14), snapLabel(snap.type));
}

void CadView::drawCrosshair(QPainter& p) const
{
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(kCrosshair, 0));
    const QPointF m = mouse_;
    p.drawLine(QPointF(0, m.y()), QPointF(width(), m.y()));
    p.drawLine(QPointF(m.x(), 0), QPointF(m.x(), height()));
    if (engine_.wantsSelection() || engine_.wantsObjectPick()) {
        // Pick box, shown when a click selects objects.
        const double s = kPickPixels;
        p.drawRect(QRectF(m.x() - s, m.y() - s, 2 * s, 2 * s));
    }
    p.setRenderHint(QPainter::Antialiasing, true);
}
