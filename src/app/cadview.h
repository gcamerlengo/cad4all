// SPDX-License-Identifier: GPL-3.0-or-later
// The drawing area: shows the document, the previews of the active command,
// object snap markers and the crosshair; turns mouse input into points,
// selections, zoom and pan.
#pragma once

#include <vector>

#include "building.h"
#include "commandengine.h"

#include <QImage>
#include <QTimer>
#include <QWidget>

#include <map>
#include <optional>
#include <set>

class CadView : public QWidget {
    Q_OBJECT

public:
    explicit CadView(cad::CommandEngine& engine, QWidget* parent = nullptr);

    void zoomExtents();
    // The area `box` fills the view, with a 5% margin.
    void zoomWindow(const cad::BBox& box);
    // Magnification times `factor`, about the centre of the view.
    void zoomFactor(double factor);
    // Back to the view before the last zoom command.
    void zoomPrevious();
    bool gridVisible() const { return grid_; }
    void setGridVisible(bool on);
    // AutoCAD's LWDISPLAY: lines as thick as their lineweight, or all thin.
    bool lineweightsVisible() const { return lineweights_; }
    void setLineweightsVisible(bool on);

    cad::Vec2 toWorld(QPointF screen) const;
    // The part of the drawing shown.
    cad::BBox visibleArea() const;
    QPointF toScreen(cad::Vec2 world) const;

signals:
    void cursorMoved(cad::Vec2 world);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateCursor(QPointF pos);
    void finishWindowSelection(QPointF end, bool remove);
    double pixels(double px) const { return px / scale_; }

    // The drawing (grid and objects) for the current view, kept in an image
    // so the cursor, previews and snap markers redraw without it.
    void renderDrawing(QPainter& p);
    // While the view moves (wheel, middle-button pan) a slow drawing is
    // shown by moving and scaling the last image; it is redrawn once the
    // view has been still for a moment.
    void viewMoving();
    void drawGrid(QPainter& p) const;
    void drawGrips(QPainter& p) const;
    void drawSnapMarker(QPainter& p, const cad::SnapPoint& snap) const;
    void drawCrosshair(QPainter& p) const;

    cad::CommandEngine& engine_;

    // View: world point at the widget centre and pixels per drawing unit.
    cad::Vec2 center_{0.0, 0.0};
    double scale_ = 20.0;
    // Views before each zoom command, for ZOOM Precedente.
    std::vector<std::pair<cad::Vec2, double>> previousViews_;
    void rememberView();
    bool grid_ = true;
    bool lineweights_ = false;

    QPointF mouse_;
    bool mouseInside_ = false;
    bool panning_ = false;
    QPointF panLast_;
    std::optional<QPointF> windowStart_;
    bool windowDragged_ = false;
    // Where a grip was pressed, while the button is held.
    std::optional<QPointF> gripPress_;

    struct CacheKey {
        std::uint64_t revision = ~std::uint64_t(0);
        cad::Vec2 center;
        double scale = 0.0;
        QSize size;
        qreal ratio = 0.0;
        bool grid = false;
        bool lineweights = false;
        std::set<cad::EntityId> selection;
        bool sameView(const CacheKey& o) const { return center == o.center && scale == o.scale; }
        bool sameContent(const CacheKey& o) const
        {
            return revision == o.revision && size == o.size && ratio == o.ratio && grid == o.grid && lineweights == o.lineweights
                && selection == o.selection;
        }
    };
    QImage cache_;
    CacheKey cacheKey_;
    double renderMs_ = 0.0;  // how long the last full drawing took
    bool moving_ = false;
    QTimer settle_;
    std::uint64_t wallsRevision_ = ~std::uint64_t(0);
    std::map<cad::EntityId, cad::WallFaces> walls_;

    // Point the next click will give to the command (after snap and ortho).
    cad::Vec2 effective_;
    std::optional<cad::SnapPoint> snap_;
};
