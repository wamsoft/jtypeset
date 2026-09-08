#ifndef TYPESET_INL_COLOR_GLYPH_HPP
#define TYPESET_INL_COLOR_GLYPH_HPP

#include <cstdint>

#include <glyphware/Face.h>

#include "typeset/dl/display_list.hpp"
#include "typeset/geom.hpp"

/**
 * color_glyph — カラーグリフ（絵文字）を表示リストへ
 *
 * COLR（v0 / v1）のフォントは glyphware がペイントグラフをレイヤ列に平坦化してくれるので、各レイヤの
 * アウトラインを塗り付きの PathItem にする。グラデーションは色止めの平均色で塗る（近似）。
 * CBDT / sbix のビットマップ絵文字は BGRA を ImageItem にする。
 * どちらでもなければ false を返し、呼び出し側は通常のグリフ（単色のアウトライン）として出す。
 * 3 つの backend が同じ表示リストを描くので、ラスタ・PDF・SVG で同じ色になる。
 */
namespace typeset::inl {

/**
 * @param face  グリフのフォント（descriptor().color が false なら何もしない）
 * @param size  フォントサイズ（pt）
 * @param pos   ペン位置（物理座標）
 * @param xform グリフ固有の変形（回転・平体長体・斜体）
 */
bool emitColorGlyph(dl::DisplayList& out, glyphware::Face& face, uint32_t gid, Pt size, Point pos,
                    const Mat2& xform);

} // namespace typeset::inl

#endif // TYPESET_INL_COLOR_GLYPH_HPP
