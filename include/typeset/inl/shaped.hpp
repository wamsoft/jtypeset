#ifndef TYPESET_INL_SHAPED_HPP
#define TYPESET_INL_SHAPED_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glyphware/Face.h>

#include "typeset/dl/display_list.hpp"
#include "typeset/obj/object.hpp"
#include "typeset/geom.hpp"
#include "typeset/style.hpp"
#include "typeset/text/char_class.hpp"

/**
 * inl/shaped — シェイピング結果（組版層の入力）
 *
 * 座標はすべて論理座標（writing_mode.hpp）:
 *   inline_ … 行頭からの送り
 *   block   … 行の中心線からのずれ（横組みは下が正、縦組みは右が正）
 */
namespace typeset::inl {

/**
 * 配置済みグリフ 1 個（論理座標）
 */
struct PlacedGlyph {
    std::shared_ptr<glyphware::Face> face;
    uint32_t gid = 0;
    Pt inline_ = 0.0f;      ///< ペン位置（送り方向）
    Pt block = 0.0f;        ///< ペン位置（中心線からのずれ）
    Pt advance = 0.0f;      ///< 送り
    Mat2 xform;             ///< 物理空間でのグリフ固有の変形（回転・平体長体・斜体）
    Pt size = 0.0f;         ///< フォントサイズ
    uint32_t charIndex = 0; ///< 元テキストでの位置（UTF-16）
    Pt embolden = 0.0f;     ///< フェイクボールドの太らせ幅
    uint32_t styleIndex = 0;///< 色・縁取りを引くためのスタイル番号

    /// 行内画像（グリフではなく画像を置く）。block は画像中心の中心線からのずれ
    std::shared_ptr<const dl::Image> image;
    Size imageSize;
    /// 行内オブジェクト（数式など）。block はオブジェクトの行送り方向の始端（横組み: 上端）の中心線からのずれ。
    /// 縦組みでは横倒し（時計回りに 90°）で置く
    std::shared_ptr<const obj::ObjectResult> object;
};

/**
 * シェイピング結果のクラスタ（組版の最小単位）
 *
 * HarfBuzz のクラスタ 1 つに対応する。組版層はグリフではなくこの単位で扱い、
 * 行を確定したあとで「クラスタの新しい位置 − origin」をグリフの inline_ に足す。
 */
struct ShapedCluster {
    uint32_t glyphStart = 0;
    uint32_t glyphCount = 0;
    size_t charStart = 0;
    size_t charEnd = 0;
    Pt origin = 0.0f;       ///< ベタ組みでの inline 位置
    Pt advance = 0.0f;      ///< シェイパーが返した送り
    text::CharClass charClass = text::CharClass::Unknown;
    bool upright = true;    ///< 正立か横倒しか（横組みでは常に true）
    uint32_t styleIndex = 0;
    bool object = false;    ///< 行内画像などの箱（ボディ幅はシェイパーの送りのまま）
};

struct ShapedText {
    std::u16string sourceText;
    std::vector<PlacedGlyph> glyphs;
    std::vector<ShapedCluster> clusters;
    Pt advance = 0.0f;      ///< 総送り
    Pt blockMin = 0.0f;     ///< 中心線からの張り出し（負側）
    Pt blockMax = 0.0f;     ///< 同（正側）
};

/**
 * スタイル区間（[start, end) は UTF-16 位置）
 */
struct StyleRun {
    size_t start = 0;
    size_t end = 0;
    uint32_t styleIndex = 0;
};

} // namespace typeset::inl

#endif // TYPESET_INL_SHAPED_HPP
