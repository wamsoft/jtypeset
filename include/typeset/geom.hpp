#ifndef TYPESET_GEOM_HPP
#define TYPESET_GEOM_HPP

#include <algorithm>
#include <cmath>
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
    /// 回転（ラジアン。y-down なので画面上では時計回りが正）
    static Matrix rotation(float radians) {
        const float c = std::cos(radians), s = std::sin(radians);
        Matrix m;
        m.xx = c; m.xy = -s;
        m.yx = s; m.yy = c;
        return m;
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

/**
 * グラデーションの停止点
 */
struct GradientStop {
    float offset = 0.0f;    ///< 0〜1
    Color color;
    bool operator==(const GradientStop& o) const { return offset == o.offset && color == o.color; }
};

enum class PaintKind : uint8_t { Solid, Linear, Radial };

/**
 * グラデーションの座標系
 *  - BoundingBox: 描く対象（GlyphRun なら run 全体、PathItem ならパス）の外接矩形を 0〜1 に正規化した座標
 *  - UserSpace: ページ座標（pt、y-down）
 */
enum class PaintUnits : uint8_t { BoundingBox, UserSpace };

/**
 * 塗り — 単色または線形／放射グラデーション
 *
 * Color から暗黙に作れるので、単色のときは今までどおり色を代入すればよい。
 * PDF は停止点ごとの不透明度を持てないので、最大の不透明度を全体に掛ける（ラスタ・SVG は停止点ごと）
 */
struct Paint {
    PaintKind kind = PaintKind::Solid;
    Color color{0, 0, 0, 255};              ///< Solid のときの色
    PaintUnits units = PaintUnits::BoundingBox;
    Point start;                            ///< Linear: 始点／Radial: 中心
    Point end{1.0f, 0.0f};                  ///< Linear: 終点
    float radius = 0.5f;                    ///< Radial: 半径
    std::vector<GradientStop> stops;        ///< offset の昇順

    Paint() = default;
    Paint(Color c) : color(c) {}            // 単色は暗黙変換

    static Paint linear(Point from, Point to, std::vector<GradientStop> stops,
                        PaintUnits units = PaintUnits::BoundingBox) {
        Paint p;
        p.kind = PaintKind::Linear;
        p.start = from;
        p.end = to;
        p.stops = std::move(stops);
        p.units = units;
        return p;
    }
    static Paint radial(Point center, float radius, std::vector<GradientStop> stops,
                        PaintUnits units = PaintUnits::BoundingBox) {
        Paint p;
        p.kind = PaintKind::Radial;
        p.start = center;
        p.radius = radius;
        p.stops = std::move(stops);
        p.units = units;
        return p;
    }

    bool isGradient() const { return kind != PaintKind::Solid && stops.size() >= 2; }

    /// t（0〜1）の色。停止点の間を線形補間する
    Color at(float t) const {
        if (stops.empty()) return color;
        if (t <= stops.front().offset) return stops.front().color;
        if (t >= stops.back().offset) return stops.back().color;
        for (size_t i = 1; i < stops.size(); ++i) {
            if (t > stops[i].offset) continue;
            const GradientStop& a = stops[i - 1];
            const GradientStop& b = stops[i];
            const float span = b.offset - a.offset;
            const float u = span > 0.0f ? (t - a.offset) / span : 0.0f;
            auto mix = [&](uint8_t x, uint8_t y) {
                return static_cast<uint8_t>(x + (static_cast<float>(y) - x) * u + 0.5f);
            };
            return Color{mix(a.color.r, b.color.r), mix(a.color.g, b.color.g),
                         mix(a.color.b, b.color.b), mix(a.color.a, b.color.a)};
        }
        return stops.back().color;
    }

    /// 単色として扱うときの色（グラデーションは中間の色）
    Color solid() const { return isGradient() ? at(0.5f) : color; }
    /// 不透明度の代表値（グラデーションは最大）
    uint8_t maxAlpha() const {
        if (!isGradient()) return color.a;
        uint8_t a = 0;
        for (const GradientStop& s : stops) a = std::max(a, s.color.a);
        return a;
    }

    bool operator==(const Paint& o) const {
        return kind == o.kind && color == o.color && units == o.units &&
               start.x == o.start.x && start.y == o.start.y && end.x == o.end.x && end.y == o.end.y &&
               radius == o.radius && stops == o.stops;
    }
    bool operator!=(const Paint& o) const { return !(*this == o); }
};

enum class StrokeJoin : uint8_t { Miter, Round, Bevel };
enum class StrokeCap : uint8_t { Butt, Round, Square };

/**
 * 線の描き方
 */
struct Stroke {
    Paint color;            ///< 線の塗り（単色または グラデーション。Color から暗黙に作れる）
    Pt width = 1.0f;
    StrokeJoin join = StrokeJoin::Miter;
    StrokeCap cap = StrokeCap::Butt;
    float miterLimit = 4.0f;

    bool operator==(const Stroke& o) const {
        return color == o.color && width == o.width && join == o.join && cap == o.cap &&
               miterLimit == o.miterLimit;
    }
    bool operator!=(const Stroke& o) const { return !(*this == o); }
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
