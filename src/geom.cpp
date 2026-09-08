/**
 * geom.cpp — 幾何ユーティリティ
 */

#include "typeset/geom.hpp"

#include <algorithm>
#include <cmath>

namespace typeset {

//------------------------------------------------------------------------------
// Rect
//------------------------------------------------------------------------------

Rect Rect::intersect(const Rect& o) const {
    const Pt x0 = std::max(x, o.x);
    const Pt y0 = std::max(y, o.y);
    const Pt x1 = std::min(right(), o.right());
    const Pt y1 = std::min(bottom(), o.bottom());
    if (x1 <= x0 || y1 <= y0) return Rect{x0, y0, 0.0f, 0.0f};
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

Rect Rect::unite(const Rect& o) const {
    if (empty()) return o;
    if (o.empty()) return *this;
    const Pt x0 = std::min(x, o.x);
    const Pt y0 = std::min(y, o.y);
    const Pt x1 = std::max(right(), o.right());
    const Pt y1 = std::max(bottom(), o.bottom());
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

//------------------------------------------------------------------------------
// Matrix
//------------------------------------------------------------------------------

Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix m;
    m.xx = a.xx * b.xx + a.xy * b.yx;
    m.xy = a.xx * b.xy + a.xy * b.yy;
    m.dx = a.xx * b.dx + a.xy * b.dy + a.dx;
    m.yx = a.yx * b.xx + a.yy * b.yx;
    m.yy = a.yx * b.xy + a.yy * b.yy;
    m.dy = a.yx * b.dx + a.yy * b.dy + a.dy;
    return m;
}

Mat2 multiply(const Mat2& a, const Mat2& b) {
    Mat2 m;
    m.xx = a.xx * b.xx + a.xy * b.yx;
    m.xy = a.xx * b.xy + a.xy * b.yy;
    m.yx = a.yx * b.xx + a.yy * b.yx;
    m.yy = a.yx * b.xy + a.yy * b.yy;
    return m;
}

Matrix invert(const Matrix& m, bool* ok) {
    const float det = m.determinant();
    if (std::fabs(det) < 1e-12f) {
        if (ok) *ok = false;
        return Matrix{};
    }
    const float inv = 1.0f / det;
    Matrix r;
    r.xx = m.yy * inv;
    r.xy = -m.xy * inv;
    r.yx = -m.yx * inv;
    r.yy = m.xx * inv;
    r.dx = -(r.xx * m.dx + r.xy * m.dy);
    r.dy = -(r.yx * m.dx + r.yy * m.dy);
    if (ok) *ok = true;
    return r;
}

//------------------------------------------------------------------------------
// Path
//------------------------------------------------------------------------------

void Path::addRect(const Rect& r) {
    moveTo(r.x, r.y);
    lineTo(r.right(), r.y);
    lineTo(r.right(), r.bottom());
    lineTo(r.x, r.bottom());
    close();
}

void Path::addEllipse(const Rect& r) {
    // 4 本の 3 次ベジェによる近似（kappa = 4(√2−1)/3）
    constexpr float k = 0.5522847498f;
    const Pt cx = r.x + r.w * 0.5f;
    const Pt cy = r.y + r.h * 0.5f;
    const Pt rx = r.w * 0.5f;
    const Pt ry = r.h * 0.5f;
    moveTo(cx + rx, cy);
    cubicTo(cx + rx, cy + ry * k, cx + rx * k, cy + ry, cx, cy + ry);
    cubicTo(cx - rx * k, cy + ry, cx - rx, cy + ry * k, cx - rx, cy);
    cubicTo(cx - rx, cy - ry * k, cx - rx * k, cy - ry, cx, cy - ry);
    cubicTo(cx + rx * k, cy - ry, cx + rx, cy - ry * k, cx + rx, cy);
    close();
}

Path Path::transformed(const Matrix& m) const {
    Path out;
    out.cmds = cmds;
    out.pts.reserve(pts.size());
    for (const Point& p : pts) out.pts.push_back(m.apply(p));
    return out;
}

Rect Path::controlBounds() const {
    if (pts.empty()) return Rect{};
    Pt x0 = pts[0].x, y0 = pts[0].y, x1 = x0, y1 = y0;
    for (const Point& p : pts) {
        x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
    }
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

namespace {

int quadSegments(Point p0, Point p1, Point p2, float tol) {
    // 2 次差分の大きさから必要な分割数を見積もる
    const Point dd{p0.x - 2 * p1.x + p2.x, p0.y - 2 * p1.y + p2.y};
    const float d = std::sqrt(dd.x * dd.x + dd.y * dd.y);
    const int n = static_cast<int>(std::ceil(std::sqrt(d * 0.25f / std::max(tol, 1e-4f))));
    return std::clamp(n, 1, 64);
}

int cubicSegments(Point p0, Point p1, Point p2, Point p3, float tol) {
    const Point d1{p0.x - 2 * p1.x + p2.x, p0.y - 2 * p1.y + p2.y};
    const Point d2{p1.x - 2 * p2.x + p3.x, p1.y - 2 * p2.y + p3.y};
    const float d = std::max(std::sqrt(d1.x * d1.x + d1.y * d1.y),
                             std::sqrt(d2.x * d2.x + d2.y * d2.y));
    const int n = static_cast<int>(std::ceil(std::sqrt(d * 0.75f / std::max(tol, 1e-4f))));
    return std::clamp(n, 1, 64);
}

} // namespace

std::vector<std::vector<Point>> flattenPath(const Path& path, float tolerance,
                                            std::vector<bool>* closedFlags) {
    std::vector<std::vector<Point>> polys;
    if (closedFlags) closedFlags->clear();

    std::vector<Point> cur;
    bool curClosed = false;
    Point last{};
    Point start{};
    size_t pi = 0;

    auto flush = [&]() {
        if (!cur.empty()) {
            polys.push_back(std::move(cur));
            if (closedFlags) closedFlags->push_back(curClosed);
        }
        cur.clear();
        curClosed = false;
    };

    for (Path::Cmd c : path.cmds) {
        switch (c) {
        case Path::Cmd::Move:
            flush();
            last = start = path.pts[pi++];
            cur.push_back(last);
            break;
        case Path::Cmd::Line: {
            const Point p = path.pts[pi++];
            if (cur.empty()) cur.push_back(last);
            cur.push_back(p);
            last = p;
            break;
        }
        case Path::Cmd::Quad: {
            const Point c1 = path.pts[pi++];
            const Point p = path.pts[pi++];
            if (cur.empty()) cur.push_back(last);
            const int n = quadSegments(last, c1, p, tolerance);
            for (int i = 1; i <= n; ++i) {
                const float t = static_cast<float>(i) / n;
                const float u = 1.0f - t;
                cur.push_back(Point{u * u * last.x + 2 * u * t * c1.x + t * t * p.x,
                                    u * u * last.y + 2 * u * t * c1.y + t * t * p.y});
            }
            last = p;
            break;
        }
        case Path::Cmd::Cubic: {
            const Point c1 = path.pts[pi++];
            const Point c2 = path.pts[pi++];
            const Point p = path.pts[pi++];
            if (cur.empty()) cur.push_back(last);
            const int n = cubicSegments(last, c1, c2, p, tolerance);
            for (int i = 1; i <= n; ++i) {
                const float t = static_cast<float>(i) / n;
                const float u = 1.0f - t;
                const float b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
                cur.push_back(Point{b0 * last.x + b1 * c1.x + b2 * c2.x + b3 * p.x,
                                    b0 * last.y + b1 * c1.y + b2 * c2.y + b3 * p.y});
            }
            last = p;
            break;
        }
        case Path::Cmd::Close:
            if (!cur.empty()) {
                curClosed = true;
                flush();
            }
            last = start;
            break;
        }
    }
    flush();
    return polys;
}

} // namespace typeset
