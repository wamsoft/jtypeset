#ifndef TYPESET_WRITING_MODE_HPP
#define TYPESET_WRITING_MODE_HPP

#include "typeset/geom.hpp"

/**
 * writing_mode — 書字方向と論理座標
 *
 * 行内は「インライン軸（行の進む方向＝送り）」と「ブロック軸（行に直交）」の
 * 2 軸で組む。richtext 縦組みの (u, v) をすべての書字方向へ一般化したもの。
 *
 *   横組み        inline = +x（右）  block = +y（下）
 *   縦組み(rl)    inline = +y（下）  block = −x（左）  ※行は右から左へ進む
 *   縦組み(lr)    inline = +y（下）  block = +x（右）
 *
 * ブロック軸の原点は「行の中心線」（縦組みなら列の中心線、横組みなら行の中心線）。
 * 文字の仮想ボディ（em box）の中心をここへ合わせるのが全書字方向共通の規則。
 */
namespace typeset {

enum class WritingMode : uint8_t {
    HorizontalTb,   ///< 横組み
    VerticalRl,     ///< 縦組み・行は右から左へ（日本語の既定）
    VerticalLr,     ///< 縦組み・行は左から右へ
};

inline bool isVertical(WritingMode wm) { return wm != WritingMode::HorizontalTb; }

/**
 * 縦組み中の文字の正立／横倒し（CSS text-orientation 相当）
 */
enum class TextOrientation : uint8_t {
    Mixed,      ///< 和文は正立、欧文は横倒し（既定）
    Upright,    ///< すべて正立
    Sideways,   ///< すべて横倒し
};

/**
 * 論理座標。inline_ は行頭からの送り、block は行の中心線からのずれ
 * （block 正方向は横組みで下、縦組み(rl)で左、縦組み(lr)で右…ではなく、
 *   **縦組みでは常に右を正**とする。richtext の u 軸と同じ。行送りの向きとは独立）
 */
struct LogicalPoint {
    Pt inline_ = 0.0f;
    Pt block = 0.0f;
};

/**
 * 論理座標 → 物理座標
 * @param wm 書字方向
 * @param p 論理座標（inline_ = 行頭からの送り、block = 中心線からのずれ）
 * @param lineOrigin 行頭の物理位置（行の中心線上の点）
 */
inline Point toPhysical(WritingMode wm, LogicalPoint p, Point lineOrigin) {
    if (wm == WritingMode::HorizontalTb) {
        return Point{lineOrigin.x + p.inline_, lineOrigin.y + p.block};
    }
    // 縦組み: inline は下へ、block は右へ（rl / lr で共通。列の送りは上位層）
    return Point{lineOrigin.x + p.block, lineOrigin.y + p.inline_};
}

/**
 * 横倒しグリフの回転角（ラジアン）
 *
 * 角度は数学慣習（y-up）の反時計回りが正。描画先は y-down なので、画面上では
 * 時計回りに 90 度倒れる（欧文の天が右を向く）。
 */
inline constexpr float kSidewaysRotation = -1.5707963267948966f;   // -90°

} // namespace typeset

#endif // TYPESET_WRITING_MODE_HPP
