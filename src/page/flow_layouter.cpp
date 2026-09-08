/**
 * flow_layouter.cpp — Flow → ページ列
 *
 * 流し込みは Flower が状態を持って進める。段抜き（spanColumns）が段組ページの途中に来たときは、
 * そのページで段に置いた内容を「ページ先頭の再開点」から段の高さを縮めて組み直し（試行モード）、
 * 段の高さが揃ったところで全幅の帯に段抜きブロックを置き、その下で段を再開する。
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

/// 段の行送り方向の長さを H にする（始端は動かさない）
void setRegionExtent(Region& r, Pt H) {
    switch (r.writingMode) {
    case WritingMode::HorizontalTb:
        r.area.h = H;
        break;
    case WritingMode::VerticalRl:
        r.area.x = r.area.right() - H;
        r.area.w = H;
        break;
    case WritingMode::VerticalLr:
        r.area.w = H;
        break;
    }
}

/// 段の始端から d だけ削る
void trimRegionStart(Region& r, Pt d) {
    switch (r.writingMode) {
    case WritingMode::HorizontalTb:
        r.area.y += d;
        r.area.h = std::max(0.0f, r.area.h - d);
        break;
    case WritingMode::VerticalRl:
        r.area.w = std::max(0.0f, r.area.w - d);
        break;
    case WritingMode::VerticalLr:
        r.area.x += d;
        r.area.w = std::max(0.0f, r.area.w - d);
        break;
    }
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
        size_t blockIndex = 0;
        size_t regionIndex = 0;
        size_t itemStart = 0;   ///< その段のページ dl に足した位置
        Pt usedBefore = 0.0f;
    };
    std::optional<Pending> pending_;

    // --- 段のバランス取り（段抜き）のための再開点 ---
    struct PageStart {
        size_t blockIndex = 0;      ///< このページ（の段）の先頭に来るブロック
        int paraIndex = 0;          ///< そのブロック内の段落番号（ラベル付き段落の本文）
        size_t charStart = 0;       ///< 続きから組む位置
        bool labelPlaced = false;
    };
    PageStart pageStart_;
    size_t pageItemStart_ = 0;              ///< 現ページの dl で、段の内容が始まる位置
    std::vector<Region> pageBaseRegions_;   ///< 現ページの段の初期状態（段抜きの後は下へずれたもの）

    // いま組んでいる位置（newPage() が再開点を記録するのに使う）
    size_t curBlockIndex_ = 0;
    int curParaIndex_ = 0;
    size_t curCharStart_ = 0;
    bool curLabelPlaced_ = false;

    // 試行モード: 段からあふれたら overflow を立てて新しいページを作らない
    bool trial_ = false;
    bool trialOverflow_ = false;

    const PageMaster& master() const { return seq_.master; }
    WritingMode wm() const { return master().writingMode; }
    Page& page() { return pages_.back(); }
    Region& region() { return regions_[regionIndex_]; }
    bool aborted() const { return trialOverflow_; }

    void newPage();
    void finishPage();
    bool nextRegion();          ///< 次の段へ。無ければ新しいページ（試行中はあふれ）
    void ensureRegion();

    void placeBlock(const block::Block& blk, const PageStart* resume = nullptr);
    void placeContent(const block::Block& blk, const block::BlockStyle& style, const PageStart* resume);
    void placeParagraphs(const std::vector<const inl::Paragraph*>& paras, const block::BlockStyle& style,
                         const inl::Paragraph* label, Pt labelWidth, Pt labelGap, const PageStart* resume);
    void placeRule(const block::RuleBlock& r);
    void placeImage(const block::ImageBlock& img);
    void placeTable(const block::TableBlock& table);
    void placeSpanning(const block::Block& blk, const block::BlockStyle& style);
    void balanceColumnsBefore(size_t spanBlockIndex);
    void replayPage(const PageStart& start, size_t endBlock);
    void drawGuides();
    void applyBreakBefore(const block::BlockStyle& style);
    void applySpaceBefore(Pt space);
    void applySpaceAfter(Pt space);

    /// 段落 1 つを現在の段以降に流す。ラベル付きなら本文の行長を縮める
    void flowParagraph(const inl::Paragraph& para, const block::BlockStyle& style,
                       const inl::Paragraph* label, Pt labelWidth, Pt labelGap, bool firstOfBlock,
                       size_t charStart0, bool labelPlaced0);
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

    pageBaseRegions_ = regions_;
    pageItemStart_ = 0;
    pageStart_ = PageStart{curBlockIndex_, curParaIndex_, curCharStart_, curLabelPlaced_};
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
    if (trial_) {
        trialOverflow_ = true;
        return false;
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
            if (trial_) { trialOverflow_ = true; return; }
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
// ブロック
//------------------------------------------------------------------------------

void Flower::placeBlock(const block::Block& blk, const PageStart* resume) {
    if (!resume) {
        curParaIndex_ = 0;
        curCharStart_ = 0;
        curLabelPlaced_ = false;
    }

    if (const auto* sec = std::get_if<block::SectionBlock>(&blk)) {
        if (sec->columns) pendingColumns_ = *sec->columns;
        if (sec->columnGap) pendingGap_ = *sec->columnGap;
        ensureRegion();
        if (trial_) {
            if (pageHasContent_) trialOverflow_ = true;
            return;
        }
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

    if (!resume) {
        applyBreakBefore(*style);
        if (aborted()) return;
    }

    if (style->spanColumns && columns_ > 1 && !resume) {
        placeSpanning(blk, *style);
        return;
    }

    placeContent(blk, *style, resume);
    if (aborted()) return;

    if (style->breakAfter == block::BreakKind::Page) {
        if (trial_) { trialOverflow_ = true; return; }
        finishPage();
        newPage();
    } else if (style->breakAfter == block::BreakKind::Column) {
        nextRegion();
        if (aborted()) return;
    }

    // keepWithNext: flowParagraph が作った保留にブロックを結び付ける。次の段落が同じ段に
    // 入らなければ、このブロックごと次の段へ移す
    if (style->keepWithNext && pending_ && !pending_->blk) {
        pending_->blk = &blk;
        pending_->blockIndex = curBlockIndex_;
    } else if (!style->keepWithNext) {
        pending_.reset();
    }
}

void Flower::placeContent(const block::Block& blk, const block::BlockStyle& style,
                          const PageStart* resume) {
    if (const auto* p = std::get_if<block::ParagraphBlock>(&blk)) {
        placeParagraphs({&p->para}, style, nullptr, 0.0f, 0.0f, resume);
    } else if (const auto* h = std::get_if<block::HeadingBlock>(&blk)) {
        placeParagraphs({&h->para}, style, nullptr, 0.0f, 0.0f, resume);
    } else if (const auto* l = std::get_if<block::LabeledBlock>(&blk)) {
        std::vector<const inl::Paragraph*> paras;
        for (const inl::Paragraph& b : l->body) paras.push_back(&b);
        placeParagraphs(paras, style, &l->label, l->labelWidth, l->gap, resume);
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
// 段抜き（spanColumns）
//------------------------------------------------------------------------------

void Flower::replayPage(const PageStart& start, size_t endBlock) {
    const size_t savedBlock = curBlockIndex_;
    for (size_t b = start.blockIndex; b < endBlock && b < flow_.blocks.size(); ++b) {
        curBlockIndex_ = b;
        if (b == start.blockIndex && (start.charStart > 0 || start.paraIndex > 0 || start.labelPlaced)) {
            placeBlock(flow_.blocks[b], &start);
        } else {
            placeBlock(flow_.blocks[b]);
        }
        if (aborted()) break;
    }
    curBlockIndex_ = savedBlock;
}

/**
 * 段抜きブロックの前で、このページの段の内容の高さを揃える
 *
 * ページ先頭の再開点から、段の長さを H に縮めて組み直す。入らなければ H を増やして繰り返す。
 * 元の長さで入っていた内容なので H = 元の長さ で必ず収まる。
 */
void Flower::balanceColumnsBefore(size_t spanBlockIndex) {
    const PageStart start = pageStart_;
    const size_t itemStart = pageItemStart_;
    const std::vector<Region> base = pageBaseRegions_;
    if (base.empty()) return;
    const Pt fullExtent = base[0].blockExtent();
    const int ncol = static_cast<int>(base.size());

    Pt total = 0.0f;
    for (const Region& r : regions_) total += r.used;
    Pt H = std::min(fullExtent, total / static_cast<float>(ncol));

    // 再開点の状態を保存しておく（replay が cursor を動かす）
    const size_t savedBlock = curBlockIndex_;
    const int savedPara = curParaIndex_;
    const size_t savedChar = curCharStart_;
    const bool savedLabel = curLabelPlaced_;

    for (int iter = 0; iter < 40; ++iter) {
        page().dl.items.resize(itemStart);
        regions_ = base;
        for (Region& r : regions_) setRegionExtent(r, H);
        regionIndex_ = 0;
        pending_.reset();
        pageHasContent_ = itemStart > 0;

        trial_ = true;
        trialOverflow_ = false;
        replayPage(start, spanBlockIndex);
        trial_ = false;

        if (!trialOverflow_ || H >= fullExtent - kEps) break;
        // 少しずつ伸ばす（行送り 1 本分程度）
        H = std::min(fullExtent, H + std::max(fullExtent * 0.03f, 6.0f));
    }
    trialOverflow_ = false;

    curBlockIndex_ = savedBlock;
    curParaIndex_ = savedPara;
    curCharStart_ = savedChar;
    curLabelPlaced_ = savedLabel;
}

void Flower::placeSpanning(const block::Block& blk, const block::BlockStyle& style) {
    ensureRegion();
    if (trial_) {
        // 試行中の段抜きは扱えない（ページに内容があればあふれ扱い）
        if (pageHasContent_) trialOverflow_ = true;
        return;
    }

    Pt top = 0.0f;   // 段の内容が終わる位置（段の始端から）
    if (pageHasContent_) {
        balanceColumnsBefore(curBlockIndex_);
        for (const Region& r : regions_) top = std::max(top, r.used);
    }

    // 全幅の帯: 段の始端から top だけ下がった位置から、版面の終端まで
    const Rect body = master().bodyRect(page().number);
    const Rect base = pageBaseRegions_.empty() ? body : pageBaseRegions_[0].area;
    Region band;
    band.writingMode = wm();
    switch (wm()) {
    case WritingMode::HorizontalTb:
        band.area = Rect{body.x, base.y + top, body.w, std::max(0.0f, base.h - top)};
        break;
    case WritingMode::VerticalRl:
        band.area = Rect{base.x, body.y, std::max(0.0f, base.w - top), body.h};
        break;
    case WritingMode::VerticalLr:
        band.area = Rect{base.x + top, body.y, std::max(0.0f, base.w - top), body.h};
        break;
    }

    const std::vector<Region> savedCols = pageBaseRegions_;
    regions_ = {band};
    regionIndex_ = 0;
    if (pageHasContent_ && style.spaceBefore > 0.0f) regions_[0].used += style.spaceBefore;

    // 帯に入らないなら改ページして先頭に置く
    Pt need = 0.0f;
    if (const auto* p = std::get_if<block::ParagraphBlock>(&blk)) need = measureParagraph(p->para, band.lineLength());
    else if (const auto* h = std::get_if<block::HeadingBlock>(&blk)) need = measureParagraph(h->para, band.lineLength());
    if (band.blockExtent() - regions_[0].used < need) {
        regions_ = savedCols;
        finishPage();
        newPage();
        placeSpanning(blk, style);
        return;
    }

    placeContent(blk, style, nullptr);
    const Pt consumed = regions_[0].used + style.spaceAfter;

    // 段を帯の下から再開する
    regions_ = savedCols;
    for (Region& reg : regions_) {
        trimRegionStart(reg, top + consumed);
        reg.used = 0.0f;
        reg.exclusions.clear();
    }
    regionIndex_ = 0;
    pageHasContent_ = true;
    pending_.reset();

    pageBaseRegions_ = regions_;
    pageItemStart_ = page().dl.items.size();
    pageStart_ = PageStart{curBlockIndex_ + 1, 0, 0, false};
}

//------------------------------------------------------------------------------
// 罫線
//------------------------------------------------------------------------------

void Flower::placeRule(const block::RuleBlock& r) {
    applySpaceBefore(r.block.spaceBefore);
    if (region().remaining() < r.thickness) {
        nextRegion();
        if (aborted()) return;
    }
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

    if (region().remaining() + kEps < total && !region().fresh()) {
        nextRegion();
        if (aborted()) return;
    }
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

    const size_t nrow = table.rows.size();

    // --- グリッド: セルを列に割り付ける（colspan / rowspan） ---
    struct GCell {
        const block::TableCell* cell = nullptr;
        size_t row = 0, col = 0;
        int colspan = 1, rowspan = 1;
        Pt height = 0.0f;
    };
    std::vector<GCell> cells;
    std::vector<std::vector<int>> owner(nrow);   // owner[r][c] = cells のインデックス（-1 = 空）
    size_t ncol = table.columns.size();
    {
        // 列数を先に決める（rowspan で下の行に食い込むぶんも数える）
        std::vector<std::vector<int>> occ(nrow);
        for (size_t r = 0; r < nrow; ++r) {
            size_t c = 0;
            for (const block::TableCell& cell : table.rows[r].cells) {
                while (c < occ[r].size() && occ[r][c] >= 0) ++c;
                const int cs = std::max(1, cell.colspan);
                const int rs = std::max(1, cell.rowspan);
                for (int dr = 0; dr < rs && r + dr < nrow; ++dr) {
                    if (occ[r + dr].size() < c + cs) occ[r + dr].resize(c + cs, -1);
                    for (int dc = 0; dc < cs; ++dc) occ[r + dr][c + dc] = 1;
                }
                c += static_cast<size_t>(cs);
            }
            ncol = std::max(ncol, occ[r].size());
        }
    }
    if (ncol == 0) return;
    for (auto& row : owner) row.assign(ncol, -1);
    for (size_t r = 0; r < nrow; ++r) {
        size_t c = 0;
        for (const block::TableCell& cell : table.rows[r].cells) {
            while (c < ncol && owner[r][c] >= 0) ++c;
            if (c >= ncol) break;
            GCell g;
            g.cell = &cell;
            g.row = r;
            g.col = c;
            g.colspan = static_cast<int>(std::min<size_t>(std::max(1, cell.colspan), ncol - c));
            g.rowspan = static_cast<int>(std::min<size_t>(std::max(1, cell.rowspan), nrow - r));
            const int id = static_cast<int>(cells.size());
            cells.push_back(g);
            for (int dr = 0; dr < g.rowspan; ++dr)
                for (int dc = 0; dc < g.colspan; ++dc) owner[r + dr][c + dc] = id;
            c += static_cast<size_t>(g.colspan);
        }
    }

    std::vector<block::TableColumn> columns = table.columns;
    columns.resize(ncol);
    const Pt pad = table.cellPadding;
    const Pt L = region().lineLength();

    // --- 列幅: 固定はそのまま、自動は内容の自然幅から（1 列のセルだけ見る） ---
    std::vector<Pt> widths(ncol, 0.0f);
    std::vector<Pt> natural(ncol, 0.0f);
    std::vector<Pt> minimum(ncol, 0.0f);
    for (const GCell& g : cells) {
        if (g.colspan != 1 || columns[g.col].width > 0.0f) continue;
        Pt nat = 0.0f, mn = 0.0f;
        for (const inl::Paragraph& p : g.cell->paras) {
            Pt a = 0.0f, b2 = 0.0f;
            measureParagraph(p, 1.0e6f, &a);
            measureParagraph(p, 1.0f, &b2);   // 1pt で組むと 1 行が 1 つの割れない塊になる
            nat = std::max(nat, a);
            mn = std::max(mn, b2);
        }
        natural[g.col] = std::max(natural[g.col], nat + 2.0f * pad);
        minimum[g.col] = std::max(minimum[g.col], mn + 2.0f * pad);
    }
    Pt fixedSum = 0.0f;
    std::vector<bool> isAuto(ncol, false);
    for (size_t i = 0; i < ncol; ++i) {
        if (columns[i].width > 0.0f) { widths[i] = columns[i].width; fixedSum += widths[i]; }
        else { widths[i] = std::max(natural[i], 2.0f * pad + 1.0f); isAuto[i] = true; natural[i] = widths[i]; }
    }
    {
        Pt available = std::max(1.0f, L - fixedSum);
        for (int iter = 0; iter < 8; ++iter) {
            Pt autoSum = 0.0f;
            int autoCount = 0;
            for (size_t i = 0; i < ncol; ++i) if (isAuto[i]) { autoSum += natural[i]; ++autoCount; }
            if (autoCount == 0 || autoSum <= 0.0f) break;
            float scale = 1.0f;
            if (autoSum > available) scale = available / autoSum;
            else if (table.fullWidth) scale = available / autoSum;
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

    // --- 行の高さ: rowspan 1 のセルで決め、rowspan のセルが足りなければ最後の行を伸ばす ---
    std::vector<Pt> rowH(nrow, 0.0f);
    for (GCell& g : cells) {
        const Pt cellWidth = colStart[g.col + g.colspan] - colStart[g.col] - 2.0f * pad;
        Pt ext = 2.0f * pad;
        for (const inl::Paragraph& p : g.cell->paras) ext += measureParagraph(p, cellWidth);
        g.height = ext;
        if (g.rowspan == 1) rowH[g.row] = std::max(rowH[g.row], ext);
    }
    for (const GCell& g : cells) {
        if (g.rowspan <= 1) continue;
        Pt sum = 0.0f;
        for (int dr = 0; dr < g.rowspan; ++dr) sum += rowH[g.row + dr];
        if (sum < g.height) rowH[g.row + g.rowspan - 1] += g.height - sum;
    }

    // --- 行のグループ（rowspan でつながる行は一緒に置く） ---
    std::vector<size_t> groupEnd(nrow, 0);
    for (size_t r = 0; r < nrow; ++r) groupEnd[r] = r;
    for (const GCell& g : cells) {
        for (int dr = 0; dr < g.rowspan; ++dr) {
            groupEnd[g.row + dr] = std::max(groupEnd[g.row + dr], g.row + g.rowspan - 1);
        }
    }

    // ヘッダ行（先頭の連続したヘッダ行を繰り返す）
    size_t headerCount = 0;
    while (headerCount < nrow && table.rows[headerCount].header) ++headerCount;

    const block::TableBorders& b = table.borders;
    const Pt tableLeft = colStart.front() - b.outer * 0.5f;
    const Pt tableRight = colStart.back() + b.outer * 0.5f;

    // 行 r の上の横罫: 上のセルと違うところだけ引く（rowspan の内部は引かない）
    auto hRuleAbove = [&](size_t r, Pt blockPos, Pt thickness, bool wholeWidth) {
        if (!b.horizontal || thickness <= 0.0f) return;
        if (wholeWidth) {
            fillLogicalRect(tableLeft, tableRight, blockPos - thickness * 0.5f, blockPos + thickness * 0.5f, b.color);
            return;
        }
        size_t c = 0;
        while (c < ncol) {
            const bool draw = (r == 0) || (owner[r - 1][c] != owner[r][c]);
            if (!draw) { ++c; continue; }
            size_t e = c;
            while (e < ncol && ((r == 0) || (owner[r - 1][e] != owner[r][e]))) ++e;
            const Pt x0 = (c == 0) ? tableLeft : colStart[c];
            const Pt x1 = (e == ncol) ? tableRight : colStart[e];
            fillLogicalRect(x0, x1, blockPos - thickness * 0.5f, blockPos + thickness * 0.5f, b.color);
            c = e;
        }
    };
    // 行 r の縦罫: 左右のセルが違う境だけ
    auto vRules = [&](size_t r, Pt block0, Pt block1) {
        if (!b.vertical) return;
        for (size_t c = 0; c <= ncol; ++c) {
            const bool edge = (c == 0 || c == ncol);
            if (!edge && owner[r][c - 1] == owner[r][c]) continue;
            const Pt t = edge ? b.outer : b.inner;
            fillLogicalRect(colStart[c] - t * 0.5f, colStart[c] + t * 0.5f, block0, block1, b.color);
        }
    };

    // 行 [r0, r1] を置く（rowspan でつながった塊）。入らなければ false
    auto placeRows = [&](size_t r0, size_t r1, bool first, bool afterHeader, bool force) -> bool {
        Region& reg = region();
        Pt extent = 0.0f;
        for (size_t r = r0; r <= r1; ++r) extent += rowH[r];
        const Pt ruleAbove = first ? b.outer : (afterHeader ? b.headerRule : b.inner);
        if (!force && reg.remaining() + kEps < extent + ruleAbove && !reg.fresh()) return false;

        Pt top = reg.used;
        for (size_t r = r0; r <= r1; ++r) {
            const Pt thickness = (r == r0) ? ruleAbove : b.inner;
            hRuleAbove(r, top, thickness, r == r0 && first);
            // この行から始まるセル
            for (const GCell& g : cells) {
                if (g.row != r) continue;
                const Pt cellWidth = colStart[g.col + g.colspan] - colStart[g.col] - 2.0f * pad;
                Pt off = pad;
                for (const inl::Paragraph& p : g.cell->paras) {
                    off += placeParagraphAt(p, top - reg.used + off, colStart[g.col] + pad, cellWidth,
                                            columns[g.col].align == Align::Start
                                                ? std::nullopt
                                                : std::optional<Align>(columns[g.col].align));
                }
            }
            vRules(r, top, top + rowH[r]);
            top += rowH[r];
        }
        reg.used = top;
        pageHasContent_ = true;
        return true;
    };

    bool first = true;
    bool prevHeader = false;
    for (size_t r = 0; r < nrow;) {
        const size_t r1 = std::max(groupEnd[r], r);
        if (!placeRows(r, r1, first, prevHeader, false)) {
            // 段の末尾で閉じて次の段へ。ヘッダ行を繰り返す
            if (b.horizontal) {
                fillLogicalRect(tableLeft, tableRight, region().used - b.outer * 0.5f,
                                region().used + b.outer * 0.5f, b.color);
            }
            nextRegion();
            if (aborted()) return;
            first = true;
            prevHeader = false;
            if (table.repeatHeader && headerCount > 0 && r >= headerCount) {
                placeRows(0, headerCount - 1, true, false, true);
                first = false;
                prevHeader = true;
            }
            placeRows(r, r1, first, prevHeader, true);   // 段より大きい塊はそのまま置く（溢れ）
        }
        first = false;
        prevHeader = table.rows[r1].header;
        r = r1 + 1;
    }
    if (b.horizontal) {
        fillLogicalRect(tableLeft, tableRight, region().used - b.outer * 0.5f,
                        region().used + b.outer * 0.5f, b.color);
    }
    region().used += b.outer * 0.5f;
    pending_.reset();
    applySpaceAfter(table.block.spaceAfter);
}

//------------------------------------------------------------------------------
// 段落
//------------------------------------------------------------------------------

void Flower::placeParagraphs(const std::vector<const inl::Paragraph*>& paras,
                             const block::BlockStyle& style, const inl::Paragraph* label,
                             Pt labelWidth, Pt labelGap, const PageStart* resume) {
    ensureRegion();
    if (!resume) applySpaceBefore(style.spaceBefore);
    const int startPara = resume ? resume->paraIndex : 0;
    for (int i = startPara; i < static_cast<int>(paras.size()); ++i) {
        const bool first = (i == 0);
        const bool resumed = resume && i == startPara;
        curParaIndex_ = i;
        flowParagraph(*paras[i], style, first ? label : nullptr, labelWidth, labelGap, first,
                      resumed ? resume->charStart : 0,
                      resumed ? resume->labelPlaced : false);
        if (aborted()) return;
    }
    applySpaceAfter(style.spaceAfter);
}

void Flower::flowParagraph(const inl::Paragraph& para, const block::BlockStyle& style,
                           const inl::Paragraph* label, Pt labelWidth, Pt labelGap,
                           bool firstOfBlock, size_t charStart0, bool labelPlaced0) {
    const Pt pitch = para.style.resolvedLinePitch(para.baseStyle().size);
    const Pt bodyIndent = (labelWidth > 0.0f || label) ? labelWidth + labelGap : 0.0f;

    size_t charStart = charStart0;
    bool labelPlaced = (label == nullptr) || labelPlaced0;
    int guard = 0;

    while (true) {
        if (++guard > 10000) break;   // 保険
        curCharStart_ = charStart;
        curLabelPlaced_ = labelPlaced;

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
                // 新しいページの先頭は巻き取った見出しになる
                const size_t myBlock = curBlockIndex_;
                const int myPara = curParaIndex_;
                curBlockIndex_ = pend.blockIndex;
                curParaIndex_ = 0;
                curCharStart_ = 0;
                curLabelPlaced_ = false;
                nextRegion();
                if (!aborted()) placeBlock(*pend.blk);
                pending_.reset();
                curBlockIndex_ = myBlock;
                curParaIndex_ = myPara;
                curCharStart_ = 0;
                curLabelPlaced_ = labelPlaced;
                return;
            }
            nextRegion();
        };

        if (fit <= 0) {
            if (reg.fresh()) {
                fit = 1;   // 段より行送りが大きい: 1 行だけ置いて進める
            } else {
                moveToNextRegion();
                if (aborted()) return;
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
            if (aborted()) return;
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
                    if (aborted()) return;
                    continue;
                }
            }
        }

        if (frag.lines.empty()) {
            if (frag.complete) break;
            nextRegion();
            if (aborted()) return;
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

        if (firstOfBlock && style.keepWithNext && charStart0 == 0) {
            Pending pend;
            pend.regionIndex = regionIndex_;
            pend.itemStart = itemStart;
            pend.usedBefore = usedBefore;
            pend.blockIndex = curBlockIndex_;
            pending_ = pend;   // blk は placeBlock が埋める
        } else {
            pending_.reset();
        }

        if (frag.complete) break;
        charStart = frag.charEnd;
        curCharStart_ = charStart;
        curLabelPlaced_ = labelPlaced;
        nextRegion();
        if (aborted()) return;
    }
}

std::vector<Page> Flower::run(int totalPagesHint) {
    totalPagesHint_ = totalPagesHint;
    newPage();
    for (size_t i = 0; i < flow_.blocks.size(); ++i) {
        curBlockIndex_ = i;
        placeBlock(flow_.blocks[i]);
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
