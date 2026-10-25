// SPDX-License-Identifier: GPL-3.0-or-later
// Draws the 3D model into an image: a small software renderer with a depth
// buffer, flat shading and the visible edges in dark lines. It needs no
// graphics driver, so it looks the same on every system and also works
// without a display (screenshots, tests).
#pragma once

#include "building.h"

#include <QColor>
#include <QImage>

// Orbit camera: looks at `target` from `distance` away, `yaw` around the
// vertical axis and `pitch` above the horizon (radians).
struct Camera3D {
    cad::Vec3 target;
    double yaw = cad::degToRad(-60.0);
    double pitch = cad::degToRad(30.0);
    double distance = 10000.0;
    double fov = cad::degToRad(40.0);

    cad::Vec3 eye() const;
    // Unit vectors of the view: to the right, up on screen, into the screen.
    cad::Vec3 right() const;
    cad::Vec3 up() const;
    cad::Vec3 forward() const;
};

struct RenderStyle {
    QColor skyTop{224, 232, 240};
    QColor skyBottom{250, 251, 252};
    QColor wall{236, 233, 226};
    QColor glass{150, 195, 225};
    QColor wood{168, 122, 78};
    QColor roof{176, 96, 70};
    QColor edge{55, 58, 64};
    QColor ground{214, 216, 208};
};

// The camera keeps its direction and frames the whole model.
Camera3D fitCamera(const cad::Model3D& model, Camera3D camera, double aspect);

// The model seen from `camera`, `size` in pixels. `lineWidth` is the edge
// thickness in pixels.
QImage renderModel(const cad::Model3D& model, const Camera3D& camera, QSize size, double lineWidth = 1.0,
                   const RenderStyle& style = {});
