// SPDX-License-Identifier: GPL-3.0-or-later
#include "cadview.h"

#include "drawingpainter.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

using namespace cad;

namespace {

const QColor kBackground(33, 40, 48);
const QColor kGridMinor(42, 50, 60);
const QColor kGridMajor(54, 63, 75);
const QColor kAxisX(150, 60, 60);
const QColor kAxisY(60, 140, 70);
const QColor kPreview(120, 190, 255);
const QColor kSelected(78, 161, 255);
const QColor kSnap(240, 195, 48);
const QColor kCrosshair(220, 220, 220);

constexpr double kPickPixels = 5.0;
constexpr double kSnapPixels = 10.0;
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

void CadView::zoomExtents()
{
    const BBox box = engine_.document().extents();
    if (!box.valid()) {
        center_ = {0.0, 0.0};
        scale_ = 20.0;
    } else {
        center_ = box.center();
        const double w = std::max(box.width(), 1e-6);
        const double h = std::max(box.height(), 1e-6);
        // Leave a 5% margin around the drawing.
        scale_ = 0.9 * std::min(width() / w, height() / h);
        scale_ = std::clamp(scale_, 1e-6, 1e9);
    }
    update();
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
}

void CadView::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    if (panning_) {
        const QPointF d = pos - panLast_;
        center_.x -= d.x() / scale_;
        center_.y += d.y() / scale_;
        panLast_ = pos;
    }
    if (windowStart_ && QLineF(*windowStart_, pos).length() > 4.0 && (event->buttons() & Qt::LeftButton))
        windowDragged_ = true;
    updateCursor(pos);
    update();
}

void CadView::mouseDoubleClickEvent(QMouseEvent* event)
{
    // Double click with the wheel: zoom extents, as in AutoCAD.
    if (event->button() == Qt::MiddleButton)
        zoomExtents();
    else
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

void CadView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), kBackground);
    p.setRenderHint(QPainter::Antialiasing);

    if (grid_)
        drawGrid(p);

    const Document& doc = engine_.document();
    const BBox visible = BBox::fromCorners(toWorld({0, 0}), toWorld(QPointF(width(), height())));
    const auto& selection = engine_.selection();
    DrawingPainter dp(p, center_, scale_, QPointF(width() / 2.0, height() / 2.0));
    dp.setVisibleArea(visible);

    for (const auto& [id, e] : doc.entities()) {
        if (!doc.isVisible(e) || !boundingBox(e.shape).intersects(visible))
            continue;
        const Layer* layer = doc.layer(e.layer);
        QPen pen(layer ? toQColor(layer->color) : Qt::white, 0);
        if (selection.count(id)) {
            pen = QPen(kSelected, 1.5, Qt::DashLine);
            pen.setCosmetic(true);
        }
        p.setPen(pen);
        dp.drawShape(e.shape);
    }

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
