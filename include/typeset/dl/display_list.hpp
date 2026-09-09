#ifndef TYPESET_DL_DISPLAY_LIST_HPP
#define TYPESET_DL_DISPLAY_LIST_HPP

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "typeset/geom.hpp"

namespace glyphware { class Face; }

/**
 * dl — 表示リスト（Display List）
 *
 * レイアウト層の出力であり、backend（ラスタ / PDF / SVG）の入力。
 * ページごとに 1 本。座標はページ左上原点・y-down・pt。
 *
 * richtext の「組版結果は配置済みグリフ列で切る」を一般化したもので、
 * グリフのほかに罫線・吹き出し（Path）、矩形、画像、グループを持つ。
 * backend はこれを解釈するだけで、組版の知識を持たない。
 */
namespace typeset::dl {

constexpr uint32_t kNoChar = std::numeric_limits<uint32_t>::max();

/**
 * 配置済みグリフ 1 個
 *
 * xform は「フォントサイズに拡大したグリフ」に掛ける 2x2（y-down ページ空間）。
 * 回転・平体長体・フェイク斜体をここに畳む。組み立ては glyph_transform.hpp。
 */
struct Glyph {
    uint32_t gid = 0;
    Point pos;                      ///< ペン位置（原点。横組みならベースライン左端）
    Mat2 xform;
    uint32_t charIndex = kNoChar;   ///< GlyphRun::text 内の位置（UTF-16）。ToUnicode 用
};

/**
 * 同じ face・サイズ・塗りのグリフ列
 */
struct GlyphRun {
    std::shared_ptr<glyphware::Face> face;
    Pt size = 10.0f;
    std::vector<Glyph> glyphs;

    std::optional<Paint> fill;      ///< 塗り（無ければ塗らない。単色またはグラデーション）
    std::optional<Stroke> stroke;   ///< 縁取り
    Pt embolden = 0.0f;             ///< フェイクボールドの太らせ幅（0 = 無し）
    /// ぼかし半径（pt）。影の層に使う。ラスタと SVG はガウスぼかし、PDF はぼかさずに描く
    Pt blur = 0.0f;

    /// charIndex が指す原文（PDF の ToUnicode、SVG の代替テキストに使う）
    std::shared_ptr<const std::u16string> text;
};

/**
 * パス（罫線・下線・吹き出し・図形）
 */
struct PathItem {
    Path path;
    std::optional<Paint> fill;
    std::optional<Stroke> stroke;
    bool evenOdd = false;           ///< 塗り規則（既定は nonzero）
};

/**
 * 塗り矩形（背景・網かけ）。PathItem でも書けるが頻出なので専用に持つ
 */
struct RectItem {
    Rect rect;
    Paint fill;
};

/**
 * 画像（RGBA8888、非前乗算）
 */
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    /// 元のエンコード済みデータ（JPEG なら PDF へ素通しできる）。無くてよい
    std::vector<uint8_t> encoded;
    enum class Encoding : uint8_t { None, Jpeg, Png } encoding = Encoding::None;
};

struct ImageItem {
    std::shared_ptr<const Image> image;
    /// 画像ピクセル座標 → ページ座標。単純配置なら translate * scale(w/px, h/px)
    Matrix xform;
    float opacity = 1.0f;
};

/**
 * しおり（PDF のアウトライン）。描画はしない
 */
struct Bookmark {
    std::u16string title;
    int level = 1;
    Point pos;                      ///< ジャンプ先（ページ座標）
};

struct Group;

using Item = std::variant<GlyphRun, PathItem, RectItem, ImageItem, Group, Bookmark>;

/**
 * 変形・クリップ・不透明度のスコープ
 */
struct Group {
    Matrix xform;                   ///< 子の座標 → 親の座標
    std::optional<Rect> clip;       ///< 親座標系でのクリップ矩形
    float opacity = 1.0f;
    std::vector<Item> children;
};

/**
 * 1 ページ分の表示リスト
 */
struct DisplayList {
    Size page;
    std::vector<Item> items;

    void add(Item item) { items.push_back(std::move(item)); }

    // 便宜メソッド
    void addRect(const Rect& r, Paint fill) { items.push_back(RectItem{r, std::move(fill)}); }
    void addPath(Path path, std::optional<Paint> fill, std::optional<Stroke> stroke = std::nullopt) {
        PathItem it;
        it.path = std::move(path);
        it.fill = fill;
        it.stroke = stroke;
        items.push_back(std::move(it));
    }
    void addLine(Point a, Point b, const Stroke& stroke) {
        Path p;
        p.addLine(a, b);
        addPath(std::move(p), std::nullopt, stroke);
    }
};

/// 全要素のバウンディングボックス（グリフは em box で概算）
Rect bounds(const DisplayList& list);

/// GlyphRun の外接矩形（グリフは em box で概算。グラデーションの BoundingBox 座標に使う）
Rect runBounds(const GlyphRun& run);

} // namespace typeset::dl

#endif // TYPESET_DL_DISPLAY_LIST_HPP
