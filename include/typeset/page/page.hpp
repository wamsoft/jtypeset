#ifndef TYPESET_PAGE_PAGE_HPP
#define TYPESET_PAGE_PAGE_HPP

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "typeset/dl/display_list.hpp"
#include "typeset/geom.hpp"
#include "typeset/inl/paragraph.hpp"
#include "typeset/writing_mode.hpp"

/**
 * page — ページマスタ・ページ列・段（Region）
 */
namespace typeset::page {

namespace paper {
constexpr Size A4{210.0f * kMm, 297.0f * kMm};
constexpr Size A5{148.0f * kMm, 210.0f * kMm};
constexpr Size B5{182.0f * kMm, 257.0f * kMm};
constexpr Size B6{128.0f * kMm, 182.0f * kMm};
constexpr Size Bunko{105.0f * kMm, 148.0f * kMm};     ///< 文庫（A6）
constexpr Size Shinsho{103.0f * kMm, 182.0f * kMm};   ///< 新書
inline Size landscape(Size s) { return Size{s.h, s.w}; }
} // namespace paper

struct Margins {
    Pt top = 20.0f * kMm;
    Pt bottom = 20.0f * kMm;
    Pt inner = 20.0f * kMm;     ///< ノド側（綴じ側）
    Pt outer = 20.0f * kMm;     ///< 小口側
};

/**
 * 柱・ノンブル（横組みで版面の上／下に置く。縦組みの本でも横に置くのが慣例）
 *
 * テキスト中の `{page}` `{pages}` はページ番号／総ページ数、その他の `{name}` は
 * FlowLayouter に渡す fields で置換する。揃えは para.style.align（Start / Center / End）。
 */
struct RunningText {
    inl::Paragraph para;
    /// 版面の端から行の中心線までの距離（上の柱は版面上端から上へ、下は下端から下へ）
    Pt offset = 0.0f;
};

struct PageMaster {
    Size size = paper::A5;
    Margins margin;
    WritingMode writingMode = WritingMode::VerticalRl;
    int columns = 1;
    Pt columnGap = 0.0f;
    std::optional<RunningText> header;
    std::optional<RunningText> footer;
    /// 見開きで内・外の余白を左右ページで入れ替える
    bool duplex = false;

    /// 版面（pageNumber は 1 始まり。duplex のとき偶奇で inner/outer が入れ替わる）
    Rect bodyRect(int pageNumber) const;

    /// 版面を段に分ける（縦組みは上下、横組みは左右）
    std::vector<Rect> columnRects(const Rect& body, int cols, Pt gap) const;
};

struct PageSequence {
    PageMaster master;
    int firstPageNumber = 1;
};

/**
 * 段 — 流し込み先
 */
struct Region {
    Rect area;
    WritingMode writingMode = WritingMode::HorizontalTb;
    Pt used = 0.0f;             ///< 行送り方向に使った量
    /// 回り込みの排除領域（ページ座標）。段が変わると消える
    std::vector<Rect> exclusions;

    /// 行の長さ（縦組みなら段の高さ、横組みなら幅）
    Pt lineLength() const { return isVertical(writingMode) ? area.h : area.w; }
    /// 行送り方向の全長
    Pt blockExtent() const { return isVertical(writingMode) ? area.w : area.h; }
    Pt remaining() const { return blockExtent() - used; }
    bool fresh() const { return used <= 0.0f; }

    /// used + offset の位置に始まる、行送り pitch の 1 行目の行頭（中心線上の点）
    Point lineOrigin(Pt blockOffset, Pt pitch, Pt inlineOffset = 0.0f) const;

    /**
     * 論理矩形 → 物理矩形
     * @param inline0,inline1 行の方向の範囲（段の行頭からの距離）
     * @param block0,block1 行送り方向の範囲（段の始端からの距離。used は含まない）
     */
    Rect toRect(Pt inline0, Pt inline1, Pt block0, Pt block1) const;

    /**
     * 行送り方向 [block0, block1) の帯で、排除領域を避けて使える行の方向の最大区間
     */
    void freeInlineRange(Pt block0, Pt block1, Pt& start, Pt& end) const;
};

/**
 * 段の排除領域を避ける行の形（回り込み）。行 i は startBlock + i × pitch の帯
 */
class RegionLineShape : public inl::LineShapeProvider {
public:
    RegionLineShape(const Region& region, Pt startBlock, Pt pitch, Pt baseIndent = 0.0f)
        : region_(region), startBlock_(startBlock), pitch_(pitch), baseIndent_(baseIndent) {}
    inl::LineShape at(int lineIndex) const override { return at(lineIndex, 0.0f); }
    inl::LineShape at(int lineIndex, Pt extraBlock) const override;
private:
    const Region& region_;
    Pt startBlock_;
    Pt pitch_;
    Pt baseIndent_;
};

/**
 * 出来上がったページ
 */
struct Page {
    int number = 1;
    dl::DisplayList dl;
};

} // namespace typeset::page

#endif // TYPESET_PAGE_PAGE_HPP
