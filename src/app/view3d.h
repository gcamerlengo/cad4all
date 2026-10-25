// SPDX-License-Identifier: GPL-3.0-or-later
// The live 3D view of the building: rebuilt every time the drawing changes,
// turned with the mouse like an orbit.
#pragma once

#include "render3d.h"

#include <QPoint>
#include <QWidget>

namespace cad {
class Document;
}

class View3D : public QWidget {
    Q_OBJECT

public:
    explicit View3D(const cad::Document& doc, QWidget* parent = nullptr);

    // Rebuilds the model from the drawing; the camera stays where it is,
    // except while nobody has moved it since it last framed the whole model.
    void refresh();
    void zoomExtents();
    bool hasModel() const { return !model_.empty(); }
    const Camera3D& camera() const { return camera_; }
    // The whole model in an image of `size` pixels, from the current direction.
    QImage image(QSize size) const;

    QSize sizeHint() const override { return {420, 360}; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    const cad::Document& doc_;
    cad::Model3D model_;
    Camera3D camera_;
    bool fitted_ = true;  // the camera frames the whole model: it follows changes and resizing
    Qt::MouseButton dragging_ = Qt::NoButton;
    QPoint last_;
};
