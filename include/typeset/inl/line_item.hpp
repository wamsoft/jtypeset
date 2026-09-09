#ifndef TYPESET_INL_LINE_ITEM_HPP
#define TYPESET_INL_LINE_ITEM_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "typeset/inl/shaped.hpp"
#include "typeset/text/spacing_table.hpp"

/**
 * inl/line_item — Box / Glue / Penalty
 *
 * 組版対象を TeX と同じ 3 種のアイテム列で表す。寸法は pt。
 *
 * 合法なブレーク点は TeX と同じ規則で決まる:
 *  - Penalty で penalty が kInfinitePenalty 未満のところ
 *  - Glue で直前が Box のところ
 *
 * 禁則は「Glue の直前に Penalty(kInfinitePenalty) を挟む」ことで表す。
 * ぶら下げは「幅が負の Penalty」（ブレーク時に行長から取り除かれる）。
 */
namespace typeset::inl {

constexpr float kInfinitePenalty = 10000.0f;
constexpr float kForcedBreakPenalty = -10000.0f;

enum class ItemType : uint8_t { Box, Glue, Penalty };

struct LineItem {
    ItemType type = ItemType::Box;

    /// Box: ボディ幅／Penalty: ブレークしたときにその位置に現れる幅（ぶら下げは負）
    Pt width = 0.0f;

    /// Glue の伸縮
    Pt natural = 0.0f;
    Pt stretch = 0.0f;
    Pt shrink = 0.0f;

    float penalty = 0.0f;

    /// Box: 仮想ボディを詰めたときのグリフの描画オフセット（始め括弧で負）
    Pt glyphOffset = 0.0f;

    /// Box が指すクラスタ（Box 以外では kNoCluster）
    uint32_t clusterIndex = kNoCluster;

    /// 元テキストでの位置（UTF-16）
    size_t charIndex = 0;

    /// UAX #9 の埋め込みレベル（欧文間隔のグルーに付ける。-1 なら未設定で、行の並べ替えでは直前の箱に従う）
    int level = -1;

    // --- インライン注記 ---

    /**
     * Box に紐づくグリフ列。inline_ は Box の先頭からの相対、block は中心線から。
     *  - ownGlyphs が true … この列が Box の本体（縦中横・割注）。clusterIndex のグリフは描かない
     *  - false … クラスタのグリフに加えて描く付随グリフ（ルビ・圏点）
     */
    std::vector<PlacedGlyph> glyphs;
    bool ownGlyphs = false;

    /// この Box が要求するブロック軸の張り出し（既定の ±0.5em を超える分だけ）
    Pt extentMin = 0.0f;
    Pt extentMax = 0.0f;

    /// Penalty: そこで行が終わったときに行末へ足すグリフ（ハイフネーションのハイフン）。inline_ は行末からの相対
    std::vector<PlacedGlyph> breakGlyphs;

    static constexpr uint32_t kNoCluster = std::numeric_limits<uint32_t>::max();

    static LineItem box(Pt w, uint32_t cluster, size_t charIndex) {
        LineItem it;
        it.type = ItemType::Box;
        it.width = w;
        it.clusterIndex = cluster;
        it.charIndex = charIndex;
        return it;
    }
    static LineItem glue(const text::GlueSpec& spec, Pt em, size_t charIndex) {
        return gluePt(spec.natural * em, spec.stretch * em, spec.shrink * em, charIndex);
    }
    static LineItem gluePt(Pt natural, Pt stretch, Pt shrink, size_t charIndex) {
        LineItem it;
        it.type = ItemType::Glue;
        it.natural = natural;
        it.stretch = stretch;
        it.shrink = shrink;
        it.charIndex = charIndex;
        return it;
    }
    static LineItem penaltyItem(float cost, Pt width, size_t charIndex) {
        LineItem it;
        it.type = ItemType::Penalty;
        it.penalty = cost;
        it.width = width;
        it.charIndex = charIndex;
        return it;
    }

    bool isBox() const { return type == ItemType::Box; }
    bool isGlue() const { return type == ItemType::Glue; }
    bool isPenalty() const { return type == ItemType::Penalty; }
    bool isForcedBreak() const { return type == ItemType::Penalty && penalty <= kForcedBreakPenalty; }
};

} // namespace typeset::inl

#endif // TYPESET_INL_LINE_ITEM_HPP
