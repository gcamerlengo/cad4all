// SPDX-License-Identifier: GPL-3.0-or-later
#include "icons.h"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

#include <functional>

namespace icons {

namespace {

using Painter = std::function<void(QPainter&)>;

// Paints on a 24x24 canvas scaled to the requested size.
class CodeIconEngine : public QIconEngine {
public:
    explicit CodeIconEngine(Painter painter) : painter_(std::move(painter)) {}

    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->translate(rect.topLeft());
        p->scale(rect.width() / 24.0, rect.height() / 24.0);
        const QPalette pal = QApplication::palette();
        QColor ink = pal.color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::WindowText);
        p->setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        painter_(*p);
        p->restore();
    }

    QIconEngine* clone() const override { return new CodeIconEngine(painter_); }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

private:
    Painter painter_;
};

const QColor kAccent(52, 140, 230);

void accentPen(QPainter& p)
{
    QPen pen = p.pen();
    pen.setColor(kAccent);
    p.setPen(pen);
}

void dot(QPainter& p, QPointF c)
{
    p.save();
    p.setBrush(p.pen().color());
    p.drawEllipse(c, 1.6, 1.6);
    p.restore();
}

void square(QPainter& p, QPointF origin)
{
    p.drawRect(QRectF(origin, QSizeF(8, 8)));
}

} // namespace

QIcon make(const QString& name)
{
    Painter painter;
    if (name == QLatin1String("line")) {
        painter = [](QPainter& p) {
            p.drawLine(QPointF(4, 20), QPointF(20, 4));
            dot(p, {4, 20});
            dot(p, {20, 4});
        };
    } else if (name == QLatin1String("polyline")) {
        painter = [](QPainter& p) {
            p.drawPolyline(QPolygonF({{3, 19}, {9, 7}, {15, 15}, {21, 5}}));
        };
    } else if (name == QLatin1String("circle")) {
        painter = [](QPainter& p) {
            p.drawEllipse(QPointF(12, 12), 8.5, 8.5);
            dot(p, {12, 12});
        };
    } else if (name == QLatin1String("arc")) {
        painter = [](QPainter& p) {
            QPainterPath path;
            path.arcMoveTo(QRectF(3, 5, 18, 18), 0);
            path.arcTo(QRectF(3, 5, 18, 18), 0, 180);
            p.drawPath(path);
        };
    } else if (name == QLatin1String("rectangle")) {
        painter = [](QPainter& p) { p.drawRect(QRectF(3.5, 6.5, 17, 11)); };
    } else if (name == QLatin1String("hatch")) {
        painter = [](QPainter& p) {
            p.drawRect(QRectF(3.5, 5.5, 17, 13));
            accentPen(p);
            QPen pen = p.pen();
            pen.setWidthF(1.1);
            p.setPen(pen);
            p.setClipRect(QRectF(4.5, 6.5, 15, 11));
            for (double x = -8; x < 22; x += 4)
                p.drawLine(QPointF(x, 18), QPointF(x + 12, 6));
        };
    } else if (name == QLatin1String("erase")) {
        painter = [](QPainter& p) {
            p.drawLine(QPointF(4, 18), QPointF(16, 6));
            accentPen(p);
            p.drawLine(QPointF(12, 14), QPointF(20, 22));
            p.drawLine(QPointF(20, 14), QPointF(12, 22));
        };
    } else if (name == QLatin1String("move")) {
        painter = [](QPainter& p) {
            p.drawLine(QPointF(12, 3), QPointF(12, 21));
            p.drawLine(QPointF(3, 12), QPointF(21, 12));
            p.drawPolyline(QPolygonF({{9, 6}, {12, 3}, {15, 6}}));
            p.drawPolyline(QPolygonF({{9, 18}, {12, 21}, {15, 18}}));
            p.drawPolyline(QPolygonF({{6, 9}, {3, 12}, {6, 15}}));
            p.drawPolyline(QPolygonF({{18, 9}, {21, 12}, {18, 15}}));
        };
    } else if (name == QLatin1String("copy")) {
        painter = [](QPainter& p) {
            square(p, {4, 4});
            accentPen(p);
            square(p, {12, 12});
        };
    } else if (name == QLatin1String("rotate")) {
        painter = [](QPainter& p) {
            QPainterPath path;
            path.arcMoveTo(QRectF(4, 4, 16, 16), 90);
            path.arcTo(QRectF(4, 4, 16, 16), 90, 270);
            p.drawPath(path);
            p.drawPolyline(QPolygonF({{9, 1}, {12, 4}, {9, 7}}));
        };
    } else if (name == QLatin1String("scale")) {
        painter = [](QPainter& p) {
            p.drawRect(QRectF(3, 11, 10, 10));
            accentPen(p);
            p.drawRect(QRectF(3, 3, 18, 18));
        };
    } else if (name == QLatin1String("mirror")) {
        painter = [](QPainter& p) {
            p.drawPolygon(QPolygonF({{3, 19}, {10, 19}, {10, 7}}));
            accentPen(p);
            p.drawPolygon(QPolygonF({{21, 19}, {14, 19}, {14, 7}}));
            QPen dashed = p.pen();
            dashed.setStyle(Qt::DashLine);
            dashed.setWidthF(1.0);
            p.setPen(dashed);
            p.drawLine(QPointF(12, 2), QPointF(12, 22));
        };
    } else if (name == QLatin1String("offset")) {
        painter = [](QPainter& p) {
            p.drawPolyline(QPolygonF({{3, 21}, {3, 9}, {15, 9}}));
            accentPen(p);
            p.drawPolyline(QPolygonF({{9, 21}, {9, 15}, {21, 15}}));
        };
    } else if (name == QLatin1String("trim")) {
        painter = [](QPainter& p) {
            p.drawLine(QPointF(12, 3), QPointF(12, 21));
            p.drawLine(QPointF(3, 12), QPointF(12, 12));
            accentPen(p);
            QPen dashed = p.pen();
            dashed.setStyle(Qt::DotLine);
            p.setPen(dashed);
            p.drawLine(QPointF(12, 12), QPointF(21, 12));
        };
    } else if (name == QLatin1String("extend")) {
        painter = [](QPainter& p) {
            p.drawLine(QPointF(20, 3), QPointF(20, 21));
            p.drawLine(QPointF(3, 12), QPointF(10, 12));
            accentPen(p);
            p.drawLine(QPointF(10, 12), QPointF(20, 12));
            p.drawPolyline(QPolygonF({{15, 9}, {18, 12}, {15, 15}}));
        };
    } else if (name == QLatin1String("fillet")) {
        painter = [](QPainter& p) {
            p.drawLine(QPointF(3, 4), QPointF(3, 13));
            p.drawLine(QPointF(12, 21), QPointF(21, 21));
            accentPen(p);
            QPainterPath path;
            path.moveTo(3, 13);
            path.arcTo(QRectF(3, 5, 18, 16), 180, 90);
            p.drawPath(path);
        };
    } else if (name == QLatin1String("chamfer")) {
        painter = [](QPainter& p) {
            p.drawLine(QPointF(3, 4), QPointF(3, 14));
            p.drawLine(QPointF(10, 21), QPointF(21, 21));
            accentPen(p);
            p.drawLine(QPointF(3, 14), QPointF(10, 21));
        };
    } else if (name == QLatin1String("stretch")) {
        painter = [](QPainter& p) {
            p.drawPolyline(QPolygonF({{3, 20}, {3, 8}, {9, 8}}));
            p.drawLine(QPointF(3, 20), QPointF(9, 20));
            accentPen(p);
            p.drawPolyline(QPolygonF({{9, 8}, {20, 8}, {20, 20}, {9, 20}}));
            QPen dashed = p.pen();
            dashed.setStyle(Qt::DashLine);
            dashed.setWidthF(1.0);
            p.setPen(dashed);
            p.drawRect(QRectF(13, 4, 10, 19));
        };
    } else if (name == QLatin1String("text")) {
        painter = [](QPainter& p) {
            p.drawLine(QPointF(4, 4), QPointF(20, 4));
            p.drawLine(QPointF(12, 4), QPointF(12, 20));
            p.drawLine(QPointF(4, 4), QPointF(4, 7));
            p.drawLine(QPointF(20, 4), QPointF(20, 7));
            accentPen(p);
            p.drawLine(QPointF(9, 20), QPointF(15, 20));
        };
    } else if (name == QLatin1String("dimLinear")) {
        painter = [](QPainter& p) {
            p.drawLine(QPointF(3, 21), QPointF(3, 8));
            p.drawLine(QPointF(21, 21), QPointF(21, 8));
            accentPen(p);
            p.drawLine(QPointF(3, 11), QPointF(21, 11));
            p.drawPolyline(QPolygonF({{7, 9}, {3, 11}, {7, 13}}));
            p.drawPolyline(QPolygonF({{17, 9}, {21, 11}, {17, 13}}));
            p.drawLine(QPointF(9, 6), QPointF(15, 6));
        };
    } else if (name == QLatin1String("dimAligned")) {
        painter = [](QPainter& p) {
            p.drawLine(QPointF(4, 21), QPointF(20, 5));
            accentPen(p);
            p.drawLine(QPointF(4, 21), QPointF(1, 18));
            p.drawLine(QPointF(20, 5), QPointF(17, 2));
            p.drawLine(QPointF(2, 19), QPointF(18, 3));
        };
    } else if (name == QLatin1String("dimRadius")) {
        painter = [](QPainter& p) {
            p.drawEllipse(QPointF(11, 13), 8, 8);
            dot(p, {11, 13});
            accentPen(p);
            p.drawLine(QPointF(11, 13), QPointF(22, 2));
            p.drawPolyline(QPolygonF({{14.5, 7.5}, {16.7, 7.3}, {16.5, 9.5}}));
        };
    } else if (name == QLatin1String("dimDiameter")) {
        painter = [](QPainter& p) {
            p.drawEllipse(QPointF(12, 12), 8.5, 8.5);
            accentPen(p);
            p.drawLine(QPointF(6, 18), QPointF(18, 6));
            p.drawPolyline(QPolygonF({{15.5, 6}, {18, 6}, {18, 8.5}}));
            p.drawPolyline(QPolygonF({{6, 15.5}, {6, 18}, {8.5, 18}}));
        };
    } else if (name == QLatin1String("zoomExtents")) {
        painter = [](QPainter& p) {
            p.drawEllipse(QPointF(10, 10), 6, 6);
            p.drawLine(QPointF(14.5, 14.5), QPointF(21, 21));
            p.drawPolyline(QPolygonF({{2, 6}, {2, 2}, {6, 2}}));
            p.drawPolyline(QPolygonF({{18, 2}, {22, 2}, {22, 6}}));
            p.drawPolyline(QPolygonF({{2, 18}, {2, 22}, {6, 22}}));
        };
    } else {
        return {};
    }
    return QIcon(new CodeIconEngine(std::move(painter)));
}

} // namespace icons
