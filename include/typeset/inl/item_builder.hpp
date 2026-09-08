#ifndef TYPESET_INL_ITEM_BUILDER_HPP
#define TYPESET_INL_ITEM_BUILDER_HPP

#include <vector>

#include "typeset/font/font_set.hpp"
#include "typeset/inl/annotation.hpp"
#include "typeset/inl/line_item.hpp"
#include "typeset/inl/shaped.hpp"
#include "typeset/style.hpp"
#include "typeset/writing_mode.hpp"

/**
 * inl/item_builder — シェイピング結果 → Box / Glue / Penalty 列
 *
 * JLReq の文字クラスとアキ量表を使って、クラスタ列を組版アイテム列へ変換する。
 *  - 約物の仮想ボディを半角へ詰め、字面のオフセットを Box に持たせる
 *  - 文字クラスの隣接ペアから伸縮するアキ（Glue）を入れる
 *  - 禁則（行頭・行末・分離禁止・欧文単語内 [UAX #14]）を Penalty(∞) で表す
 *  - ぶら下げを「幅が負の Penalty」で表す
 *  - ルビ・縦中横・圏点・割注・字取りを Box の本体／付随グリフ／固定アキに落とす
 * 書字方向に依存しない（注記の付く側だけ縦横で変わる）。
 */
namespace typeset::inl {

struct ItemBuildContext {
    font::FontSet& fonts;
    WritingMode writingMode = WritingMode::HorizontalTb;
    TextOrientation orientation = TextOrientation::Mixed;
    const std::vector<TextStyle>* styles = nullptr;   ///< styleIndex → TextStyle
    /// 本文のスタイル（注記の子テキストを組む基準。styles[0] 相当）
    const TextStyle* baseStyle = nullptr;
    /// 字間（em）。クラスタ間の Glue に足す
    float letterSpacing = 0.0f;
};

/**
 * アイテム列を組み立てる。末尾には段落終端（無限に伸びる Glue ＋ 強制ブレーク）を付ける
 */
std::vector<LineItem> buildLineItems(const ShapedText& shaped,
                                     const std::vector<Annotation>& annotations,
                                     const SpacingOptions& opts,
                                     const ItemBuildContext& ctx);

/// 注記の付く側（縦組み: +1 = 右、横組み: −1 = 上）
inline float annotationSide(WritingMode wm) { return isVertical(wm) ? 1.0f : -1.0f; }

} // namespace typeset::inl

#endif // TYPESET_INL_ITEM_BUILDER_HPP
