#ifndef TYPESET_BLOCK_BLOCK_HPP
#define TYPESET_BLOCK_BLOCK_HPP

#include <optional>
#include <variant>
#include <vector>

#include "typeset/dl/display_list.hpp"
#include "typeset/geom.hpp"
#include "typeset/inl/paragraph.hpp"

/**
 * block — ブロック（段落・見出し・罫線・ラベル付き段落・アキ・セクション）
 *
 * Flow はブロック列で、FlowLayouter が Region（段）へ順に流し込む。
 */
namespace typeset::block {

enum class BreakKind : uint8_t { Auto, Column, Page };

/**
 * ブロック共通のスタイル（配置・改ページ制御）
 */
struct BlockStyle {
    Pt spaceBefore = 0.0f;      ///< ブロック前のアキ（行送り方向、pt）
    Pt spaceAfter = 0.0f;
    int orphans = 1;            ///< 段の末尾に残す最小行数
    int widows = 1;             ///< 段の先頭に送る最小行数
    bool keepWithNext = false;  ///< 次のブロックと同じ段に置く（見出し）
    BreakKind breakBefore = BreakKind::Auto;
    BreakKind breakAfter = BreakKind::Auto;
    bool spanColumns = false;   ///< 段抜き（全幅に置く。段組のときだけ意味がある）
};

struct ParagraphBlock {
    inl::Paragraph para;
    BlockStyle block;
};

struct HeadingBlock {
    inl::Paragraph para;
    int level = 1;
    BlockStyle block = [] { BlockStyle b; b.keepWithNext = true; return b; }();
};

/// 罫線（行送り方向に thickness を占め、行の方向いっぱいに引く）
struct RuleBlock {
    Pt thickness = 0.5f;
    Color color{0, 0, 0, 255};
    Pt inset = 0.0f;            ///< 両端を縮める量
    BlockStyle block;
};

struct SpacerBlock {
    Pt size = 0.0f;
    BlockStyle block;
};

/**
 * ラベル付き段落 — 台本の「名前欄＋本文」、箇条書き、用語集
 *
 * ラベルは行頭側に labelWidth の幅で置き、本文はその後ろから始まる。本文の全行が
 * 同じ位置で揃う（行長 = 段の行長 − labelWidth − gap）。本文が段をまたいでも
 * ラベルは先頭側にだけ出る。
 */
struct LabeledBlock {
    inl::Paragraph label;
    std::vector<inl::Paragraph> body;
    Pt labelWidth = 0.0f;
    Pt gap = 0.0f;
    BlockStyle block;
};

/**
 * 以降の段数を切り替える。段の途中なら次のページから効く
 */
struct SectionBlock {
    std::optional<int> columns;
    std::optional<Pt> columnGap;
};

enum class ImagePlacement : uint8_t {
    Block,       ///< 本文の流れに置く（行送り方向に高さぶん消費する）
    FloatStart,  ///< 行頭側に寄せて本文を回り込ませる（横組み: 左、縦組み: 上）
    FloatEnd,    ///< 行末側に寄せて本文を回り込ませる
};

/**
 * 画像ブロック
 *
 * size は物理サイズ（pt）。0 なら画素数を 72dpi として使い、片方だけ 0 なら縦横比を保つ。
 * 段より大きいときは段に収まるよう縮める。
 */
struct ImageBlock {
    std::shared_ptr<const dl::Image> image;
    Size size;
    ImagePlacement placement = ImagePlacement::Block;
    Align align = Align::Center;            ///< Block のときの行方向の揃え
    Pt gap = 6.0f;                          ///< 回り込みの本文との間隔
    std::optional<inl::Paragraph> caption;  ///< 画像の下（行送り方向の後）に置く
    Pt captionGap = 3.0f;
    BlockStyle block;
};

/**
 * 表 — TeX の tabular 水準（列幅指定／自動、罫線、colspan、ヘッダ行の繰り返し、ページまたぎ）
 *
 * 列は行の方向（inline）に並び、行は行送り方向（block）に進む。縦組みでは列が上下、行が右→左。
 */
struct TableColumn {
    Pt width = 0.0f;                ///< 0 = 自動（内容の自然幅から配分）
    Align align = Align::Start;     ///< セル内の揃え（段落の align を上書き）
};

struct TableCell {
    std::vector<inl::Paragraph> paras;
    int colspan = 1;
};

struct TableRow {
    std::vector<TableCell> cells;
    bool header = false;            ///< ヘッダ行（ページをまたぐときに繰り返す）
};

struct TableBorders {
    Pt outer = 0.75f;               ///< 外枠
    Pt inner = 0.25f;               ///< 内側の罫
    Pt headerRule = 0.75f;          ///< ヘッダ行の下
    bool vertical = true;           ///< 縦罫（列の境）を引く
    bool horizontal = true;         ///< 横罫（行の境）を引く
    Color color{0, 0, 0, 255};
};

struct TableBlock {
    std::vector<TableColumn> columns;   ///< 空なら行のセル数から自動列数（全部自動幅）
    std::vector<TableRow> rows;
    TableBorders borders;
    Pt cellPadding = 3.0f;
    bool fullWidth = true;              ///< 段の幅いっぱいに広げる（false なら自然幅で align）
    Align align = Align::Start;
    bool repeatHeader = true;
    BlockStyle block;
};

using Block = std::variant<ParagraphBlock, HeadingBlock, RuleBlock, SpacerBlock, LabeledBlock,
                           SectionBlock, ImageBlock, TableBlock>;

struct Flow {
    std::vector<Block> blocks;

    void add(Block b) { blocks.push_back(std::move(b)); }
    void addParagraph(inl::Paragraph p, BlockStyle style = {}) {
        blocks.push_back(ParagraphBlock{std::move(p), std::move(style)});
    }
    void addHeading(inl::Paragraph p, int level = 1, std::optional<BlockStyle> style = std::nullopt) {
        HeadingBlock h;
        h.para = std::move(p);
        h.level = level;
        if (style) h.block = *style;
        blocks.push_back(std::move(h));
    }
    void addLabeled(inl::Paragraph label, inl::Paragraph body, Pt labelWidth, Pt gap = 0.0f,
                    BlockStyle style = {}) {
        LabeledBlock b;
        b.label = std::move(label);
        b.body.push_back(std::move(body));
        b.labelWidth = labelWidth;
        b.gap = gap;
        b.block = std::move(style);
        blocks.push_back(std::move(b));
    }
    void addRule(Pt thickness = 0.5f, Color color = Color{0, 0, 0, 255}, BlockStyle style = {}) {
        RuleBlock r;
        r.thickness = thickness;
        r.color = color;
        r.block = std::move(style);
        blocks.push_back(std::move(r));
    }
    void addSpacer(Pt size) { blocks.push_back(SpacerBlock{size, {}}); }
    void addImage(ImageBlock img) { blocks.push_back(std::move(img)); }
    void addTable(TableBlock table) { blocks.push_back(std::move(table)); }
    void addPageBreak() {
        SpacerBlock s;
        s.block.breakBefore = BreakKind::Page;
        blocks.push_back(std::move(s));
    }
    void addColumnBreak() {
        SpacerBlock s;
        s.block.breakBefore = BreakKind::Column;
        blocks.push_back(std::move(s));
    }
};

} // namespace typeset::block

#endif // TYPESET_BLOCK_BLOCK_HPP
