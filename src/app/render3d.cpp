// SPDX-License-Identifier: GPL-3.0-or-later
#include "render3d.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace cad;

Vec3 Camera3D::eye() const
{
    const Vec3 dir{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    return target + dir * distance;
}

Vec3 Camera3D::forward() const { return normalized(target - eye()); }

Vec3 Camera3D::right() const
{
    // Looking straight down the cross product vanishes: use the yaw instead.
    const Vec3 r = cross(forward(), Vec3{0, 0, 1});
    if (length(r) < 1e-9)
        return {-std::sin(yaw), std::cos(yaw), 0.0};
    return normalized(r);
}

Vec3 Camera3D::up() const { return cross(right(), forward()); }

Camera3D fitCamera(const Model3D& model, Camera3D camera, double aspect)
{
    if (model.empty())
        return camera;
    const auto [lo, hi] = model.bounds();
    camera.target = (lo + hi) * 0.5;
    const double radius = std::max(length(hi - lo) / 2.0, 1e-6);
    const double vertical = camera.fov / 2.0;
    const double horizontal = std::atan(std::tan(vertical) * std::max(aspect, 1e-3));
    camera.distance = radius / std::sin(std::min(vertical, horizontal)) * 1.02;
    return camera;
}

namespace {

struct ViewPoint {
    double x, y, z;
};

struct ScreenPoint {
    double x, y, iz;  // iz = 1 / depth, linear on screen
};

class Rasterizer {
public:
    Rasterizer(const Camera3D& cam, QSize size)
        : w_(size.width()), h_(size.height()), depth_(static_cast<size_t>(w_) * static_cast<size_t>(h_), 0.0f)
    {
        eye_ = cam.eye();
        right_ = cam.right();
        up_ = cam.up();
        forward_ = cam.forward();
        focal_ = h_ / 2.0 / std::tan(cam.fov / 2.0);
        near_ = std::max(cam.distance * 1e-3, 1e-6);
    }

    ViewPoint toView(Vec3 p) const
    {
        const Vec3 d = p - eye_;
        return {dot(d, right_), dot(d, up_), dot(d, forward_)};
    }

    ScreenPoint project(ViewPoint v) const
    {
        return {w_ / 2.0 + v.x * focal_ / v.z, h_ / 2.0 - v.y * focal_ / v.z, 1.0 / v.z};
    }

    // The part of a polygon in front of the near plane.
    std::vector<ViewPoint> clipNear(const std::vector<ViewPoint>& in) const
    {
        std::vector<ViewPoint> out;
        for (size_t k = 0; k < in.size(); ++k) {
            const ViewPoint& a = in[k];
            const ViewPoint& b = in[(k + 1) % in.size()];
            const bool ia = a.z >= near_;
            const bool ib = b.z >= near_;
            if (ia)
                out.push_back(a);
            if (ia != ib) {
                const double t = (near_ - a.z) / (b.z - a.z);
                out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, near_});
            }
        }
        return out;
    }

    // `alpha` < 1 blends without writing the depth (glass).
    void triangle(QImage& img, std::array<Vec3, 3> tri, QRgb color, double alpha)
    {
        std::vector<ViewPoint> poly{toView(tri[0]), toView(tri[1]), toView(tri[2])};
        poly = clipNear(poly);
        for (size_t k = 1; k + 1 < poly.size(); ++k)
            fill(img, project(poly[0]), project(poly[k]), project(poly[k + 1]), color, alpha);
    }

    void line(QImage& img, Vec3 a, Vec3 b, QRgb color, double width)
    {
        ViewPoint va = toView(a);
        ViewPoint vb = toView(b);
        if (va.z < near_ && vb.z < near_)
            return;
        auto cut = [&](ViewPoint& p, const ViewPoint& q) {
            const double t = (near_ - p.z) / (q.z - p.z);
            p = {p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, near_};
        };
        if (va.z < near_)
            cut(va, vb);
        else if (vb.z < near_)
            cut(vb, va);
        const ScreenPoint s0 = project(va);
        const ScreenPoint s1 = project(vb);
        const double len = std::max(std::abs(s1.x - s0.x), std::abs(s1.y - s0.y));
        if (len > 1e5)
            return;  // far outside the image
        const int steps = std::max(1, static_cast<int>(std::ceil(len)));
        const int r0 = -static_cast<int>(std::floor((width - 1.0) / 2.0));
        const int r1 = r0 + std::max(1, static_cast<int>(std::lround(width))) - 1;
        auto* bits = reinterpret_cast<QRgb*>(img.bits());
        for (int i = 0; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            const int x = static_cast<int>(std::floor(s0.x + (s1.x - s0.x) * t));
            const int y = static_cast<int>(std::floor(s0.y + (s1.y - s0.y) * t));
            const double iz = s0.iz + (s1.iz - s0.iz) * t;
            for (int dy = r0; dy <= r1; ++dy) {
                for (int dx = r0; dx <= r1; ++dx) {
                    const int px = x + dx;
                    const int py = y + dy;
                    if (px < 0 || py < 0 || px >= w_ || py >= h_)
                        continue;
                    const size_t idx = static_cast<size_t>(py) * static_cast<size_t>(w_) + static_cast<size_t>(px);
                    // Edges lie on their faces: a small tolerance keeps them
                    // in front of the face they belong to.
                    if (iz >= depth_[idx] * (1.0 - 0.006))
                        bits[idx] = color;
                }
            }
        }
    }

private:
    void fill(QImage& img, ScreenPoint a, ScreenPoint b, ScreenPoint c, QRgb color, double alpha)
    {
        const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::abs(area) < 1e-12)
            return;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
        const int x1 = std::min(w_ - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
        const int y1 = std::min(h_ - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
        auto* bits = reinterpret_cast<QRgb*>(img.bits());
        const double inv = 1.0 / area;
        for (int y = y0; y <= y1; ++y) {
            const double py = y + 0.5;
            for (int x = x0; x <= x1; ++x) {
                const double px = x + 0.5;
                const double wa = ((c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.x)) * inv;
                const double wb = ((a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x)) * inv;
                const double wc = 1.0 - wa - wb;
                if (wa < 0.0 || wb < 0.0 || wc < 0.0)
                    continue;
                const float iz = static_cast<float>(wa * a.iz + wb * b.iz + wc * c.iz);
                const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x);
                if (iz <= depth_[idx])
                    continue;
                if (alpha >= 1.0) {
                    depth_[idx] = iz;
                    bits[idx] = color;
                } else {
                    const QRgb d = bits[idx];
                    auto mix = [&](int s, int t) { return static_cast<int>(t * alpha + s * (1.0 - alpha)); };
                    bits[idx] = qRgb(mix(qRed(d), qRed(color)), mix(qGreen(d), qGreen(color)), mix(qBlue(d), qBlue(color)));
                }
            }
        }
    }

    int w_, h_;
    std::vector<float> depth_;
    Vec3 eye_, right_, up_, forward_;
    double focal_ = 1.0;
    double near_ = 1.0;
};

QRgb shaded(const QColor& base, double light)
{
    light = std::clamp(light, 0.0, 1.0);
    return qRgb(static_cast<int>(base.red() * light), static_cast<int>(base.green() * light),
                static_cast<int>(base.blue() * light));
}

} // namespace

QImage renderModel(const Model3D& model, const Camera3D& camera, QSize size, double lineWidth, const RenderStyle& style)
{
    QImage img(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y) {
        const double t = size.height() > 1 ? static_cast<double>(y) / (size.height() - 1) : 0.0;
        const QRgb c = qRgb(static_cast<int>(style.skyTop.red() + (style.skyBottom.red() - style.skyTop.red()) * t),
                            static_cast<int>(style.skyTop.green() + (style.skyBottom.green() - style.skyTop.green()) * t),
                            static_cast<int>(style.skyTop.blue() + (style.skyBottom.blue() - style.skyTop.blue()) * t));
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        std::fill(line, line + size.width(), c);
    }
    if (model.empty() || size.isEmpty())
        return img;

    Rasterizer r(camera, size);
    const Vec3 sun = normalized(Vec3{0.35, -0.6, 0.75});
    const Vec3 view = camera.forward();
    auto light = [&](const Model3D::Triangle& t) {
        const Vec3 n = normalized(cross(t.v[1] - t.v[0], t.v[2] - t.v[0]));
        // Two-sided: the faces do not need a consistent winding.
        return 0.32 + 0.43 * std::abs(dot(n, sun)) + 0.25 * std::abs(dot(n, view));
    };
    // Ground under the building, just below its base so it never covers it.
    const auto [lo, hi] = model.bounds();
    const double margin = std::max(hi.x - lo.x, hi.y - lo.y);
    const double gz = lo.z - margin * 1e-3;
    const Vec3 g0{lo.x - margin, lo.y - margin, gz};
    const Vec3 g1{hi.x + margin, lo.y - margin, gz};
    const Vec3 g2{hi.x + margin, hi.y + margin, gz};
    const Vec3 g3{lo.x - margin, hi.y + margin, gz};
    const QRgb ground = style.ground.rgb();
    r.triangle(img, {g0, g1, g2}, ground, 1.0);
    r.triangle(img, {g0, g2, g3}, ground, 1.0);

    for (const auto& t : model.triangles) {
        if (t.material == Model3D::Material::Glass)
            continue;
        const QColor& base = t.material == Model3D::Material::Wood ? style.wood : style.wall;
        r.triangle(img, t.v, shaded(base, light(t)), 1.0);
    }
    for (const auto& e : model.edges)
        r.line(img, e[0], e[1], style.edge.rgb(), lineWidth);
    for (const auto& t : model.triangles)
        if (t.material == Model3D::Material::Glass)
            r.triangle(img, t.v, shaded(style.glass, 0.6 + 0.4 * light(t)), 0.45);
    return img;
}
