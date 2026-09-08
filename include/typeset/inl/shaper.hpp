#ifndef TYPESET_INL_SHAPER_HPP
#define TYPESET_INL_SHAPER_HPP

#include <string>
#include <vector>

#include "typeset/font/font_set.hpp"
#include "typeset/inl/shaped.hpp"
#include "typeset/writing_mode.hpp"

/**
 * inl/shaper — Itemizer ＋ HarfBuzz シェイピング
 *
 * スタイル境界・フォントフォールバック（文字カバレッジ）・正立／横倒しで
 * ランに分け、各ランを HarfBuzz で組む。
 *
 *  - 縦組みの正立ラン … HB_DIRECTION_TTB。vert / vrt2、vmtx / VORG、vkrn / vpal が効く
 *  - 横組み・横倒しラン … HB_DIRECTION_LTR（横倒しは物理化のとき -90° 回す）
 *
 * ブロック軸の位置は「em box の中心＝行の中心線」の規則で決める（設計.md 2.6）。
 * 横組みのベースラインは第一候補フォントの ascender/descender から求め、
 * フォールバック先のフォントも同じベースラインに乗せる。
 */
namespace typeset::inl {

struct ShapeContext {
    font::FontSet& fonts;
    WritingMode writingMode = WritingMode::HorizontalTb;
    TextOrientation orientation = TextOrientation::Mixed;
    /// styleIndex → TextStyle
    const std::vector<TextStyle>* styles = nullptr;
};

/**
 * シェイピング実行
 * @param text 1 段落分（改行は含めない）
 * @param runs スタイル区間（text 全体を覆うこと）
 */
ShapedText shapeText(const std::u16string& text, const std::vector<StyleRun>& runs,
                     const ShapeContext& ctx);

/// 単一スタイル版
ShapedText shapeText(const std::u16string& text, const TextStyle& style,
                     font::FontSet& fonts, WritingMode wm,
                     TextOrientation orientation = TextOrientation::Mixed);

/**
 * 横組みのベースラインのブロック位置（中心線から下向き正）／
 * 縦組み横倒しランのベースラインのブロック位置（中心線から右向き正）
 */
Pt baselineOffset(const glyphware::Face& face, Pt size, WritingMode wm);

} // namespace typeset::inl

#endif // TYPESET_INL_SHAPER_HPP
