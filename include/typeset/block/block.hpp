#ifndef TYPESET_BLOCK_BLOCK_HPP
#define TYPESET_BLOCK_BLOCK_HPP

#include <map>
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
    bool keepTogether = false;  ///< ブロックを段の境で分けない（段より大きければ分ける）
    BreakKind breakBefore = BreakKind::Auto;
    BreakKind breakAfter = BreakKind::Auto;
    bool spanColumns = false;   ///< 段抜き（全幅に置く。段組のときだけ意味がある）

    /// 相互参照のラベル。本文の `{ref:ラベル}` が番号（見出し・図・表）、`{page:ラベル}` がページ番号になる
    std::string label;

    /// 背景（段落・コードブロック用）。padding は背景の内側の余白（行送り方向）
    std::optional<Color> background;
    Pt padding = 0.0f;
};

struct ParagraphBlock {
    inl::Paragraph para;
    BlockStyle block;
};

struct HeadingBlock {
    inl::Paragraph para;
    int level = 1;
    /// 自動採番（レベル 1: "1."、レベル 2 以降: "1.1"）を見出しの前に付ける
    bool numbered = false;
    /// PDF のしおりに入れる
    bool bookmark = true;
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
    /// 画像の下（行送り方向の後）に置く。テキストの `{fig}` は図番号になる
    std::optional<inl::Paragraph> caption;
    Pt captionGap = 3.0f;
    BlockStyle block;
};

/**
 * 外部ハンドラで生成するオブジェクトのブロック（別行立ての数式・グラフなど）
 *
 * 組版時に obj::ObjectRegistry のハンドラを呼び、返った箱を行方向に揃えて置く。段より大きければ縮める。
 * numbered なら式番号（FlowLayoutOptions::equationFormat）を行末に置き、label で `{ref:label}` から参照できる。
 * caption の `{eq}` も式番号になる。textStyle は式番号・代替テキストの書体で、fontSize の基準にもなる。
 */
struct ObjectBlock {
    std::string handler;
    std::u16string source;
    std::map<std::string, std::string> params;
    Align align = Align::Center;
    bool numbered = false;
    TextStyle textStyle;
    std::optional<inl::Paragraph> caption;
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

enum class VAlign : uint8_t { Top, Middle, Bottom };

struct TableCell {
    std::vector<inl::Paragraph> paras;
    int colspan = 1;
    int rowspan = 1;    ///< 下の行へまたぐ。またいだ行はページをまたがない（まとめて次の段へ）
    VAlign valign = VAlign::Top;
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
    /// 表の上に置くキャプション。テキストの `{table}` は表番号になる
    std::optional<inl::Paragraph> caption;
    Pt captionGap = 3.0f;
    BlockStyle block;
};

/**
 * 箇条書き（ラベル付き段落の列）
 */
struct ListBlock {
    enum class Marker : uint8_t { Bullet, Numbered };
    std::vector<inl::Paragraph> items;
    Marker marker = Marker::Bullet;
    std::u16string bullet = u"・";
    std::u16string numberSuffix = u".";     ///< Numbered のとき "1." の "."
    Pt labelWidth = 0.0f;                   ///< 0 なら 1.5em（本文サイズ基準）
    Pt gap = 0.0f;
    Pt itemGap = 0.0f;                      ///< 項目間のアキ
    BlockStyle block;
};

/**
 * 目次。前のパスで集めた見出し（番号付き）とページ番号を並べる
 */
struct TocBlock {
    int maxLevel = 2;
    TextStyle style;                        ///< 項目の文字（level 1）
    std::optional<TextStyle> subStyle;      ///< level 2 以降（無ければ style）
    Pt indentPerLevel = 0.0f;               ///< 0 なら 1em
    Pt lineHeight = 1.8f;
    bool leader = true;                     ///< 点線
    BlockStyle block;
};

/**
 * 索引。本文中の `{index:用語}` / `{index:よみ|用語}` を集め、読みの順（かな: 五十音、欧文: A–Z）に
 * 用語とページ番号を並べる。目次と同じく前のパスで集めた情報を使う（2〜3 パス）
 */
struct IndexBlock {
    TextStyle style;                        ///< 項目の文字
    std::optional<TextStyle> groupStyle;    ///< 見出し文字（あ・か・さ… / A・B…）。無ければ style
    bool grouped = true;                    ///< 行（あ・か・さ…）ごとに見出しを入れる
    bool leader = true;                     ///< 点線
    Pt lineHeight = 1.6f;
    std::u16string pageSeparator = u", ";
    BlockStyle block;
};

using Block = std::variant<ParagraphBlock, HeadingBlock, RuleBlock, SpacerBlock, LabeledBlock,
                           SectionBlock, ImageBlock, TableBlock, ListBlock, TocBlock, ObjectBlock, IndexBlock>;

struct Flow {
    std::vector<Block> blocks;

    void add(Block b) { blocks.push_back(std::move(b)); }
    void addParagraph(inl::Paragraph p, BlockStyle style = {}) {
        blocks.push_back(ParagraphBlock{std::move(p), std::move(style)});
    }
    void addHeading(inl::Paragraph p, int level = 1, std::optional<BlockStyle> style = std::nullopt,
                    bool numbered = false) {
        HeadingBlock h;
        h.para = std::move(p);
        h.level = level;
        h.numbered = numbered;
        if (style) h.block = *style;
        blocks.push_back(std::move(h));
    }
    void addList(ListBlock list) { blocks.push_back(std::move(list)); }
    void addToc(TocBlock toc) { blocks.push_back(std::move(toc)); }
    void addIndex(IndexBlock idx) { blocks.push_back(std::move(idx)); }
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
    void addObject(ObjectBlock obj) { blocks.push_back(std::move(obj)); }
    void addObject(std::string handler, std::u16string source, TextStyle textStyle,
                   std::map<std::string, std::string> params = {}, bool numbered = false,
                   BlockStyle style = {}) {
        ObjectBlock o;
        o.handler = std::move(handler);
        o.source = std::move(source);
        o.params = std::move(params);
        o.textStyle = std::move(textStyle);
        o.numbered = numbered;
        o.block = std::move(style);
        blocks.push_back(std::move(o));
    }
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
