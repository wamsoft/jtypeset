#ifndef TYPESET_PAGE_FLOW_LAYOUTER_HPP
#define TYPESET_PAGE_FLOW_LAYOUTER_HPP

#include <map>
#include <string>
#include <vector>

#include "typeset/block/block.hpp"
#include "typeset/font/font_set.hpp"
#include "typeset/page/page.hpp"

/**
 * page/flow_layouter — Flow をページ列へ流し込む
 *
 *  - ページ → 段（Region）の順に埋める。段落は段に入るだけ組み、残りは次の段へ続きから
 *  - orphans / widows、keepWithNext（見出し）、breakBefore（改段・改ページ）、
 *    spaceBefore / spaceAfter、段抜き（spanColumns）、ラベル付き段落、罫線
 *  - 柱・ノンブルはページ確定後に `{page}` 等を置換して組む
 *
 * 制限（Phase 2）: 段抜きブロックは段に内容があるとページを改める（段の途中での
 * 段抜き＝段のバランス取りは後回し）。SectionBlock の段数変更も次のページから効く。
 */
namespace typeset::page {

struct FlowLayoutOptions {
    /// `{title}` 等の置換フィールド（`{page}` `{pages}` は自動。本文中の `{ref:ラベル}` `{page:ラベル}` `{fig}` `{table}` も置換する）
    std::map<std::u16string, std::u16string> fields;
    /// 版面・段の枠を薄く描く（デバッグ用）
    bool drawGuides = false;
    /// 最終ページ（段組）の段の高さを揃える
    bool balanceLastPage = true;
    /// 見出し番号の後ろに付ける区切り
    std::u16string headingNumberSeparator = u" ";
    /// 図番号・表番号の書式（`{n}` が番号）
    std::u16string figureFormat = u"図 {n}";
    std::u16string tableFormat = u"表 {n}";
};

class FlowLayouter {
public:
    explicit FlowLayouter(font::FontSet& fonts) : fonts_(fonts) {}

    std::vector<Page> layout(const block::Flow& flow, const PageSequence& seq,
                             const FlowLayoutOptions& opts = {});

private:
    font::FontSet& fonts_;
};

} // namespace typeset::page

#endif // TYPESET_PAGE_FLOW_LAYOUTER_HPP
