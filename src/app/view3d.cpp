// SPDX-License-Identifier: GPL-3.0-or-later
#include "view3d.h"

#include "document.h"
#include "theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace cad;

View3D::View3D(const Document& doc, QWidget* parent) : QWidget(parent), doc_(doc)
{
    setMinimumSize(160, 120);
    setFocusPolicy(Qt::ClickFocus);
    setToolTip(tr("Trascinare per ruotare, tasto destro o centrale per spostare, rotella per lo zoom, "
                  "doppio clic per vedere tutto."));
    refresh();
}

void View3D::refresh()
{
    const bool wasEmpty = model_.empty();
    model_ = buildingModel(doc_);
    if (!model_.empty() && (wasEmpty || fitted_))
        zoomExtents();
    update();
}

void View3D::zoomExtents()
{
    camera_ = fitCamera(model_, camera_, height() > 0 ? static_cast<double>(width()) / height() : 1.0);
    fitted_ = true;
    update();
}

void View3D::resizeEvent(QResizeEvent*)
{
    if (fitted_)
        zoomExtents();
}

QImage View3D::image(QSize size) const
{
    const Camera3D cam = fitCamera(model_, camera_, static_cast<double>(size.width()) / std::max(1, size.height()));
    return renderModel(model_, cam, size * 2, 2.0).scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void View3D::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (model_.empty()) {
        p.fillRect(rect(), palette().color(QPalette::Base));
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap,
                   tr("Nessun muro nel disegno.\nDisegnare i muri con il comando MURO: qui compaiono in 3D."));
        return;
    }
    // Drawn at twice the resolution and scaled down, which smooths the
    // edges on ordinary screens; high density screens are sharp enough.
    const double dpr = devicePixelRatioF();
    const int super = dpr >= 1.5 ? 1 : 2;
    const QSize device(static_cast<int>(width() * dpr), static_cast<int>(height() * dpr));
    RenderStyle style;
    if (theme::current() == theme::Theme::Dark) {
        style.skyTop = QColor(38, 44, 52);
        style.skyBottom = QColor(70, 79, 91);
        style.ground = QColor(92, 98, 104);
    }
    QImage img = renderModel(model_, camera_, device * super, super * std::max(1.0, dpr * 0.75), style);
    if (super > 1)
        img = img.scaled(device, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    img.setDevicePixelRatio(dpr);
    p.drawImage(QPointF(0, 0), img);
}

void View3D::mousePressEvent(QMouseEvent* event)
{
    dragging_ = event->button();
    last_ = event->position().toPoint();
}

void View3D::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging_ == Qt::NoButton)
        return;
    const QPoint pos = event->position().toPoint();
    const QPoint d = pos - last_;
    last_ = pos;
    fitted_ = false;
    if (dragging_ == Qt::LeftButton) {
        camera_.yaw -= d.x() * 0.01;
        camera_.pitch = std::clamp(camera_.pitch + d.y() * 0.01, degToRad(-89.0), degToRad(89.0));
    } else {
        // Pan: the target follows the cursor at its own depth.
        const double perPixel = 2.0 * camera_.distance * std::tan(camera_.fov / 2.0) / std::max(1, height());
        camera_.target = camera_.target - camera_.right() * (d.x() * perPixel) + camera_.up() * (d.y() * perPixel);
    }
    update();
}

void View3D::mouseReleaseEvent(QMouseEvent*) { dragging_ = Qt::NoButton; }

void View3D::mouseDoubleClickEvent(QMouseEvent*) { zoomExtents(); }

void View3D::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    camera_.distance *= std::pow(0.85, steps);
    fitted_ = false;
    camera_.distance = std::max(camera_.distance, 1e-3);
    update();
}
