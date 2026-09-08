/**
 * flow_layouter.cpp — Flow → ページ列
 */

#include "typeset/page/flow_layouter.hpp"

#include <algorithm>
#include <cmath>

#include "typeset/text/utf.hpp"

namespace typeset::page {

namespace {

constexpr float kEps = 0.01f;

/// `{name}` を置換した段落を返す
inl::Paragraph substitute(const inl::Paragraph& src,
                          const std::map<std::u16string, std::u16string>& fields) {
    inl::Paragraph out = src;
    for (inl::InlineRun& run : out.runs) {
        std::u16string& t = run.text;
        size_t pos = 0;
        while ((pos = t.find(u'{', pos)) != std::u16string::npos) {
            const size_t close = t.find(u'}', pos);
            if (close == std::u16string::npos) break;
            const std::u16string key = t.substr(pos + 1, close - pos - 1);
            auto it = fields.find(key);
            if (it != fields.end()) {
                t.replace(pos, close - pos + 1, it->second);
                pos += it->second.size();
            } else {
                pos = close + 1;
            }
        }
    }
    return out;
}

std::u16string toU16(int v) {
    return text::utf8ToUtf16(std::to_string(v));
}

const block::BlockStyle* styleOf(const block::Block& blk) {
    if (const auto* p = std::get_if<block::ParagraphBlock>(&blk)) return &p->block;
    if (const auto* h = std::get_if<block::HeadingBlock>(&blk)) return &h->block;
    if (const auto* r = std::get_if<block::RuleBlock>(&blk)) return &r->block;
    if (const auto* s = std::get_if<block::SpacerBlock>(&blk)) return &s->block;
    if (const auto* l = std::get_if<block::LabeledBlock>(&blk)) return &l->block;
    if (const auto* i = std::get_if<block::ImageBlock>(&blk)) return &i->block;
    if (const auto* t = std::get_if<block::TableBlock>(&blk)) return &t->block;
    return nullptr;
}

/**
 * 流し込みの状態
 */
class Flower {
public:
    Flower(font::FontSet& fonts, const block::Flow& flow, const PageSequence& seq,
           const FlowLayoutOptions& opts)
        : fonts_(fonts), layouter_(fonts), flow_(flow), seq_(seq), opts_(opts),
          columns_(seq.master.columns), columnGap_(seq.master.columnGap) {}

    std::vector<Page> run(int totalPagesHint);

private:
    font::FontSet& fonts_;
    inl::ParagraphLayouter layouter_;
    const block::Flow& flow_;
    const PageSequence& seq_;
    const FlowLayoutOptions& opts_;

    int columns_;
    Pt columnGap_;
    std::optional<int> pendingColumns_;
    std::optional<Pt> pendingGap_;

    std::vector<Page> pages_;
    std::vector<Region> regions_;   ///< 現在のページの段
    size_t regionIndex_ = 0;
    bool pageHasContent_ = false;
    int totalPagesHint_ = 0;

    /// keepWithNext で保留中のブロック（この段に置いたが、次が入らなければ一緒に移す）
    struct Pending {
        const block::Block* blk = nullptr;
        size_t regionIndex = 0;
        size_t itemStart = 0;   ///< その段のページ dl に足した位置
        Pt usedBefore = 0.0f;
    };
    std::optional<Pending> pending_;

    const PageMaster& master() const { return seq_.master; }
    WritingMode wm() const { return master().writingMode; }
    Page& page() { return pages_.back(); }
    Region& region() { return regions_[regionIndex_]; }

    void newPage();
    void finishPage();
    bool nextRegion();          ///< 次の段へ。無ければ新しいページ
    void ensureRegion();

    void placeBlock(const block::Block& blk);
    void placeContent(const block::Block& blk, const block::BlockStyle& style);
    void placeParagraphs(const std::vector<const inl::Paragraph*>& paras, const block::BlockStyle& style,
                         const inl::Paragraph* label, Pt labelWidth, Pt labelGap);
    void placeRule(const block::RuleBlock& r);
    void placeImage(const block::ImageBlock& img);
    void placeTable(const block::TableBlock& table);
    void drawGuides();
    void applyBreakBefore(const block::BlockStyle& style);
    void applySpaceBefore(Pt space);
    void applySpaceAfter(Pt space);

    /// 段落 1 つを現在の段以降に流す。ラベル付きなら本文の行長を縮める
    void flowParagraph(const inl::Paragraph& para, const block::BlockStyle& style,
                       const inl::Paragraph* label, Pt labelWidth, Pt labelGap, bool firstOfBlock);
    int linesThatFit(Pt pitch) const;
    void layoutRunning(const RunningText& rt, bool top);

    /// 論理座標の矩形を塗る（罫線用）
    void fillLogicalRect(Pt inline0, Pt inline1, Pt block0, Pt block1, Color color);
    /// 段落を指定の行長・位置で全部組んで置く（表のセル・キャプション用）。消費した行送り方向の量を返す
    Pt placeParagraphAt(const inl::Paragraph& para, Pt blockOffset, Pt inlineOffset, Pt lineLength,
                        std::optional<Align> alignOverride = std::nullopt);
    /// 段落を組んだときの行送り方向の量（置かない）
    Pt measureParagraph(const inl::Paragraph& para, Pt lineLength, Pt* naturalMax = nullptr);
};

//------------------------------------------------------------------------------

void Flower::newPage() {
    if (pendingColumns_) { columns_ = *pendingColumns_; pendingColumns_.reset(); }
    if (pendingGap_) { columnGap_ = *pendingGap_; pendingGap_.reset(); }

    Page p;
    p.number = seq_.firstPageNumber + static_cast<int>(pages_.size());
    p.dl.page = master().size;
    pages_.push_back(std::move(p));

    const Rect body = master().bodyRect(page().number);
    regions_.clear();
    for (const Rect& r : master().columnRects(body, columns_, columnGap_)) {
        Region reg;
        reg.area = r;
        reg.writingMode = wm();
        regions_.push_back(reg);
    }
    regionIndex_ = 0;
    pageHasContent_ = false;
    pending_.reset();
}

void Flower::finishPage() {
    if (pages_.empty()) return;
    if (opts_.drawGuides) drawGuides();
    if (master().header) layoutRunning(*master().header, true);
    if (master().footer) layoutRunning(*master().footer, false);
}

bool Flower::nextRegion() {
    if (regionIndex_ + 1 < regions_.size()) {
        ++regionIndex_;
        return true;
    }
    finishPage();
    newPage();
    return false;
}

void Flower::ensureRegion() {
    if (pages_.empty()) newPage();
}

void Flower::drawGuides() {
    Stroke s;
    s.color = Color::rgba(0, 150, 0, 80);
    s.width = 0.3f;
    for (const Region& r : regions_) {
        Path p;
        p.addRect(r.area);
        page().dl.addPath(p, std::nullopt, s);
    }
}

void Flower::layoutRunning(const RunningText& rt, bool top) {
    std::map<std::u16string, std::u16string> fields = opts_.fields;
    fields[u"page"] = toU16(page().number);
    fields[u"pages"] = toU16(std::max(totalPagesHint_, static_cast<int>(pages_.size())));
    const inl::Paragraph para = substitute(rt.para, fields);

    const Rect body = master().bodyRect(page().number);
    // 柱・ノンブルは横組み。版面の幅いっぱいを行長にして揃えで位置を決める
    inl::Paragraph p = para;
    p.style.firstLineIndent = 0.0f;
    if (p.style.align == Align::Justify) p.style.align = Align::Start;
    const inl::ConstantLineShape shape(body.w);
    const inl::ParagraphFragment frag = layouter_.layout(p, WritingMode::HorizontalTb, shape);
    if (frag.lines.empty()) return;
    const Pt y = top ? body.y - rt.offset : body.bottom() + rt.offset;
    inl::emitParagraph(page().dl, frag, WritingMode::HorizontalTb, Point{body.x, y});
}

//------------------------------------------------------------------------------

void Flower::applyBreakBefore(const block::BlockStyle& style) {
    ensureRegion();
    if (style.breakBefore == block::BreakKind::Page) {
        if (pageHasContent_) {
            finishPage();
            newPage();
        }
    } else if (style.breakBefore == block::BreakKind::Column) {
        if (!region().fresh()) nextRegion();
    }
}

void Flower::applySpaceBefore(Pt space) {
    if (space <= 0.0f) return;
    if (region().fresh()) return;   // 段の先頭ではアキを入れない
    region().used += space;
}

void Flower::applySpaceAfter(Pt space) {
    if (space <= 0.0f) return;
    region().used += space;
}

int Flower::linesThatFit(Pt pitch) const {
    if (pitch <= 0.0f) return 0;
    const Region& r = regions_[regionIndex_];
    return static_cast<int>(std::floor((r.remaining() + kEps) / pitch));
}

void Flower::fillLogicalRect(Pt inline0, Pt inline1, Pt block0, Pt block1, Color color) {
    const Rect r = region().toRect(inline0, inline1, block0, block1);
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    page().dl.addRect(r, color);
}

Pt Flower::measureParagraph(const inl::Paragraph& para, Pt lineLength, Pt* naturalMax) {
    const inl::ConstantLineShape shape(std::max(1.0f, lineLength));
    const inl::ParagraphFragment frag = layouter_.layout(para, wm(), shape);
    if (naturalMax) {
        Pt m = 0.0f;
        for (const inl::LineBox& l : frag.lines) m = std::max(m, l.naturalLength + l.indent);
        *naturalMax = m;
    }
    return frag.linePitch * static_cast<float>(frag.lines.size());
}

Pt Flower::placeParagraphAt(const inl::Paragraph& para, Pt blockOffset, Pt inlineOffset,
                            Pt lineLength, std::optional<Align> alignOverride) {
    inl::Paragraph p = para;
    if (alignOverride) p.style.align = *alignOverride;
    const inl::ConstantLineShape shape(std::max(1.0f, lineLength));
    const inl::ParagraphFragment frag = layouter_.layout(p, wm(), shape);
    if (frag.lines.empty()) return 0.0f;
    const Point origin = region().lineOrigin(blockOffset, frag.linePitch, inlineOffset);
    inl::emitParagraph(page().dl, frag, wm(), origin);
    return frag.linePitch * static_cast<float>(frag.lines.size());
}

//------------------------------------------------------------------------------

void Flower::placeBlock(const block::Block& blk) {
    if (const auto* sec = std::get_if<block::SectionBlock>(&blk)) {
        if (sec->columns) pendingColumns_ = *sec->columns;
        if (sec->columnGap) pendingGap_ = *sec->columnGap;
        ensureRegion();
        if (pageHasContent_) {
            // 段数の変更はページ単位。内容があれば改ページする
            finishPage();
            newPage();
        } else {
            // まだ何も置いていなければ今のページから効かせる
            pages_.pop_back();
            newPage();
        }
        return;
    }

    const block::BlockStyle* style = styleOf(blk);
    if (!style) return;

    applyBreakBefore(*style);

    // 段抜き: 段組のページに内容があれば改ページして先頭に置き、その下で段を再開する
    // （Phase 2 の制限。段のバランス取りは後回し）
    if (style->spanColumns && columns_ > 1) {
        if (pageHasContent_) {
            finishPage();
            newPage();
        }
        const Rect body = master().bodyRect(page().number);
        std::vector<Region> saved = regions_;
        Region full;
        full.area = body;
        full.writingMode = wm();
        regions_ = {full};
        regionIndex_ = 0;

        placeContent(blk, *style);

        const Pt consumed = regions_[0].used;
        regions_ = saved;
        for (Region& reg : regions_) {
            if (isVertical(wm())) {
                reg.area.w = std::max(0.0f, reg.area.w - consumed);
                if (wm() == WritingMode::VerticalLr) reg.area.x += consumed;
            } else {
                reg.area.y += consumed;
                reg.area.h = std::max(0.0f, reg.area.h - consumed);
            }
        }
        regionIndex_ = 0;
        pageHasContent_ = true;
        pending_.reset();
        return;
    }

    placeContent(blk, *style);

    if (style->breakAfter == block::BreakKind::Page) {
        finishPage();
        newPage();
    } else if (style->breakAfter == block::BreakKind::Column) {
        nextRegion();
    }

    // keepWithNext: flowParagraph が作った保留にブロックを結び付ける。次の段落が同じ段に
    // 入らなければ、このブロックごと次の段へ移す
    if (style->keepWithNext && pending_ && !pending_->blk) {
        pending_->blk = &blk;
    } else if (!style->keepWithNext) {
        pending_.reset();
    }
}

void Flower::placeContent(const block::Block& blk, const block::BlockStyle& style) {
    if (const auto* p = std::get_if<block::ParagraphBlock>(&blk)) {
        placeParagraphs({&p->para}, style, nullptr, 0.0f, 0.0f);
    } else if (const auto* h = std::get_if<block::HeadingBlock>(&blk)) {
        placeParagraphs({&h->para}, style, nullptr, 0.0f, 0.0f);
    } else if (const auto* l = std::get_if<block::LabeledBlock>(&blk)) {
        std::vector<const inl::Paragraph*> paras;
        for (const inl::Paragraph& b : l->body) paras.push_back(&b);
        placeParagraphs(paras, style, &l->label, l->labelWidth, l->gap);
    } else if (const auto* r = std::get_if<block::RuleBlock>(&blk)) {
        placeRule(*r);
    } else if (const auto* s = std::get_if<block::SpacerBlock>(&blk)) {
        applySpaceBefore(style.spaceBefore);
        if (s->size > 0.0f && !region().fresh()) region().used += s->size;
        applySpaceAfter(style.spaceAfter);
    } else if (const auto* img = std::get_if<block::ImageBlock>(&blk)) {
        placeImage(*img);
    } else if (const auto* t = std::get_if<block::TableBlock>(&blk)) {
        placeTable(*t);
    }
}

//------------------------------------------------------------------------------
// 罫線
//------------------------------------------------------------------------------

void Flower::placeRule(const block::RuleBlock& r) {
    applySpaceBefore(r.block.spaceBefore);
    if (region().remaining() < r.thickness) nextRegion();
    Region& reg = region();
    const Pt L = reg.lineLength();
    fillLogicalRect(r.inset, L - r.inset, reg.used, reg.used + r.thickness, r.color);
    reg.used += r.thickness;
    pageHasContent_ = true;
    pending_.reset();
    applySpaceAfter(r.block.spaceAfter);
}

//------------------------------------------------------------------------------
// 画像
//------------------------------------------------------------------------------

void Flower::placeImage(const block::ImageBlock& img) {
    if (!img.image || img.image->width <= 0 || img.image->height <= 0) return;
    ensureRegion();
    applySpaceBefore(img.block.spaceBefore);

    // 物理サイズを決める
    const float pw = static_cast<float>(img.image->width);
    const float ph = static_cast<float>(img.image->height);
    Pt w = img.size.w, h = img.size.h;
    if (w <= 0.0f && h <= 0.0f) { w = pw; h = ph; }
    else if (w <= 0.0f) w = h * pw / ph;
    else if (h <= 0.0f) h = w * ph / pw;

    const bool vertical = isVertical(wm());
    auto inlineExtent = [&]() { return vertical ? h : w; };
    auto blockExtent = [&]() { return vertical ? w : h; };

    // 段に収まるよう縮める
    {
        const Region& reg = region();
        const float s = std::min(1.0f, std::min(reg.lineLength() / std::max(1.0f, inlineExtent()),
                                                reg.blockExtent() / std::max(1.0f, blockExtent())));
        if (s < 1.0f) { w *= s; h *= s; }
    }

    // キャプションの高さ
    Pt captionExtent = 0.0f;
    if (img.caption) {
        captionExtent = img.captionGap + measureParagraph(*img.caption, inlineExtent());
    }
    const Pt total = blockExtent() + captionExtent;

    if (region().remaining() + kEps < total && !region().fresh()) nextRegion();
    Region& reg = region();
    const Pt L = reg.lineLength();

    Pt in0 = 0.0f;
    switch (img.placement) {
    case block::ImagePlacement::Block:
        if (img.align == Align::Center) in0 = (L - inlineExtent()) * 0.5f;
        else if (img.align == Align::End) in0 = L - inlineExtent();
        break;
    case block::ImagePlacement::FloatStart: in0 = 0.0f; break;
    case block::ImagePlacement::FloatEnd:   in0 = L - inlineExtent(); break;
    }
    in0 = std::max(0.0f, in0);

    // 画像本体
    const Rect rect = reg.toRect(in0, in0 + inlineExtent(), reg.used, reg.used + blockExtent());
    dl::ImageItem item;
    item.image = img.image;
    item.xform = multiply(Matrix::translation(rect.x, rect.y),
                          Matrix::scaling(rect.w / pw, rect.h / ph));
    page().dl.add(item);

    // キャプション
    if (img.caption) {
        placeParagraphAt(*img.caption, blockExtent() + img.captionGap, in0, inlineExtent(),
                         img.caption->style.align == Align::Justify ? std::optional<Align>(Align::Center)
                                                                    : std::nullopt);
    }

    pageHasContent_ = true;
    pending_.reset();

    if (img.placement == block::ImagePlacement::Block) {
        reg.used += total;
        applySpaceAfter(img.block.spaceAfter);
    } else {
        // 回り込み: 本文との間隔ぶん広げた排除領域を登録し、行送りは進めない
        const Pt exIn0 = (img.placement == block::ImagePlacement::FloatEnd) ? in0 - img.gap : in0;
        const Pt exIn1 = (img.placement == block::ImagePlacement::FloatStart) ? in0 + inlineExtent() + img.gap
                                                                              : in0 + inlineExtent();
        reg.exclusions.push_back(reg.toRect(exIn0, exIn1, reg.used, reg.used + total + img.gap));
    }
}

//------------------------------------------------------------------------------
// 表
//------------------------------------------------------------------------------

void Flower::placeTable(const block::TableBlock& table) {
    if (table.rows.empty()) return;
    ensureRegion();
    applySpaceBefore(table.block.spaceBefore);

    // 列数
    size_t ncol = table.columns.size();
    for (const block::TableRow& row : table.rows) {
        size_t n = 0;
        for (const block::TableCell& c : row.cells) n += static_cast<size_t>(std::max(1, c.colspan));
        ncol = std::max(ncol, n);
    }
    if (ncol == 0) return;
    std::vector<block::TableColumn> columns = table.columns;
    columns.resize(ncol);

    const Pt pad = table.cellPadding;
    const Pt L = region().lineLength();

    // 列幅: 固定はそのまま、自動は内容の自然幅から
    std::vector<Pt> widths(ncol, 0.0f);
    std::vector<Pt> natural(ncol, 0.0f);   // 折り返さないときの幅
    std::vector<Pt> minimum(ncol, 0.0f);   // 割れない最長の語の幅
    for (const block::TableRow& row : table.rows) {
        size_t ci = 0;
        for (const block::TableCell& cell : row.cells) {
            const int span = std::max(1, cell.colspan);
            if (span == 1 && ci < ncol && columns[ci].width <= 0.0f) {
                Pt nat = 0.0f, mn = 0.0f;
                for (const inl::Paragraph& p : cell.paras) {
                    Pt a = 0.0f, b2 = 0.0f;
                    measureParagraph(p, 1.0e6f, &a);
                    measureParagraph(p, 1.0f, &b2);   // 1pt で組むと 1 行が 1 つの割れない塊になる
                    nat = std::max(nat, a);
                    mn = std::max(mn, b2);
                }
                natural[ci] = std::max(natural[ci], nat + 2.0f * pad);
                minimum[ci] = std::max(minimum[ci], mn + 2.0f * pad);
            }
            ci += static_cast<size_t>(span);
        }
    }
    Pt fixedSum = 0.0f;
    std::vector<bool> isAuto(ncol, false);
    for (size_t i = 0; i < ncol; ++i) {
        if (columns[i].width > 0.0f) { widths[i] = columns[i].width; fixedSum += widths[i]; }
        else { widths[i] = std::max(natural[i], 2.0f * pad + 1.0f); isAuto[i] = true; }
    }
    {
        // 自動列を available に合わせて比例配分する。最小幅を割る列は最小幅に固定して
        // 残りで配分し直す（数回で収束する）
        Pt available = std::max(1.0f, L - fixedSum);
        for (int iter = 0; iter < 8; ++iter) {
            Pt autoSum = 0.0f;
            int autoCount = 0;
            for (size_t i = 0; i < ncol; ++i) if (isAuto[i]) { autoSum += natural[i]; ++autoCount; }
            if (autoCount == 0 || autoSum <= 0.0f) break;
            float scale = 1.0f;
            if (autoSum > available) scale = available / autoSum;                 // 収まらない: 縮める
            else if (table.fullWidth) scale = available / autoSum;                // 全幅に広げる
            bool changed = false;
            for (size_t i = 0; i < ncol; ++i) {
                if (!isAuto[i]) continue;
                const Pt w = natural[i] * scale;
                if (w < minimum[i] && scale < 1.0f) {
                    widths[i] = minimum[i];
                    isAuto[i] = false;
                    available = std::max(1.0f, available - minimum[i]);
                    changed = true;
                } else {
                    widths[i] = w;
                }
            }
            if (!changed) break;
        }
    }
    Pt tableWidth = 0.0f;
    for (Pt w : widths) tableWidth += w;
    Pt tableStart = 0.0f;
    if (!table.fullWidth && tableWidth < L) {
        if (table.align == Align::Center) tableStart = (L - tableWidth) * 0.5f;
        else if (table.align == Align::End) tableStart = L - tableWidth;
    }
    std::vector<Pt> colStart(ncol + 1, tableStart);
    for (size_t i = 0; i < ncol; ++i) colStart[i + 1] = colStart[i] + widths[i];

    // ヘッダ行（繰り返し用）
    std::vector<const block::TableRow*> headerRows;
    for (const block::TableRow& row : table.rows) if (row.header) headerRows.push_back(&row);

    const block::TableBorders& b = table.borders;
    auto hRule = [&](Pt blockPos, Pt thickness) {
        if (!b.horizontal || thickness <= 0.0f) return;
        fillLogicalRect(colStart.front() - b.outer * 0.5f, colStart.back() + b.outer * 0.5f,
                        blockPos - thickness * 0.5f, blockPos + thickness * 0.5f, b.color);
    };
    auto vRules = [&](Pt block0, Pt block1, const block::TableRow& row) {
        if (!b.vertical) return;
        // 外枠
        fillLogicalRect(colStart.front() - b.outer * 0.5f, colStart.front() + b.outer * 0.5f, block0, block1, b.color);
        fillLogicalRect(colStart.back() - b.outer * 0.5f, colStart.back() + b.outer * 0.5f, block0, block1, b.color);
        // 内側（colspan の内部境界は引かない）
        size_t ci = 0;
        for (const block::TableCell& cell : row.cells) {
            ci += static_cast<size_t>(std::max(1, cell.colspan));
            if (ci < ncol) {
                fillLogicalRect(colStart[ci] - b.inner * 0.5f, colStart[ci] + b.inner * 0.5f, block0, block1, b.color);
            }
        }
    };

    // 1 行を置く。入らなければ false（呼び出し側が段を進める）
    auto placeRow = [&](const block::TableRow& row, bool first, bool afterHeader, bool force) -> bool {
        Region& reg = region();
        // セルの高さを測る
        Pt rowExtent = 0.0f;
        size_t ci = 0;
        for (const block::TableCell& cell : row.cells) {
            const int span = std::max(1, cell.colspan);
            const size_t end = std::min(ncol, ci + static_cast<size_t>(span));
            const Pt cellWidth = colStart[end] - colStart[ci] - 2.0f * pad;
            Pt ext = 2.0f * pad;
            for (const inl::Paragraph& p : cell.paras) ext += measureParagraph(p, cellWidth);
            rowExtent = std::max(rowExtent, ext);
            ci = end;
        }
        const Pt ruleAbove = first ? b.outer : (afterHeader ? b.headerRule : b.inner);
        if (!force && reg.remaining() + kEps < rowExtent + ruleAbove && !reg.fresh()) return false;

        const Pt top = reg.used;
        hRule(top, ruleAbove);
        ci = 0;
        for (const block::TableCell& cell : row.cells) {
            const int span = std::max(1, cell.colspan);
            const size_t end = std::min(ncol, ci + static_cast<size_t>(span));
            const Pt cellWidth = colStart[end] - colStart[ci] - 2.0f * pad;
            Pt off = pad;
            for (const inl::Paragraph& p : cell.paras) {
                off += placeParagraphAt(p, off, colStart[ci] + pad, cellWidth,
                                        columns[ci].align == Align::Start ? std::nullopt
                                                                          : std::optional<Align>(columns[ci].align));
            }
            ci = end;
        }
        vRules(top, top + rowExtent, row);
        reg.used += rowExtent;
        pageHasContent_ = true;
        return true;
    };

    bool first = true;
    bool prevHeader = false;
    for (size_t ri = 0; ri < table.rows.size(); ++ri) {
        const block::TableRow& row = table.rows[ri];
        if (!placeRow(row, first, prevHeader, false)) {
            // 段の末尾で閉じて次の段へ。ヘッダ行を繰り返す
            hRule(region().used, b.outer);
            nextRegion();
            first = true;
            prevHeader = false;
            if (table.repeatHeader && !row.header) {
                for (const block::TableRow* hr : headerRows) {
                    placeRow(*hr, first, false, true);
                    first = false;
                    prevHeader = true;
                }
            }
            placeRow(row, first, prevHeader, true);   // 段より大きい行はそのまま置く（溢れ）
        }
        first = false;
        prevHeader = row.header;
    }
    hRule(region().used, b.outer);
    region().used += b.outer * 0.5f;
    pending_.reset();
    applySpaceAfter(table.block.spaceAfter);
}

//------------------------------------------------------------------------------
// 段落
//------------------------------------------------------------------------------

void Flower::placeParagraphs(const std::vector<const inl::Paragraph*>& paras,
                             const block::BlockStyle& style, const inl::Paragraph* label,
                             Pt labelWidth, Pt labelGap) {
    ensureRegion();
    applySpaceBefore(style.spaceBefore);
    bool first = true;
    for (const inl::Paragraph* p : paras) {
        flowParagraph(*p, style, first ? label : nullptr, labelWidth, labelGap, first);
        first = false;
    }
    applySpaceAfter(style.spaceAfter);
}

void Flower::flowParagraph(const inl::Paragraph& para, const block::BlockStyle& style,
                           const inl::Paragraph* label, Pt labelWidth, Pt labelGap,
                           bool firstOfBlock) {
    const Pt pitch = para.style.resolvedLinePitch(para.baseStyle().size);
    const Pt bodyIndent = (labelWidth > 0.0f || label) ? labelWidth + labelGap : 0.0f;

    size_t charStart = 0;
    bool labelPlaced = (label == nullptr);
    int guard = 0;

    while (true) {
        if (++guard > 10000) break;   // 保険
        Region& reg = region();
        int fit = linesThatFit(pitch);

        // この段落（の先頭）を次の段へ送る。直前のブロックが keepWithNext でこの段に
        // 置かれていれば、それも巻き取って一緒に移す
        auto moveToNextRegion = [&]() {
            if (charStart == 0 && pending_ && pending_->blk && pending_->regionIndex == regionIndex_) {
                const Pending pend = *pending_;
                pending_.reset();
                page().dl.items.resize(pend.itemStart);
                region().used = pend.usedBefore;
                nextRegion();
                placeBlock(*pend.blk);
                pending_.reset();
                return;
            }
            nextRegion();
        };

        if (fit <= 0) {
            if (reg.fresh()) {
                fit = 1;   // 段より行送りが大きい: 1 行だけ置いて進める
            } else {
                moveToNextRegion();
                continue;
            }
        }

        // 行の形: 排除領域（回り込み）を避け、ラベル幅ぶん下げる
        const RegionLineShape shape(reg, reg.used, pitch, bodyIndent);
        inl::ParagraphFragment frag = layouter_.layout(para, wm(), shape, charStart, fit);
        const bool starting = (charStart == 0);

        if (starting && !frag.complete && static_cast<int>(frag.lines.size()) < style.orphans &&
            !reg.fresh()) {
            // 段末に orphans 行未満しか残らない: 段落全体を次の段へ
            moveToNextRegion();
            continue;
        }

        // widows: 残りが widows 行未満なら、その分をこの段から送る
        if (!frag.complete && style.widows > 1) {
            const inl::ParagraphFragment rest = layouter_.layout(para, wm(), shape, frag.charEnd, -1);
            const int remaining = static_cast<int>(rest.lines.size());
            if (remaining < style.widows) {
                const int pull = style.widows - remaining;
                const int keep = static_cast<int>(frag.lines.size()) - pull;
                if (keep >= style.orphans && keep > 0) {
                    frag = layouter_.layout(para, wm(), shape, charStart, keep);
                } else if (starting && !reg.fresh()) {
                    moveToNextRegion();
                    continue;
                }
            }
        }

        if (frag.lines.empty()) {
            if (frag.complete) break;
            nextRegion();
            continue;
        }

        // 置く
        const size_t itemStart = page().dl.items.size();
        const Pt usedBefore = reg.used;
        const Point origin = reg.lineOrigin(0.0f, pitch);
        inl::emitParagraph(page().dl, frag, wm(), origin);

        int linesUsed = static_cast<int>(frag.lines.size());
        if (!labelPlaced) {
            // ラベルは本文 1 行目と同じ行送り位置に、行頭側 labelWidth の幅で
            inl::Paragraph lp = *label;
            lp.style.firstLineIndent = 0.0f;
            lp.style.linePitch = pitch;
            Pt s0 = 0.0f, e0 = 0.0f;
            reg.freeInlineRange(reg.used, reg.used + pitch, s0, e0);
            const inl::ConstantLineShape lshape(labelWidth);
            const inl::ParagraphFragment lfrag = layouter_.layout(lp, wm(), lshape);
            inl::emitParagraph(page().dl, lfrag, wm(), reg.lineOrigin(0.0f, pitch, s0));
            linesUsed = std::max(linesUsed, static_cast<int>(lfrag.lines.size()));
            labelPlaced = true;
        }
        reg.used += pitch * static_cast<float>(linesUsed);
        pageHasContent_ = true;

        if (firstOfBlock && style.keepWithNext) {
            Pending pend;
            pend.regionIndex = regionIndex_;
            pend.itemStart = itemStart;
            pend.usedBefore = usedBefore;
            pending_ = pend;   // blk は placeBlock が埋める
        } else {
            pending_.reset();
        }

        if (frag.complete) break;
        charStart = frag.charEnd;
        nextRegion();
    }
}

std::vector<Page> Flower::run(int totalPagesHint) {
    totalPagesHint_ = totalPagesHint;
    newPage();
    for (const block::Block& blk : flow_.blocks) {
        placeBlock(blk);
    }
    finishPage();
    return std::move(pages_);
}

} // namespace

std::vector<Page> FlowLayouter::layout(const block::Flow& flow, const PageSequence& seq,
                                       const FlowLayoutOptions& opts) {
    // {pages} を使うときは総ページ数が要るので 2 回組む
    bool needsTotal = false;
    auto uses = [&](const std::optional<RunningText>& rt) {
        if (!rt) return;
        for (const inl::InlineRun& r : rt->para.runs) {
            if (r.text.find(u"{pages}") != std::u16string::npos) needsTotal = true;
        }
    };
    uses(seq.master.header);
    uses(seq.master.footer);

    int total = 0;
    if (needsTotal) {
        Flower first(fonts_, flow, seq, opts);
        total = static_cast<int>(first.run(0).size());
    }
    Flower flower(fonts_, flow, seq, opts);
    return flower.run(total);
}

} // namespace typeset::page
