// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry.h"

#include <algorithm>

namespace cad {

double normalizeAngle(double a)
{
    a = std::fmod(a, kTwoPi);
    if (a < 0.0)
        a += kTwoPi;
    // fmod can return exactly 2*pi after the correction above for tiny negatives.
    return a >= kTwoPi ? 0.0 : a;
}

double sweepAngle(double start, double end)
{
    const double sweep = normalizeAngle(end - start);
    return sweep < kEps ? kTwoPi : sweep;
}

bool angleInSweep(double a, double start, double end, double eps)
{
    const double rel = normalizeAngle(a - start);
    const double sweep = sweepAngle(start, end);
    return rel <= sweep + eps || rel >= kTwoPi - eps;
}

void BBox::add(Vec2 p)
{
    min.x = std::min(min.x, p.x);
    min.y = std::min(min.y, p.y);
    max.x = std::max(max.x, p.x);
    max.y = std::max(max.y, p.y);
}

void BBox::add(const BBox& b)
{
    if (!b.valid())
        return;
    add(b.min);
    add(b.max);
}

BBox BBox::inflated(double d) const
{
    BBox r = *this;
    r.min -= Vec2{d, d};
    r.max += Vec2{d, d};
    return r;
}

bool BBox::intersects(const BBox& o) const
{
    return valid() && o.valid() && min.x <= o.max.x && o.min.x <= max.x && min.y <= o.max.y && o.min.y <= max.y;
}

bool BBox::contains(const BBox& o) const
{
    return valid() && o.valid() && o.min.x >= min.x && o.max.x <= max.x && o.min.y >= min.y && o.max.y <= max.y;
}

bool BBox::contains(Vec2 p) const
{
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
}

BBox BBox::fromCorners(Vec2 a, Vec2 b)
{
    BBox r;
    r.add(a);
    r.add(b);
    return r;
}

Transform Transform::operator*(const Transform& f) const
{
    Transform r;
    r.a = a * f.a + c * f.b;
    r.b = b * f.a + d * f.b;
    r.c = a * f.c + c * f.d;
    r.d = b * f.c + d * f.d;
    r.tx = a * f.tx + c * f.ty + tx;
    r.ty = b * f.tx + d * f.ty + ty;
    return r;
}

Transform Transform::translation(Vec2 delta)
{
    Transform t;
    t.tx = delta.x;
    t.ty = delta.y;
    return t;
}

Transform Transform::rotation(Vec2 center, double angle)
{
    const double cs = std::cos(angle);
    const double sn = std::sin(angle);
    Transform r;
    r.a = cs;
    r.b = sn;
    r.c = -sn;
    r.d = cs;
    return translation(center) * r * translation(-center);
}

Transform Transform::scaling(Vec2 center, double factor)
{
    Transform s;
    s.a = factor;
    s.d = factor;
    return translation(center) * s * translation(-center);
}

Transform Transform::mirror(Vec2 p1, Vec2 p2)
{
    const Vec2 dir = normalized(p2 - p1);
    // Reflection across a line through the origin with unit direction (ux, uy).
    Transform m;
    m.a = dir.x * dir.x - dir.y * dir.y;
    m.b = 2.0 * dir.x * dir.y;
    m.c = 2.0 * dir.x * dir.y;
    m.d = dir.y * dir.y - dir.x * dir.x;
    return translation(p1) * m * translation(-p1);
}

Vec2 closestPointOnSegment(Vec2 p, Vec2 a, Vec2 b)
{
    const Vec2 ab = b - a;
    const double len2 = dot(ab, ab);
    if (len2 < kEps * kEps)
        return a;
    const double t = std::clamp(dot(p - a, ab) / len2, 0.0, 1.0);
    return a + ab * t;
}

double distanceToSegment(Vec2 p, Vec2 a, Vec2 b)
{
    return distance(p, closestPointOnSegment(p, a, b));
}

std::vector<Vec2> intersectSegments(Vec2 a1, Vec2 a2, Vec2 b1, Vec2 b2)
{
    const Vec2 r = a2 - a1;
    const Vec2 s = b2 - b1;
    const double denom = cross(r, s);
    // Parallel or overlapping segments have no single intersection point.
    if (std::abs(denom) <= 1e-12 * length(r) * length(s))
        return {};
    const Vec2 q = b1 - a1;
    const double t = cross(q, s) / denom;
    const double u = cross(q, r) / denom;
    constexpr double tol = 1e-9;
    if (t < -tol || t > 1.0 + tol || u < -tol || u > 1.0 + tol)
        return {};
    return {a1 + r * t};
}

std::vector<Vec2> intersectSegmentCircle(Vec2 a, Vec2 b, Vec2 center, double radius)
{
    const Vec2 d = b - a;
    const Vec2 f = a - center;
    const double qa = dot(d, d);
    if (qa < kEps * kEps)
        return {};
    const double qb = 2.0 * dot(f, d);
    const double qc = dot(f, f) - radius * radius;
    double disc = qb * qb - 4.0 * qa * qc;
    // Allow a small negative discriminant so that tangent lines still report
    // their touching point despite rounding.
    if (disc < -1e-12 * qb * qb - 1e-18)
        return {};
    disc = std::max(disc, 0.0);
    const double sq = std::sqrt(disc);
    std::vector<Vec2> out;
    constexpr double tol = 1e-9;
    for (double t : {(-qb - sq) / (2.0 * qa), (-qb + sq) / (2.0 * qa)}) {
        if (t < -tol || t > 1.0 + tol)
            continue;
        const Vec2 p = a + d * t;
        if (out.empty() || !almostEqual(out.front(), p, 1e-9 * (1.0 + radius)))
            out.push_back(p);
    }
    return out;
}

std::vector<Vec2> intersectCircles(Vec2 c1, double r1, Vec2 c2, double r2)
{
    const double d = distance(c1, c2);
    const double tol = 1e-9 * (1.0 + r1 + r2);
    if (d < tol || d > r1 + r2 + tol || d < std::abs(r1 - r2) - tol)
        return {};
    const double a = (r1 * r1 - r2 * r2 + d * d) / (2.0 * d);
    const double h = std::sqrt(std::max(0.0, r1 * r1 - a * a));
    const Vec2 dir = (c2 - c1) / d;
    const Vec2 base = c1 + dir * a;
    if (h <= tol)
        return {base};
    const Vec2 off = perpendicular(dir) * h;
    return {base + off, base - off};
}

} // namespace cad
