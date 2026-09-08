/**
 * flow_layouter.cpp — Flow → ページ列
 *
 * 流し込みは Flower が状態を持って進める。
 *
 *  - 段抜き（spanColumns）が段組ページの途中に来たときは、そのページで段に置いた内容を
 *    「ページ先頭の再開点」から段の長さを縮めて組み直し（試行モード）、段の高さが揃ったところで
 *    全幅の帯に段抜きブロックを置き、その下で段を再開する。最終ページの段揃えも同じ機構
 *  - 見出しの自動採番・図表番号・相互参照（{ref:ラベル} {page:ラベル}）・目次は 2〜3 パスで解く。
 *    前のパスで集めた番号とページを次のパスの置換に使う
 */

#include "typeset/page/flow_layouter.hpp"

#include <algorithm>
#include <cmath>

#include "typeset/text/utf.hpp"

namespace typeset::page {

namespace {

constexpr float kEps = 0.01f;

/// `{name}` を置換した段落を返す（`{` が無ければコピーだけ）
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

bool hasPlaceholder(const inl::Paragraph& p) {
    for (const inl::InlineRun& r : p.runs) {
        if (r.text.find(u'{') != std::u16string::npos) return true;
    }
    return false;
}

std::u16string toU16(int v) {
    return text::utf8ToUtf16(std::to_string(v));
}

std::u16string toU16(const std::string& s) { return text::utf8ToUtf16(s); }

const block::BlockStyle* styleOf(const block::Block& blk) {
    if (const auto* p = std::get_if<block::ParagraphBlock>(&blk)) return &p->block;
    if (const auto* h = std::get_if<block::HeadingBlock>(&blk)) return &h->block;
    if (const auto* r = std::get_if<block::RuleBlock>(&blk)) return &r->block;
    if (const auto* s = std::get_if<block::SpacerBlock>(&blk)) return &s->block;
    if (const auto* l = std::get_if<block::LabeledBlock>(&blk)) return &l->block;
    if (const auto* i = std::get_if<block::ImageBlock>(&blk)) return &i->block;
    if (const auto* t = std::get_if<block::TableBlock>(&blk)) return &t->block;
    if (const auto* li = std::get_if<block::ListBlock>(&blk)) return &li->block;
    if (const auto* tc = std::get_if<block::TocBlock>(&blk)) return &tc->block;
    if (const auto* ob = std::get_if<block::ObjectBlock>(&blk)) return &ob->block;
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
 * パス間で受け渡す情報（番号・ページ・見出し一覧）
 */
struct RefInfo {
    std::map<std::u16string, std::u16string> fields;   ///< "ref:label" / "page:label" → 文字列
    struct Heading {
        std::u16string title;   ///< 番号込み
        int level = 1;
        int page = 0;
    };
    std::vector<Heading> headings;
    int totalPages = 0;

    bool operator==(const RefInfo& o) const {
        if (fields != o.fields || totalPages != o.totalPages || headings.size() != o.headings.size()) return false;
        for (size_t i = 0; i < headings.size(); ++i) {
            if (headings[i].title != o.headings[i].title || headings[i].level != o.headings[i].level ||
                headings[i].page != o.headings[i].page) return false;
        }
        return true;
    }
};

/**
 * 流し込みの状態
 */
class Flower {
public:
    Flower(font::FontSet& fonts, const block::Flow& flow, const PageSequence& seq,
           const FlowLayoutOptions& opts, const RefInfo* prev)
        : fonts_(fonts), layouter_(fonts), flow_(flow), seq_(seq), opts_(opts), prev_(prev),
          columns_(seq.master.columns), columnGap_(seq.master.columnGap) {
        fields_ = opts.fields;
        if (prev_) {
            for (const auto& kv : prev_->fields) fields_[kv.first] = kv.second;
        }
    }

    std::vector<Page> run(int totalPagesHint);
    const RefInfo& collected() const { return collected_; }

private:
    font::FontSet& fonts_;
    inl::ParagraphLayouter layouter_;
    const block::Flow& flow_;
    const PageSequence& seq_;
    const FlowLayoutOptions& opts_;
    const RefInfo* prev_;
    std::map<std::u16string, std::u16string> fields_;
    RefInfo collected_;

    int columns_;
    Pt columnGap_;
    std::optional<int> pendingColumns_;
    std::optional<Pt> pendingGap_;

    std::vector<Page> pages_;
    std::vector<Region> regions_;   ///< 現在のページの段
    size_t regionIndex_ = 0;
    bool pageHasContent_ = false;
    int totalPagesHint_ = 0;

    // 採番
    std::vector<int> headingCounters_;
    int figureCount_ = 0;
    int tableCount_ = 0;
    int equationCount_ = 0;

    /// ブロックを置く前の採番・収集の状態（巻き戻し用）
    struct NumberingState {
        std::vector<int> headingCounters;
        int figureCount = 0;
        int tableCount = 0;
        int equationCount = 0;
        size_t collectedHeadings = 0;
        std::map<std::u16string, std::u16string> collectedFields;
    };
    NumberingState snapshotNumbering() const {
        return NumberingState{headingCounters_, figureCount_, tableCount_, equationCount_,
                              collected_.headings.size(), collected_.fields};
    }
    void restoreNumbering(const NumberingState& s) {
        headingCounters_ = s.headingCounters;
        figureCount_ = s.figureCount;
        tableCount_ = s.tableCount;
        equationCount_ = s.equationCount;
        collected_.headings.resize(std::min(s.collectedHeadings, collected_.headings.size()));
        collected_.fields = s.collectedFields;
    }

    /// keepWithNext で保留中のブロック（この段に置いたが、次が入らなければ一緒に移す）
    struct Pending {
        const block::Block* blk = nullptr;
        size_t blockIndex = 0;
        size_t regionIndex = 0;
        size_t itemStart = 0;   ///< その段のページ dl に足した位置
        Pt usedBefore = 0.0f;
        NumberingState numbering;   ///< ブロックを置く前の採番状態
    };
    std::optional<Pending> pending_;
    size_t blockItemStart_ = 0;     ///< いま置いているブロックの最初の dl 位置
    NumberingState blockNumbering_; ///< いま置いているブロックの前の採番状態

    // --- 段のバランス取り（段抜き）のための再開点 ---
    struct PageStart {
        size_t blockIndex = 0;      ///< このページ（の段）の先頭に来るブロック
        int paraIndex = 0;          ///< そのブロック内の段落番号（ラベル付き段落の本文・箇条書きの項目）
        size_t charStart = 0;       ///< 続きから組む位置
        bool labelPlaced = false;
    };
    PageStart pageStart_;
    size_t pageItemStart_ = 0;              ///< 現ページの dl で、段の内容が始まる位置
    std::vector<Region> pageBaseRegions_;   ///< 現ページの段の初期状態（段抜きの後は下へずれたもの）
    NumberingState pageNumbering_;          ///< 再開点での採番状態（組み直しで番号が進まないように）

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

    inl::Paragraph resolved(const inl::Paragraph& p) const {
        inl::Paragraph r = hasPlaceholder(p) ? substitute(p, fields_) : p;
        resolveObjects(r);
        return r;
    }

    /// 行内オブジェクトの参照（objectRef）をハンドラで解決する。ハンドラが無い／失敗なら代替テキスト
    void resolveObjects(inl::Paragraph& p) const {
        for (inl::InlineRun& r : p.runs) {
            if (!r.objectRef || r.object) continue;
            std::shared_ptr<const obj::ObjectResult> res;
            if (opts_.objects) {
                obj::ObjectRequest req;
                req.handler = r.objectRef->handler;
                req.source = r.objectRef->source;
                req.params = r.objectRef->params;
                req.maxInline = regions_.empty() ? 0.0f : regions_[regionIndex_].lineLength();
                req.fontSize = r.style.size;
                req.writingMode = wm();
                req.inlineContext = true;
                res = opts_.objects->render(req);
            }
            if (res && res->ok()) {
                r.object = res;
            } else {
                r.text = u"[" + toU16(r.objectRef->handler) + u"]";
            }
        }
    }

    /// 現ページの段に内容が無い（巻き戻しで空になった等）
    bool pageEmpty() const {
        if (pages_.empty()) return true;
        if (pages_.back().dl.items.size() > pageItemStart_) return false;
        for (const Region& r : regions_) if (r.used > kEps) return false;
        return true;
    }

    void newPage();
    void finishPage();
    bool nextRegion();          ///< 次の段へ。無ければ新しいページ（試行中はあふれ）
    void ensureRegion();

    void placeBlock(const block::Block& blk, const PageStart* resume = nullptr);
    void placeContent(const block::Block& blk, const block::BlockStyle& style, const PageStart* resume);
    void placeParagraphs(const std::vector<const inl::Paragraph*>& paras, const block::BlockStyle& style,
                         const inl::Paragraph* label, Pt labelWidth, Pt labelGap, const PageStart* resume);
    void placeHeading(const block::HeadingBlock& h, const PageStart* resume);
    void placeList(const block::ListBlock& list, const PageStart* resume);
    void placeToc(const block::TocBlock& toc, const PageStart* resume);
    void placeRule(const block::RuleBlock& r);
    void placeImage(const block::ImageBlock& img);
    void placeObject(const block::ObjectBlock& ob);
    void placeTable(const block::TableBlock& table);
    void placeSpanning(const block::Block& blk, const block::BlockStyle& style);
    void balanceColumns(size_t endBlock);
    void replayPage(const PageStart& start, size_t endBlock);
    void drawGuides();
    void applyBreakBefore(const block::BlockStyle& style);
    void applySpaceBefore(Pt space);
    void applySpaceAfter(Pt space);
    void recordLabel(const std::string& label, const std::u16string& number);

    /// 段落 1 つを現在の段以降に流す。ラベル付きなら本文の行長を縮める
    void flowParagraph(const inl::Paragraph& para, const block::BlockStyle& style,
                       const inl::Paragraph* label, Pt labelWidth, Pt labelGap, bool firstOfBlock,
                       size_t charStart0, bool labelPlaced0);
    int linesThatFit(Pt pitch) const;
    void layoutRunning(const RunningText& rt, bool top);

    /// 論理座標の矩形を塗る（罫線・背景用）。index を渡すとその位置へ挿入（背景を後ろへ）
    void fillLogicalRect(Pt inline0, Pt inline1, Pt block0, Pt block1, Color color,
                         std::optional<size_t> insertAt = std::nullopt);
    /// 段落を指定の行長・位置で全部組んで置く（表のセル・キャプション用）。消費した行送り方向の量を返す
    Pt placeParagraphAt(const inl::Paragraph& para, Pt blockOffset, Pt inlineOffset, Pt lineLength,
                        std::optional<Align> alignOverride = std::nullopt,
                        inl::ParagraphFragment* fragOut = nullptr);
    /// 段落を組んだときの行送り方向の量（置かない）
    Pt measureParagraph(const inl::Paragraph& para, Pt lineLength, Pt* naturalMax = nullptr);
    /// maxLines 行以内で組み、行内オブジェクトで広がった行送りが available に入らなければ行数を減らす
    inl::ParagraphFragment layoutFitting(const inl::Paragraph& para, const inl::LineShapeProvider& shape,
                                         size_t charStart, int maxLines, Pt available);
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
    pageNumbering_ = snapshotNumbering();
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
    std::map<std::u16string, std::u16string> fields = fields_;
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

void Flower::recordLabel(const std::string& label, const std::u16string& number) {
    if (label.empty()) return;
    collected_.fields[u"ref:" + toU16(label)] = number;
    collected_.fields[u"page:" + toU16(label)] = toU16(page().number);
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

void Flower::fillLogicalRect(Pt inline0, Pt inline1, Pt block0, Pt block1, Color color,
                             std::optional<size_t> insertAt) {
    const Rect r = region().toRect(inline0, inline1, block0, block1);
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    if (insertAt && *insertAt <= page().dl.items.size()) {
        page().dl.items.insert(page().dl.items.begin() + static_cast<ptrdiff_t>(*insertAt),
                               dl::RectItem{r, color});
    } else {
        page().dl.addRect(r, color);
    }
}

Pt Flower::measureParagraph(const inl::Paragraph& para, Pt lineLength, Pt* naturalMax) {
    const inl::ConstantLineShape shape(std::max(1.0f, lineLength));
    const inl::ParagraphFragment frag = layouter_.layout(resolved(para), wm(), shape);
    if (naturalMax) {
        Pt m = 0.0f;
        for (const inl::LineBox& l : frag.lines) m = std::max(m, l.naturalLength + l.indent);
        *naturalMax = m;
    }
    return frag.blockExtent();
}

inl::ParagraphFragment Flower::layoutFitting(const inl::Paragraph& para, const inl::LineShapeProvider& shape,
                                             size_t charStart, int maxLines, Pt available) {
    inl::ParagraphFragment frag = layouter_.layout(para, wm(), shape, charStart, maxLines);
    // 行内オブジェクトで行送りが広がって入らなくなったら、行数を減らして組み直す
    while (frag.lines.size() > 1 && frag.blockExtent() > available + kEps) {
        frag = layouter_.layout(para, wm(), shape, charStart, static_cast<int>(frag.lines.size()) - 1);
    }
    return frag;
}

Pt Flower::placeParagraphAt(const inl::Paragraph& para, Pt blockOffset, Pt inlineOffset,
                            Pt lineLength, std::optional<Align> alignOverride,
                            inl::ParagraphFragment* fragOut) {
    inl::Paragraph p = resolved(para);
    if (alignOverride) p.style.align = *alignOverride;
    const inl::ConstantLineShape shape(std::max(1.0f, lineLength));
    const inl::ParagraphFragment frag = layouter_.layout(p, wm(), shape);
    if (fragOut) *fragOut = frag;
    if (frag.lines.empty()) return 0.0f;
    const Point origin = region().lineOrigin(blockOffset, frag.linePitch, inlineOffset);
    inl::emitParagraph(page().dl, frag, wm(), origin);
    return frag.blockExtent();
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
        if (trial_) return;   // 組み直し中: 段数はこのページで既に効いている
        if (pageHasContent_) {
            // 段数の変更はページ単位。内容があれば改ページする。直前が keepWithNext の見出しなら
            // 巻き取って新しいページの先頭に置き直す
            std::optional<Pending> carry;
            if (pending_ && pending_->blk && pending_->regionIndex == regionIndex_) {
                carry = *pending_;
                pending_.reset();
                page().dl.items.resize(carry->itemStart);
                region().used = carry->usedBefore;
                restoreNumbering(carry->numbering);
            }
            const size_t myBlock = curBlockIndex_;
            if (carry) curBlockIndex_ = carry->blockIndex;   // 新しいページの再開点は巻き取った見出し
            if (pageEmpty()) {
                pages_.pop_back();   // 見出しだけだったページは作り直す（空ページを残さない）
            } else {
                finishPage();
            }
            newPage();
            if (carry) placeBlock(*carry->blk);
            curBlockIndex_ = myBlock;
        } else {
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
    ensureRegion();
    blockItemStart_ = page().dl.items.size();
    blockNumbering_ = snapshotNumbering();

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
        placeHeading(*h, resume);
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
    } else if (const auto* ob = std::get_if<block::ObjectBlock>(&blk)) {
        placeObject(*ob);
    } else if (const auto* t = std::get_if<block::TableBlock>(&blk)) {
        placeTable(*t);
    } else if (const auto* li = std::get_if<block::ListBlock>(&blk)) {
        placeList(*li, resume);
    } else if (const auto* tc = std::get_if<block::TocBlock>(&blk)) {
        placeToc(*tc, resume);
    }
}

//------------------------------------------------------------------------------
// 見出し（採番・しおり・目次の収集）
//------------------------------------------------------------------------------

void Flower::placeHeading(const block::HeadingBlock& h, const PageStart* resume) {
    inl::Paragraph para = resolved(h.para);
    std::u16string number;
    if (h.numbered && !resume) {
        const size_t level = static_cast<size_t>(std::max(1, h.level));
        if (headingCounters_.size() < level) headingCounters_.resize(level, 0);
        headingCounters_[level - 1] += 1;
        headingCounters_.resize(level);   // 下のレベルをリセット
        for (size_t i = 0; i < level; ++i) {
            if (i) number += u".";
            number += toU16(headingCounters_[i]);
        }
        std::u16string prefix = number;
        if (level == 1) prefix += u".";
        prefix += opts_.headingNumberSeparator;
        if (!para.runs.empty()) para.runs.front().text = prefix + para.runs.front().text;
        else para.runs.push_back(inl::InlineRun{prefix, h.para.baseStyle()});
    } else if (h.numbered && resume) {
        // 続き（段またぎ）は番号を振り直さず、前に付けた番号を再現する
        const size_t level = static_cast<size_t>(std::max(1, h.level));
        for (size_t i = 0; i < level && i < headingCounters_.size(); ++i) {
            if (i) number += u".";
            number += toU16(headingCounters_[i]);
        }
        std::u16string prefix = number + (level == 1 ? u"." : u"") + opts_.headingNumberSeparator;
        if (!para.runs.empty()) para.runs.front().text = prefix + para.runs.front().text;
    }

    // 置く前の位置を控える（しおり・目次のページはここで決まる。ただし段が変わったら置いた後で直す）
    const size_t pagesBefore = pages_.size();
    placeParagraphs({&para}, h.block, nullptr, 0.0f, 0.0f, resume);
    if (aborted()) return;

    if (!resume) {
        // 見出しの先頭の位置: 直前に置いたグリフ列の最初の位置から
        Point pos{region().area.x, region().area.y};
        for (size_t i = blockItemStart_; i < page().dl.items.size(); ++i) {
            if (const auto* run = std::get_if<dl::GlyphRun>(&page().dl.items[i])) {
                if (!run->glyphs.empty()) { pos = run->glyphs.front().pos; break; }
            }
        }
        if (pages_.size() != pagesBefore) {
            // 見出しが次のページへ移った場合、dl は新しいページのものなので blockItemStart_ は使えない
            for (const dl::Item& item : page().dl.items) {
                if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
                    if (!run->glyphs.empty()) { pos = run->glyphs.front().pos; break; }
                }
            }
        }
        if (h.bookmark && !trial_) {
            dl::Bookmark bm;
            bm.title = para.text();
            bm.level = h.level;
            bm.pos = pos;
            // 見出しのグリフの前に入れる（keepWithNext の巻き取りで一緒に消えるように）
            const size_t at = std::min(blockItemStart_, page().dl.items.size());
            page().dl.items.insert(page().dl.items.begin() + static_cast<ptrdiff_t>(at), bm);
            if (pending_ && pending_->itemStart >= at) pending_->itemStart += 1;
        }
        if (!trial_) {
            collected_.headings.push_back(RefInfo::Heading{para.text(), h.level, page().number});
            recordLabel(h.block.label, number.empty() ? para.text() : number);
        }
    }
}

//------------------------------------------------------------------------------
// 箇条書き
//------------------------------------------------------------------------------

void Flower::placeList(const block::ListBlock& list, const PageStart* resume) {
    if (list.items.empty()) return;
    ensureRegion();
    if (!resume) applySpaceBefore(list.block.spaceBefore);
    const int start = resume ? resume->paraIndex : 0;
    for (int i = start; i < static_cast<int>(list.items.size()); ++i) {
        const inl::Paragraph& item = list.items[i];
        const Pt em = item.baseStyle().size;
        const Pt labelWidth = list.labelWidth > 0.0f ? list.labelWidth : em * 1.5f;
        std::u16string marker = list.marker == block::ListBlock::Marker::Bullet
                                    ? list.bullet
                                    : toU16(i + 1) + list.numberSuffix;
        inl::Paragraph label = inl::Paragraph::plain(marker, item.baseStyle());
        label.style.align = Align::Start;
        label.style.firstLineIndent = 0.0f;

        block::BlockStyle st = list.block;
        st.spaceBefore = (i == 0) ? 0.0f : list.itemGap;
        st.spaceAfter = 0.0f;
        st.keepWithNext = false;
        st.background.reset();
        st.padding = 0.0f;

        curParaIndex_ = i;
        const bool resumed = resume && i == start;
        if (!resumed && i > 0) applySpaceBefore(list.itemGap);
        flowParagraph(item, st, &label, labelWidth, list.gap, false,
                      resumed ? resume->charStart : 0, resumed ? resume->labelPlaced : false);
        if (aborted()) return;
    }
    applySpaceAfter(list.block.spaceAfter);
    pending_.reset();
}

//------------------------------------------------------------------------------
// 目次
//------------------------------------------------------------------------------

void Flower::placeToc(const block::TocBlock& toc, const PageStart* resume) {
    ensureRegion();
    if (!prev_) return;   // 最初のパスでは見出しがまだ分からない
    if (!resume) applySpaceBefore(toc.block.spaceBefore);

    const TextStyle& st1 = toc.style;
    const TextStyle& st2 = toc.subStyle ? *toc.subStyle : toc.style;
    const Pt indentUnit = toc.indentPerLevel > 0.0f ? toc.indentPerLevel : st1.size;

    // ページ番号の幅（3 桁分）
    inl::Paragraph probe = inl::Paragraph::plain(u"000", st1);
    Pt numberWidth = 0.0f;
    measureParagraph(probe, 1.0e6f, &numberWidth);
    const Pt gap = st1.size;

    const int start = resume ? resume->paraIndex : 0;
    for (int i = start; i < static_cast<int>(prev_->headings.size()); ++i) {
        const RefInfo::Heading& e = prev_->headings[i];
        if (e.level > toc.maxLevel) continue;
        curParaIndex_ = i;
        curCharStart_ = 0;

        const TextStyle& st = (e.level <= 1) ? st1 : st2;
        const Pt indent = indentUnit * static_cast<float>(std::max(0, e.level - 1));
        inl::Paragraph title = inl::Paragraph::plain(e.title, st);
        title.style.align = Align::Start;
        title.style.firstLineIndent = 0.0f;
        title.style.lineHeight = toc.lineHeight;
        const Pt L = region().lineLength();
        const Pt titleLen = std::max(1.0f, L - indent - numberWidth - gap);
        const Pt need = measureParagraph(title, titleLen);
        if (region().remaining() + kEps < need && !region().fresh()) {
            nextRegion();
            if (aborted()) return;
        }
        Region& reg = region();
        inl::ParagraphFragment frag;
        const Pt used = placeParagraphAt(title, 0.0f, indent, titleLen, std::nullopt, &frag);
        if (frag.lines.empty()) continue;
        const Pt pitch = frag.linePitch;
        const Pt lastLineOffset = frag.lineCenterOffset(frag.lines.size() - 1);

        // ページ番号は最後の行の右端
        inl::Paragraph num = inl::Paragraph::plain(toU16(e.page), st);
        num.style.align = Align::End;
        num.style.firstLineIndent = 0.0f;
        num.style.lineHeight = toc.lineHeight;
        placeParagraphAt(num, lastLineOffset, 0.0f, L);

        // 点線: 見出しの最後の行の末尾からページ番号の手前まで
        if (toc.leader) {
            const inl::LineBox& last = frag.lines.back();
            const Pt from = indent + last.indent + last.naturalLength + st.size * 0.5f;
            const Pt to = L - numberWidth - st.size * 0.3f;
            const Pt dot = 0.9f;
            const Pt center = reg.used + lastLineOffset + pitch * 0.5f + st.size * 0.3f;
            for (Pt x = from; x + dot <= to; x += 3.0f) {
                fillLogicalRect(x, x + dot, center - dot * 0.5f, center + dot * 0.5f, st.fill);
            }
        }
        reg.used += used;
        pageHasContent_ = true;
    }
    applySpaceAfter(toc.block.spaceAfter);
    pending_.reset();
}

//------------------------------------------------------------------------------
// 段抜き（spanColumns）と段のバランス取り
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
 * このページの段の内容の高さを揃える（endBlock の前まで）
 *
 * ページ先頭の再開点から、段の長さを H に縮めて組み直す。入らなければ H を増やして繰り返す。
 * 元の長さで入っていた内容なので H = 元の長さ で必ず収まる。
 */
void Flower::balanceColumns(size_t endBlock) {
    const PageStart start = pageStart_;
    const size_t itemStart = pageItemStart_;
    const std::vector<Region> base = pageBaseRegions_;
    if (base.size() < 2) return;
    const Pt fullExtent = base[0].blockExtent();
    const int ncol = static_cast<int>(base.size());

    Pt total = 0.0f;
    for (const Region& r : regions_) total += r.used;
    Pt H = std::min(fullExtent, total / static_cast<float>(ncol));

    const size_t savedBlock = curBlockIndex_;
    const int savedPara = curParaIndex_;
    const size_t savedChar = curCharStart_;
    const bool savedLabel = curLabelPlaced_;
    // 再組みで採番が進まないように、再開点の採番状態から組み直す
    const NumberingState startNumbering = pageNumbering_;

    for (int iter = 0; iter < 40; ++iter) {
        page().dl.items.resize(itemStart);
        regions_ = base;
        for (Region& r : regions_) setRegionExtent(r, H);
        regionIndex_ = 0;
        pending_.reset();
        pageHasContent_ = itemStart > 0;
        restoreNumbering(startNumbering);

        trial_ = true;
        trialOverflow_ = false;
        replayPage(start, endBlock);
        trial_ = false;

        if (!trialOverflow_ || H >= fullExtent - kEps) break;
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
        if (pageHasContent_) trialOverflow_ = true;
        return;
    }

    Pt top = 0.0f;   // 段の内容が終わる位置（段の始端から）
    if (pageHasContent_) {
        balanceColumns(curBlockIndex_);
        for (const Region& r : regions_) top = std::max(top, r.used);
    }

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

    blockItemStart_ = page().dl.items.size();
    placeContent(blk, style, nullptr);
    const Pt consumed = regions_[0].used + style.spaceAfter;

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
    pageNumbering_ = snapshotNumbering();
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

    const float pw = static_cast<float>(img.image->width);
    const float ph = static_cast<float>(img.image->height);
    Pt w = img.size.w, h = img.size.h;
    if (w <= 0.0f && h <= 0.0f) { w = pw; h = ph; }
    else if (w <= 0.0f) w = h * pw / ph;
    else if (h <= 0.0f) h = w * ph / pw;

    const bool vertical = isVertical(wm());
    auto inlineExtent = [&]() { return vertical ? h : w; };
    auto blockExtent = [&]() { return vertical ? w : h; };
    {
        const Region& reg = region();
        const float s = std::min(1.0f, std::min(reg.lineLength() / std::max(1.0f, inlineExtent()),
                                                reg.blockExtent() / std::max(1.0f, blockExtent())));
        if (s < 1.0f) { w *= s; h *= s; }
    }

    // 図番号
    std::optional<inl::Paragraph> caption;
    std::u16string number;
    if (img.caption) {
        ++figureCount_;
        number = toU16(figureCount_);
        std::map<std::u16string, std::u16string> f = fields_;
        f[u"fig"] = substitute(inl::Paragraph::plain(opts_.figureFormat, TextStyle{}), {{u"n", number}}).text();
        caption = substitute(*img.caption, f);
    }

    Pt captionExtent = 0.0f;
    if (caption) captionExtent = img.captionGap + measureParagraph(*caption, inlineExtent());
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

    const Rect rect = reg.toRect(in0, in0 + inlineExtent(), reg.used, reg.used + blockExtent());
    dl::ImageItem item;
    item.image = img.image;
    item.xform = multiply(Matrix::translation(rect.x, rect.y),
                          Matrix::scaling(rect.w / pw, rect.h / ph));
    page().dl.add(item);

    if (caption) {
        placeParagraphAt(*caption, blockExtent() + img.captionGap, in0, inlineExtent(),
                         caption->style.align == Align::Justify ? std::optional<Align>(Align::Center)
                                                                : std::nullopt);
    }
    if (!trial_) recordLabel(img.block.label, number);

    pageHasContent_ = true;
    pending_.reset();

    if (img.placement == block::ImagePlacement::Block) {
        reg.used += total;
        applySpaceAfter(img.block.spaceAfter);
    } else {
        const Pt exIn0 = (img.placement == block::ImagePlacement::FloatEnd) ? in0 - img.gap : in0;
        const Pt exIn1 = (img.placement == block::ImagePlacement::FloatStart) ? in0 + inlineExtent() + img.gap
                                                                              : in0 + inlineExtent();
        reg.exclusions.push_back(reg.toRect(exIn0, exIn1, reg.used, reg.used + total + img.gap));
    }
}

//------------------------------------------------------------------------------
// 外部オブジェクト（別行立ての数式など）
//------------------------------------------------------------------------------

void Flower::placeObject(const block::ObjectBlock& ob) {
    ensureRegion();
    applySpaceBefore(ob.block.spaceBefore);
    const bool vertical = isVertical(wm());

    // 式番号（採番はハンドラの成否に関わらず進める: 参照が安定するように）。
    // {ref:label} は書式込みの番号「(1)」になる（「式 (1)」と書けるように）
    std::u16string number;
    std::optional<inl::Paragraph> numberPara;
    if (ob.numbered) {
        ++equationCount_;
        number = substitute(inl::Paragraph::plain(opts_.equationFormat, TextStyle{}),
                            {{u"n", toU16(equationCount_)}}).text();
        numberPara = inl::Paragraph::plain(number, ob.textStyle);
        numberPara->style.align = Align::End;
    }
    std::optional<inl::Paragraph> caption;
    if (ob.caption) {
        std::map<std::u16string, std::u16string> f = fields_;
        f[u"eq"] = number;
        caption = substitute(*ob.caption, f);
    }

    // ハンドラを呼ぶ
    std::shared_ptr<const obj::ObjectResult> res;
    {
        obj::ObjectRequest req;
        req.handler = ob.handler;
        req.source = ob.source;
        req.params = ob.params;
        req.maxInline = region().lineLength();
        req.maxBlock = region().blockExtent();
        req.fontSize = ob.textStyle.size;
        req.writingMode = wm();
        req.inlineContext = false;
        if (opts_.objects) res = opts_.objects->render(req);
    }
    if (!res || !res->ok()) {
        // 代替テキストを段落として置く
        const std::u16string msg = u"[" + toU16(ob.handler) + u": " +
                                   toU16(res ? res->error : std::string("no handler")) + u"]";
        inl::Paragraph alt = inl::Paragraph::plain(msg, ob.textStyle);
        alt.style.align = Align::Start;
        block::BlockStyle st = ob.block;
        st.spaceBefore = 0.0f;
        flowParagraph(alt, st, nullptr, 0.0f, 0.0f, true, 0, false);
        if (!trial_) recordLabel(ob.block.label, number);
        return;
    }

    // 論理の箱（縦組みは横倒しなので幅が行方向）
    Pt inlineExtent = res->size.w, blockExtent = res->size.h;
    Pt numberWidth = 0.0f;
    if (numberPara) {
        Pt natural = 0.0f;
        inl::Paragraph probe = *numberPara;
        probe.style.align = Align::Start;              // 自然長を測る（End 揃えだと字下げが載る）
        measureParagraph(probe, region().lineLength(), &natural);
        numberWidth = natural + ob.textStyle.size;     // 番号と式の間に 1 字あける
    }
    float scale = 1.0f;
    {
        const Region& reg = region();
        const Pt avail = std::max(1.0f, reg.lineLength() - numberWidth * 2.0f);
        scale = std::min(1.0f, std::min(avail / std::max(1.0f, inlineExtent),
                                        reg.blockExtent() / std::max(1.0f, blockExtent)));
        inlineExtent *= scale;
        blockExtent *= scale;
    }
    Pt captionExtent = 0.0f;
    if (caption) captionExtent = ob.captionGap + measureParagraph(*caption, region().lineLength());
    const Pt total = blockExtent + captionExtent;

    if (region().remaining() + kEps < total && !region().fresh()) {
        nextRegion();
        if (aborted()) return;
    }
    Region& reg = region();
    const Pt L = reg.lineLength();
    Pt in0 = 0.0f;
    if (ob.align == Align::Center) in0 = (L - inlineExtent) * 0.5f;
    else if (ob.align == Align::End) in0 = L - inlineExtent - numberWidth;
    in0 = std::max(0.0f, in0);

    const Rect box = reg.toRect(in0, in0 + inlineExtent, reg.used, reg.used + blockExtent);
    dl::Group grp = inl::objectGroup(*res, box, vertical);
    if (scale < 1.0f) grp.xform = multiply(grp.xform, Matrix::scaling(scale, scale));
    page().dl.add(std::move(grp));

    if (numberPara) {
        // 式の行送り方向の中央に番号の行の中心を合わせて、行末へ
        const Pt ext = measureParagraph(*numberPara, L);
        placeParagraphAt(*numberPara, std::max(0.0f, (blockExtent - ext) * 0.5f), 0.0f, L, Align::End);
    }
    if (caption) {
        placeParagraphAt(*caption, blockExtent + ob.captionGap, 0.0f, L,
                         caption->style.align == Align::Justify ? std::optional<Align>(Align::Center)
                                                                : std::nullopt);
    }
    if (!trial_) recordLabel(ob.block.label, number);

    pageHasContent_ = true;
    pending_.reset();
    reg.used += total;
    applySpaceAfter(ob.block.spaceAfter);
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
    std::vector<std::vector<int>> owner(nrow);
    size_t ncol = table.columns.size();
    {
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

    // --- 列幅 ---
    std::vector<Pt> widths(ncol, 0.0f), natural(ncol, 0.0f), minimum(ncol, 0.0f);
    for (const GCell& g : cells) {
        if (g.colspan != 1 || columns[g.col].width > 0.0f) continue;
        Pt nat = 0.0f, mn = 0.0f;
        for (const inl::Paragraph& p : g.cell->paras) {
            Pt a = 0.0f, b2 = 0.0f;
            measureParagraph(p, 1.0e6f, &a);
            measureParagraph(p, 1.0f, &b2);
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

    // --- 行の高さ ---
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

    // --- 行のグループ ---
    std::vector<size_t> groupEnd(nrow, 0);
    for (size_t r = 0; r < nrow; ++r) groupEnd[r] = r;
    for (const GCell& g : cells) {
        for (int dr = 0; dr < g.rowspan; ++dr) {
            groupEnd[g.row + dr] = std::max(groupEnd[g.row + dr], g.row + g.rowspan - 1);
        }
    }

    size_t headerCount = 0;
    while (headerCount < nrow && table.rows[headerCount].header) ++headerCount;

    const block::TableBorders& b = table.borders;
    const Pt tableLeft = colStart.front() - b.outer * 0.5f;
    const Pt tableRight = colStart.back() + b.outer * 0.5f;

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
    auto vRules = [&](size_t r, Pt block0, Pt block1) {
        if (!b.vertical) return;
        for (size_t c = 0; c <= ncol; ++c) {
            const bool edge = (c == 0 || c == ncol);
            if (!edge && owner[r][c - 1] == owner[r][c]) continue;
            const Pt t = edge ? b.outer : b.inner;
            fillLogicalRect(colStart[c] - t * 0.5f, colStart[c] + t * 0.5f, block0, block1, b.color);
        }
    };
    auto bottomRule = [&]() {
        if (!b.horizontal) return;
        fillLogicalRect(tableLeft, tableRight, region().used - b.outer * 0.5f,
                        region().used + b.outer * 0.5f, b.color);
    };
    auto valignOffset = [&](const GCell& g, Pt spanExtent) -> Pt {
        const Pt slack = std::max(0.0f, spanExtent - g.height);
        switch (g.cell->valign) {
        case block::VAlign::Middle: return slack * 0.5f;
        case block::VAlign::Bottom: return slack;
        default: return 0.0f;
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
            for (const GCell& g : cells) {
                if (g.row != r) continue;
                Pt spanExtent = 0.0f;
                for (int dr = 0; dr < g.rowspan; ++dr) spanExtent += rowH[r + dr];
                const Pt cellWidth = colStart[g.col + g.colspan] - colStart[g.col] - 2.0f * pad;
                Pt off = pad + valignOffset(g, spanExtent);
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

    // 段より高い 1 行を、段の境で分けながら置く（rowspan の無い行だけ）
    auto placeSplitRow = [&](size_t r, bool first, bool afterHeader) {
        struct Cursor { int para = 0; size_t charStart = 0; bool done = false; };
        std::vector<Cursor> cursors(cells.size());
        for (size_t i = 0; i < cells.size(); ++i) cursors[i].done = (cells[i].row != r);
        bool firstSeg = true;
        for (int guard = 0; guard < 1000; ++guard) {
            Region& reg = region();
            const Pt ruleAbove = firstSeg ? (first ? b.outer : (afterHeader ? b.headerRule : b.inner)) : 0.0f;
            const Pt top = reg.used;
            const Pt avail = reg.remaining() - ruleAbove - 2.0f * pad;
            if (avail <= 0.0f && !reg.fresh()) {
                nextRegion();
                if (aborted()) return;
                continue;
            }
            if (firstSeg) hRuleAbove(r, top, ruleAbove, first);
            Pt segExtent = 0.0f;
            bool remaining = false;
            for (size_t i = 0; i < cells.size(); ++i) {
                const GCell& g = cells[i];
                if (cursors[i].done) continue;
                const Pt cellWidth = colStart[g.col + g.colspan] - colStart[g.col] - 2.0f * pad;
                Pt off = pad;
                while (cursors[i].para < static_cast<int>(g.cell->paras.size())) {
                    const inl::Paragraph p = resolved(g.cell->paras[cursors[i].para]);
                    const Pt pitch = p.style.resolvedLinePitch(p.baseStyle().size);
                    const int fit = static_cast<int>(std::floor((avail - off + pad + kEps) / pitch));
                    if (fit <= 0) break;
                    const inl::ConstantLineShape shape(std::max(1.0f, cellWidth));
                    const inl::ParagraphFragment frag =
                        layoutFitting(p, shape, cursors[i].charStart, fit, avail - off + pad);
                    if (frag.lines.empty()) break;
                    const Point origin = reg.lineOrigin(off, pitch, colStart[g.col] + pad);
                    inl::emitParagraph(page().dl, frag, wm(), origin);
                    off += frag.blockExtent();
                    if (frag.complete) {
                        cursors[i].para += 1;
                        cursors[i].charStart = 0;
                    } else {
                        cursors[i].charStart = frag.charEnd;
                        break;
                    }
                }
                if (cursors[i].para >= static_cast<int>(g.cell->paras.size())) cursors[i].done = true;
                else remaining = true;
                segExtent = std::max(segExtent, off + pad);
            }
            if (remaining) segExtent = std::max(segExtent, avail + 2.0f * pad);
            vRules(r, top, top + segExtent);
            reg.used = top + segExtent;
            pageHasContent_ = true;
            firstSeg = false;
            if (!remaining) return;
            bottomRule();
            nextRegion();
            if (aborted()) return;
            if (table.repeatHeader && headerCount > 0 && r >= headerCount) {
                placeRows(0, headerCount - 1, true, false, true);
            }
        }
    };

    // --- キャプション（表の上） ---
    std::optional<inl::Paragraph> caption;
    std::u16string number;
    if (table.caption) {
        ++tableCount_;
        number = toU16(tableCount_);
        std::map<std::u16string, std::u16string> f = fields_;
        f[u"table"] = substitute(inl::Paragraph::plain(opts_.tableFormat, TextStyle{}), {{u"n", number}}).text();
        caption = substitute(*table.caption, f);
    }
    {
        // キャプション＋ヘッダ行＋最初の本文の塊が入らなければ、先に次の段へ（ヘッダだけが段末に残らないように）
        Pt need = 0.0f;
        if (caption) need += measureParagraph(*caption, L) + table.captionGap;
        const size_t firstBody = std::min(headerCount, nrow - 1);
        const size_t g1 = std::max(groupEnd[firstBody], firstBody);
        for (size_t r = 0; r <= g1; ++r) need += rowH[r];
        need += b.outer;
        if (region().remaining() + kEps < need && !region().fresh() && need <= region().blockExtent()) {
            nextRegion();
            if (aborted()) return;
        }
        if (caption) {
            region().used += placeParagraphAt(*caption, 0.0f, 0.0f, L) + table.captionGap;
        }
        if (!trial_) recordLabel(table.block.label, number);
    }

    bool first = true;
    bool prevHeader = false;
    for (size_t r = 0; r < nrow;) {
        const size_t r1 = std::max(groupEnd[r], r);
        bool hasRowspanStart = false;
        for (const GCell& g : cells) if (g.row == r && g.rowspan > 1) hasRowspanStart = true;
        const Pt groupExtent = [&]() { Pt e = 0.0f; for (size_t k = r; k <= r1; ++k) e += rowH[k]; return e; }();
        const bool tooTall = groupExtent + b.outer > region().blockExtent();

        if (r1 == r && !hasRowspanStart && tooTall) {
            if (!region().fresh() && region().remaining() < region().blockExtent() * 0.3f) {
                bottomRule();
                nextRegion();
                if (aborted()) return;
                first = true;
                prevHeader = false;
                if (table.repeatHeader && headerCount > 0 && r >= headerCount) {
                    placeRows(0, headerCount - 1, true, false, true);
                    first = false;
                    prevHeader = true;
                }
            }
            placeSplitRow(r, first, prevHeader);
            if (aborted()) return;
        } else if (!placeRows(r, r1, first, prevHeader, false)) {
            bottomRule();
            nextRegion();
            if (aborted()) return;
            first = true;
            prevHeader = false;
            if (table.repeatHeader && headerCount > 0 && r >= headerCount) {
                placeRows(0, headerCount - 1, true, false, true);
                first = false;
                prevHeader = true;
            }
            placeRows(r, r1, first, prevHeader, true);
        }
        first = false;
        prevHeader = table.rows[r1].header;
        r = r1 + 1;
    }
    bottomRule();
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

void Flower::flowParagraph(const inl::Paragraph& paraIn, const block::BlockStyle& style,
                           const inl::Paragraph* labelIn, Pt labelWidth, Pt labelGap,
                           bool firstOfBlock, size_t charStart0, bool labelPlaced0) {
    const inl::Paragraph para = resolved(paraIn);
    std::optional<inl::Paragraph> labelResolved;
    if (labelIn) labelResolved = resolved(*labelIn);
    const inl::Paragraph* label = labelResolved ? &*labelResolved : nullptr;

    const Pt pitch = para.style.resolvedLinePitch(para.baseStyle().size);
    const Pt bodyIndent = (labelWidth > 0.0f || label) ? labelWidth + labelGap : 0.0f;
    const bool decorated = style.background.has_value() || style.padding > 0.0f;

    size_t charStart = charStart0;
    bool labelPlaced = (label == nullptr) || labelPlaced0;
    bool paddedStart = charStart0 > 0;   // 続きから組むときは前の段で先頭の余白は済んでいる
    int guard = 0;

    while (true) {
        if (++guard > 10000) break;   // 保険
        curCharStart_ = charStart;
        curLabelPlaced_ = labelPlaced;

        Region& reg = region();

        bool forceHere = false;   // 巻き取った先が空のページなら、ここに置くしかない
        auto moveToNextRegion = [&]() {
            if (charStart == 0 && pending_ && pending_->blk && pending_->regionIndex == regionIndex_) {
                const Pending pend = *pending_;
                pending_.reset();
                page().dl.items.resize(pend.itemStart);
                region().used = pend.usedBefore;
                restoreNumbering(pend.numbering);
                const size_t myBlock = curBlockIndex_;
                const int myPara = curParaIndex_;
                curBlockIndex_ = pend.blockIndex;
                curParaIndex_ = 0;
                curCharStart_ = 0;
                curLabelPlaced_ = false;
                if (pageEmpty() && regionIndex_ == 0) {
                    // 見出しだけのページだった: 次へ送っても同じことになるので、ここに置き直す
                    pageStart_ = PageStart{pend.blockIndex, 0, 0, false};
                    pageNumbering_ = snapshotNumbering();
                    forceHere = true;
                } else {
                    nextRegion();
                }
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

        // keepTogether: 全体が入らなければ次の段へ（段より大きければ諦めて分ける）
        if (charStart == 0 && style.keepTogether && !reg.fresh()) {
            const Pt need = measureParagraph(para, reg.lineLength() - bodyIndent) + 2.0f * style.padding;
            if (need > reg.remaining() + kEps && need <= reg.blockExtent()) {
                moveToNextRegion();
                if (aborted()) return;
                continue;
            }
        }

        // 先頭の余白（背景付き）
        if (decorated && !paddedStart && style.padding > 0.0f) {
            if (reg.remaining() < style.padding + pitch && !reg.fresh()) {
                moveToNextRegion();
                if (aborted()) return;
                continue;
            }
        }
        const Pt segTop = reg.used;   // 背景の始端（余白込み）
        if (decorated && !paddedStart && style.padding > 0.0f) reg.used += style.padding;
        paddedStart = true;

        int fit = linesThatFit(pitch);
        if (fit <= 0) {
            if (reg.fresh() || forceHere ||
                (decorated && reg.used - segTop >= style.padding && reg.used - style.padding <= kEps)) {
                fit = 1;   // 段より行送りが大きい: 1 行だけ置いて進める
            } else {
                reg.used = segTop;
                moveToNextRegion();
                if (aborted()) return;
                continue;
            }
        }

        const RegionLineShape shape(reg, reg.used, pitch, bodyIndent);
        inl::ParagraphFragment frag = layoutFitting(para, shape, charStart, fit, reg.remaining());
        const bool starting = (charStart == 0);

        // 1 行でも入らない（行内オブジェクトで行送りが広がった）: 次の段へ
        if (frag.lines.size() == 1 && frag.blockExtent() > reg.remaining() + kEps &&
            !reg.fresh() && !forceHere) {
            reg.used = segTop;
            moveToNextRegion();
            if (aborted()) return;
            continue;
        }

        if (starting && !frag.complete && static_cast<int>(frag.lines.size()) < style.orphans &&
            !reg.fresh() && !forceHere) {
            reg.used = segTop;
            moveToNextRegion();
            if (aborted()) return;
            continue;
        }

        if (!frag.complete && style.widows > 1 && !forceHere) {
            const inl::ParagraphFragment rest = layouter_.layout(para, wm(), shape, frag.charEnd, -1);
            const int remaining = static_cast<int>(rest.lines.size());
            if (remaining < style.widows) {
                const int pull = style.widows - remaining;
                const int keep = static_cast<int>(frag.lines.size()) - pull;
                if (keep >= style.orphans && keep > 0) {
                    frag = layouter_.layout(para, wm(), shape, charStart, keep);
                } else if (starting && !reg.fresh()) {
                    reg.used = segTop;
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

        Pt consumed = frag.blockExtent();
        if (!labelPlaced) {
            inl::Paragraph lp = *label;
            lp.style.firstLineIndent = 0.0f;
            lp.style.linePitch = pitch;
            Pt s0 = 0.0f, e0 = 0.0f;
            reg.freeInlineRange(reg.used, reg.used + pitch, s0, e0);
            const inl::ConstantLineShape lshape(labelWidth);
            const inl::ParagraphFragment lfrag = layouter_.layout(lp, wm(), lshape);
            inl::emitParagraph(page().dl, lfrag, wm(), reg.lineOrigin(0.0f, pitch, s0));
            consumed = std::max(consumed, pitch * static_cast<float>(lfrag.lines.size()));
            labelPlaced = true;
        }
        reg.used += consumed;

        // 背景（末尾の余白は最後の断片だけ）
        if (decorated) {
            if (frag.complete && style.padding > 0.0f) reg.used += style.padding;
            if (style.background) {
                fillLogicalRect(0.0f, reg.lineLength(), segTop, reg.used, *style.background, itemStart);
            }
        }
        pageHasContent_ = true;

        if (firstOfBlock && style.keepWithNext && charStart0 == 0) {
            Pending pend;
            pend.regionIndex = regionIndex_;
            pend.itemStart = std::min(itemStart, blockItemStart_);
            pend.usedBefore = std::min(usedBefore, segTop);
            pend.blockIndex = curBlockIndex_;
            pend.numbering = blockNumbering_;
            pending_ = pend;   // blk は placeBlock が埋める
        } else {
            pending_.reset();
        }

        forceHere = false;
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
    // 最終ページの段揃え
    if (opts_.balanceLastPage && columns_ > 1 && pageHasContent_ && regions_.size() > 1) {
        bool allFull = true;
        for (const Region& r : regions_) if (r.remaining() > r.blockExtent() * 0.1f) allFull = false;
        if (!allFull) balanceColumns(flow_.blocks.size());
    }
    finishPage();
    collected_.totalPages = static_cast<int>(pages_.size());
    return std::move(pages_);
}

} // namespace

std::vector<Page> FlowLayouter::layout(const block::Flow& flow, const PageSequence& seq,
                                       const FlowLayoutOptions& opts) {
    // 相互参照・目次・{pages} があれば 2〜3 パス。番号とページが安定したら止める
    bool multiPass = false;
    auto uses = [&](const std::optional<RunningText>& rt) {
        if (!rt) return;
        for (const inl::InlineRun& r : rt->para.runs) {
            if (r.text.find(u"{pages}") != std::u16string::npos) multiPass = true;
        }
    };
    uses(seq.master.header);
    uses(seq.master.footer);
    for (const block::Block& blk : flow.blocks) {
        if (std::get_if<block::TocBlock>(&blk)) multiPass = true;
        const block::BlockStyle* st = styleOf(blk);
        if (st && !st->label.empty()) multiPass = true;
    }

    RefInfo info;
    std::vector<Page> pages;
    const int maxPasses = multiPass ? 3 : 1;
    for (int pass = 0; pass < maxPasses; ++pass) {
        Flower flower(fonts_, flow, seq, opts, pass == 0 ? nullptr : &info);
        pages = flower.run(pass == 0 ? 0 : info.totalPages);
        const RefInfo& out = flower.collected();
        if (pass > 0 && out == info) break;
        info = out;
    }
    return pages;
}

} // namespace typeset::page
