#ifndef TYPESET_GEOM_HPP
#define TYPESET_GEOM_HPP

#include <cstdint>
#include <vector>

/**
 * geom — 単位と幾何
 *
 * 文書内部の長さはすべて pt（1/72 inch）の float。ラスタ backend が dpi/72 を
 * 掛けてピクセルにする。座標はページ左上原点・y-down。
 */
namespace typeset {

using Pt = float;

constexpr Pt kInch = 72.0f;
constexpr Pt kMm = 72.0f / 25.4f;
constexpr Pt kCm = kMm * 10.0f;

struct Point {
    Pt x = 0.0f;
    Pt y = 0.0f;
};

struct Size {
    Pt w = 0.0f;
    Pt h = 0.0f;
};

struct Rect {
    Pt x = 0.0f;
    Pt y = 0.0f;
    Pt w = 0.0f;
    Pt h = 0.0f;

    Pt right() const { return x + w; }
    Pt bottom() const { return y + h; }
    bool empty() const { return w <= 0.0f || h <= 0.0f; }
    bool contains(Point p) const {
        return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
    }
    Rect intersect(const Rect& o) const;
    Rect unite(const Rect& o) const;
};

/**
 * 2x2 行列（平行移動なし）。グリフ固有の変形に使う。
 *   x' = xx*x + xy*y
 *   y' = yx*x + yy*y
 */
struct Mat2 {
    float xx = 1.0f, xy = 0.0f;
    float yx = 0.0f, yy = 1.0f;

    bool isIdentity() const {
        return xx == 1.0f && xy == 0.0f && yx == 0.0f && yy == 1.0f;
    }
};

/**
 * 2x3 アフィン行列（行優先）
 *   x' = xx*x + xy*y + dx
 *   y' = yx*x + yy*y + dy
 */
struct Matrix {
    float xx = 1.0f, xy = 0.0f, dx = 0.0f;
    float yx = 0.0f, yy = 1.0f, dy = 0.0f;

    static Matrix identity() { return Matrix{}; }
    static Matrix translation(Pt tx, Pt ty) {
        Matrix m; m.dx = tx; m.dy = ty; return m;
    }
    static Matrix scaling(float sx, float sy) {
        Matrix m; m.xx = sx; m.yy = sy; return m;
    }
    static Matrix fromMat2(const Mat2& r, Pt tx = 0.0f, Pt ty = 0.0f) {
        Matrix m;
        m.xx = r.xx; m.xy = r.xy; m.yx = r.yx; m.yy = r.yy;
        m.dx = tx; m.dy = ty;
        return m;
    }

    bool isIdentity() const {
        return xx == 1.0f && xy == 0.0f && dx == 0.0f &&
               yx == 0.0f && yy == 1.0f && dy == 0.0f;
    }
    Mat2 linear() const { return Mat2{xx, xy, yx, yy}; }
    float determinant() const { return xx * yy - xy * yx; }
    Point apply(Point p) const {
        return Point{xx * p.x + xy * p.y + dx, yx * p.x + yy * p.y + dy};
    }
    Point applyVector(Point v) const {
        return Point{xx * v.x + xy * v.y, yx * v.x + yy * v.y};
    }
};

/// a を後から適用する合成（結果 = a ∘ b。b で写してから a）
Matrix multiply(const Matrix& a, const Matrix& b);
Mat2 multiply(const Mat2& a, const Mat2& b);
/// 逆行列。特異なら単位行列を返し ok=false
Matrix invert(const Matrix& m, bool* ok = nullptr);

/**
 * 色（非前乗算 RGBA）
 */
struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    static Color rgb(uint8_t r, uint8_t g, uint8_t b) { return Color{r, g, b, 255}; }
    static Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return Color{r, g, b, a}; }
    static Color argb(uint32_t v) {
        return Color{static_cast<uint8_t>((v >> 16) & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF),
                     static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 24) & 0xFF)};
    }
    uint32_t toArgb() const {
        return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
    }
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Color& o) const { return !(*this == o); }
};

enum class StrokeJoin : uint8_t { Miter, Round, Bevel };
enum class StrokeCap : uint8_t { Butt, Round, Square };

/**
 * 線の描き方
 */
struct Stroke {
    Color color;
    Pt width = 1.0f;
    StrokeJoin join = StrokeJoin::Miter;
    StrokeCap cap = StrokeCap::Butt;
    float miterLimit = 4.0f;
};

/**
 * パス（moveTo / lineTo / quadTo / cubicTo / close）
 *
 * pts は Cmd ごとに Move:1 Line:1 Quad:2 Cubic:3 Close:0 個消費する。
 */
struct Path {
    enum class Cmd : uint8_t { Move, Line, Quad, Cubic, Close };

    std::vector<Cmd> cmds;
    std::vector<Point> pts;

    bool empty() const { return cmds.empty(); }

    void moveTo(Pt x, Pt y) { cmds.push_back(Cmd::Move); pts.push_back({x, y}); }
    void lineTo(Pt x, Pt y) { cmds.push_back(Cmd::Line); pts.push_back({x, y}); }
    void quadTo(Pt cx, Pt cy, Pt x, Pt y) {
        cmds.push_back(Cmd::Quad); pts.push_back({cx, cy}); pts.push_back({x, y});
    }
    void cubicTo(Pt c1x, Pt c1y, Pt c2x, Pt c2y, Pt x, Pt y) {
        cmds.push_back(Cmd::Cubic);
        pts.push_back({c1x, c1y}); pts.push_back({c2x, c2y}); pts.push_back({x, y});
    }
    void close() { cmds.push_back(Cmd::Close); }

    void addRect(const Rect& r);
    /// 楕円（矩形に内接）を 4 本の 3 次ベジェで近似
    void addEllipse(const Rect& r);
    void addLine(Point a, Point b) { moveTo(a.x, a.y); lineTo(b.x, b.y); }

    Path transformed(const Matrix& m) const;
    /// 制御点を含むバウンディングボックス
    Rect controlBounds() const;
};

/// 曲線を折れ線に平坦化した閉／開多角形列を得る（tolerance は pt）
std::vector<std::vector<Point>> flattenPath(const Path& path, float tolerance,
                                            std::vector<bool>* closedFlags = nullptr);

} // namespace typeset

#endif // TYPESET_GEOM_HPP
