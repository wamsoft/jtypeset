/**
 * paragraph_layouter.cpp — 段落の組版と表示リストへの出力
 */

#include "typeset/inl/paragraph.hpp"

#include "color_glyph.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "typeset/inl/item_builder.hpp"
#include "typeset/inl/shaper.hpp"

namespace typeset::inl {

namespace {

/// 行内オブジェクト／画像が行送りの箱から出るぶんを、行の前後の追加の送りにする
void computeExtraLeading(LineBox& line, Pt pitch, bool vertical) {
    Pt top = -pitch * 0.5f, bottom = pitch * 0.5f;
    bool any = false;
    for (const PlacedGlyph& g : line.glyphs) {
        if (g.object) {
            top = std::min(top, g.block);
            bottom = std::max(bottom, g.block + g.object->size.h);
            any = true;
        } else if (g.image || g.placeholder) {
            const Pt half = vertical ? g.imageSize.w * 0.5f : g.imageSize.h * 0.5f;
            top = std::min(top, g.block - half);
            bottom = std::max(bottom, g.block + half);
            any = true;
        }
    }
    if (!any) return;
    line.extraBefore = std::max(0.0f, -top - pitch * 0.5f);
    line.extraAfter = std::max(0.0f, bottom - pitch * 0.5f);
}

/**
 * UAX #9 L2: 行の中の箱・グルーをレベルで視覚順に並べ替え、グリフの inline_ を付け直す。
 * グルーのレベルは直前の箱（行頭なら直後の箱）に従う
 */
template <class Piece>
void reorderBidi(LineBox& line, std::vector<Piece>& pieces, int paragraphLevel) {
    // グルーにレベルを与える
    int lastLevel = -1;
    for (Piece& p : pieces) {
        if (p.box) lastLevel = p.level;
        else if (p.level < 0 && lastLevel >= 0) p.level = lastLevel;
    }
    for (size_t i = pieces.size(); i-- > 0;) {
        if (pieces[i].level < 0) pieces[i].level = (i + 1 < pieces.size()) ? pieces[i + 1].level : paragraphLevel;
    }
    int maxLevel = 0, minOdd = 255;
    for (const Piece& p : pieces) {
        maxLevel = std::max(maxLevel, p.level);
        if (p.level & 1) minOdd = std::min(minOdd, p.level);
    }
    if (maxLevel == 0) return;
    std::vector<size_t> order(pieces.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    const int lowest = std::min(minOdd, maxLevel);
    for (int lvl = maxLevel; lvl >= lowest && lvl >= 1; --lvl) {
        size_t i = 0;
        while (i < order.size()) {
            if (pieces[order[i]].level < lvl) { ++i; continue; }
            size_t j = i;
            while (j < order.size() && pieces[order[j]].level >= lvl) ++j;
            std::reverse(order.begin() + static_cast<ptrdiff_t>(i), order.begin() + static_cast<ptrdiff_t>(j));
            i = j;
        }
    }
    Pt v = 0.0f;
    for (size_t k : order) {
        const Piece& p = pieces[k];
        const Pt delta = v - p.start;
        if (p.box && delta != 0.0f) {
            for (size_t g = p.glyphBegin; g < p.glyphEnd; ++g) line.glyphs[g].inline_ += delta;
        }
        v += p.width;
    }
}

/**
 * 段落の字下げ: 段落の 1 行目は firstIndent、それ以降は hangingIndent。Indent 注記による行ごとの上書き
 * （lineIndents: 行番号 → 字下げ pt。負なら上書き無し）
 */
class ParagraphIndentShape : public LineShapeProvider {
public:
    ParagraphIndentShape(const LineShapeProvider& base, int firstLine, Pt firstIndent, Pt hangingIndent,
                         const std::vector<Pt>& lineIndents)
        : base_(base), firstLine_(firstLine), first_(firstIndent), hanging_(hangingIndent), lineIndents_(lineIndents) {}
    LineShape at(int lineIndex) const override { return at(lineIndex, 0.0f); }
    LineShape at(int lineIndex, Pt extraBlock) const override {
        LineShape s = base_.at(lineIndex, extraBlock);
        Pt indent = (lineIndex == firstLine_) ? first_ : hanging_;
        if (lineIndex >= 0 && static_cast<size_t>(lineIndex) < lineIndents_.size() && lineIndents_[lineIndex] >= 0.0f) {
            indent = lineIndents_[lineIndex] + (lineIndex == firstLine_ ? first_ : 0.0f);
        }
        if (indent != 0.0f) {
            s.length -= indent;
            s.indent += indent;
        }
        return s;
    }
private:
    const LineShapeProvider& base_;
    int firstLine_;
    Pt first_;
    Pt hanging_;
    const std::vector<Pt>& lineIndents_;
};

} // namespace

dl::Group objectGroup(const obj::ObjectResult& ob, const Rect& box, bool sideways) {
    dl::Group grp;
    if (!sideways) {
        grp.xform = Matrix::translation(box.x, box.y);
    } else {
        // 時計回りに 90°: ローカル (x, y) → (box.right − y, box.y + x)
        grp.xform.xx = 0.0f; grp.xform.xy = -1.0f; grp.xform.dx = box.right();
        grp.xform.yx = 1.0f; grp.xform.yy = 0.0f;  grp.xform.dy = box.y;
    }
    grp.children = ob.items;
    return grp;
}

std::u16string Paragraph::text() const {
    std::u16string t;
    for (const InlineRun& r : runs) t += r.text;
    return t;
}

const TextStyle& Paragraph::baseStyle() const {
    static const TextStyle kDefault;
    return runs.empty() ? kDefault : runs.front().style;
}

//------------------------------------------------------------------------------

namespace {

/// 段落全体のスタイル区間を [from, to) に切り出して、from 基準へずらす
std::vector<StyleRun> clipRuns(const std::vector<StyleRun>& runs, size_t from, size_t to) {
    std::vector<StyleRun> out;
    for (const StyleRun& r : runs) {
        const size_t s = std::max(r.start, from);
        const size_t e = std::min(r.end, to);
        if (e <= s) continue;
        out.push_back(StyleRun{s - from, e - from, r.styleIndex});
    }
    return out;
}

std::vector<Annotation> clipAnnotations(const std::vector<Annotation>& anns, size_t from, size_t to) {
    std::vector<Annotation> out;
    for (const Annotation& a : anns) {
        if (a.end <= from || a.start >= to) continue;
        Annotation shifted = a;
        shifted.start = std::max(a.start, from) - from;
        shifted.end = std::min(a.end, to) - from;
        out.push_back(std::move(shifted));
    }
    return out;
}

} // namespace

namespace {

/// preserveSpaces の段落でタブをタブ位置（tabWidth 桁）までの空白に展開する。注記の位置もずらす
/// （シェイパはタブを幅 0 のグリフにするので、そのままだと消える）
Paragraph expandTabs(const Paragraph& para) {
    bool any = false;
    for (const InlineRun& r : para.runs) if (r.text.find(u'\t') != std::u16string::npos) { any = true; break; }
    if (!any) return para;
    const int tabWidth = std::max(1, para.style.tabWidth);
    Paragraph out = para;
    std::vector<std::pair<size_t, size_t>> insertions;   // (元の位置, 増えた文字数)
    size_t pos = 0;      // 元テキストでの位置
    size_t column = 0;   // 行頭からの桁
    for (InlineRun& r : out.runs) {
        std::u16string t;
        t.reserve(r.text.size());
        for (char16_t c : r.text) {
            if (c == u'\n') {
                column = 0;
                t.push_back(c);
            } else if (c == u'\t') {
                const size_t n = static_cast<size_t>(tabWidth) - column % static_cast<size_t>(tabWidth);
                t.append(n, u' ');
                column += n;
                if (n > 1) insertions.emplace_back(pos, n - 1);
            } else {
                t.push_back(c);
                ++column;
            }
            ++pos;
        }
        r.text = std::move(t);
    }
    for (Annotation& a : out.annotations) {
        size_t ds = 0, de = 0;
        for (const auto& [p, n] : insertions) {
            if (a.start > p) ds += n;
            if (a.end > p) de += n;
        }
        a.start += ds;
        a.end += de;
    }
    return out;
}

} // namespace

/**
 * 省略記号: 行数上限で切れた最後の行の末尾を落として「…」を置く（組み直さず、行のグリフを削る）。
 * 双方向の行では末尾＝送り方向の終端とみなす
 */
void ParagraphLayouter::applyEllipsis(ParagraphFragment& frag, const Paragraph& para, WritingMode wm,
                                      const LineShapeProvider& shape) {
    LineBox& line = frag.lines.back();
    const uint32_t lastStyle = line.glyphs.empty() ? 0u : line.glyphs.back().styleIndex;
    const TextStyle& st = lastStyle < frag.styles.size() ? frag.styles[lastStyle] : frag.styles.front();
    const ShapedText ell = shapeText(para.style.ellipsis, st, fonts_, wm, para.style.orientation);
    if (ell.glyphs.empty()) return;
    const Pt avail = shape.at(line.lineIndex).length - line.indent;
    // 本文の文字（注記以外）を後ろから落として、省略記号が入る所を探す
    auto endOf = [&]() {
        Pt e = 0.0f;
        for (const PlacedGlyph& g : line.glyphs) if (!g.annotation) e = std::max(e, g.inline_ + g.boxAfter);
        return e;
    };
    while (!line.glyphs.empty() && endOf() + ell.advance > avail + 0.01f) {
        uint32_t maxChar = 0;
        for (const PlacedGlyph& g : line.glyphs) maxChar = std::max(maxChar, g.charIndex);
        // maxChar の文字（とその注記）を落とす
        line.glyphs.erase(std::remove_if(line.glyphs.begin(), line.glyphs.end(),
                                         [&](const PlacedGlyph& g) { return g.charIndex == maxChar; }),
                          line.glyphs.end());
        line.charEnd = maxChar;
    }
    const Pt at = endOf();
    for (PlacedGlyph g : ell.glyphs) {
        g.inline_ += at;
        g.charIndex = std::numeric_limits<uint32_t>::max();   // 元テキストに無い
        g.styleIndex = lastStyle;
        line.glyphs.push_back(std::move(g));
    }
    line.naturalLength = at + ell.advance;
    line.length = line.naturalLength;
}

ParagraphFragment ParagraphLayouter::layout(const Paragraph& paraIn, WritingMode wm,
                                            const LineShapeProvider& shape,
                                            size_t charStart, int maxLines,
                                            int firstLineIndex) {
    const Paragraph para = paraIn.style.preserveSpaces ? expandTabs(paraIn) : paraIn;
    // 行内オブジェクトで行送りが広がると、後ろの行の実際の位置が「行番号 × 行送り」からずれる。
    // 排除領域（回り込み）を見る LineShapeProvider にそのずれを渡して組み直す（不動点まで、最大 3 回）
    std::vector<Pt> offsets;
    ParagraphFragment frag;
    // 途中からの字下げ（Indent 注記）: 行頭の文字が決まらないと字下げが決まらないので、組んでから行ごとの字下げを
    // 埋めて組み直す（不動点まで、最大 4 回）
    std::vector<const Annotation*> indents;
    for (const Annotation& a : para.annotations) if (a.type == AnnotationType::Indent) indents.push_back(&a);
    lineIndents_.clear();
    const Pt baseEm = para.baseStyle().size;
    for (int iter = 0; iter < 3 + (indents.empty() ? 0 : 4); ++iter) {
        const OffsetLineShape shifted(shape, offsets, firstLineIndex);
        frag = layoutOnce(para, wm, shifted, charStart, maxLines, firstLineIndex);
        bool indentChanged = false;
        if (!indents.empty()) {
            std::vector<Pt> want(frag.lines.size() + static_cast<size_t>(std::max(0, firstLineIndex)), -1.0f);
            for (size_t i = 0; i < frag.lines.size(); ++i) {
                const size_t li = i + static_cast<size_t>(std::max(0, firstLineIndex));
                for (const Annotation* a : indents) {
                    if (frag.lines[i].charStart >= a->start && frag.lines[i].charStart < a->end) want[li] = a->indentEm * baseEm;
                }
            }
            if (want != lineIndents_) {
                lineIndents_ = std::move(want);
                indentChanged = true;
            }
        }
        std::vector<Pt> next(frag.lines.size(), 0.0f);
        Pt acc = 0.0f;
        bool any = false;
        for (size_t i = 0; i < frag.lines.size(); ++i) {
            next[i] = acc + frag.lines[i].extraBefore;
            acc += frag.lines[i].extraBefore + frag.lines[i].extraAfter;
            if (next[i] != 0.0f) any = true;
        }
        if (indentChanged) { offsets = std::move(next); continue; }
        if (!any && offsets.empty()) break;
        if (next == offsets) break;
        offsets = std::move(next);
    }
    if (!frag.complete && !para.style.ellipsis.empty() && !frag.lines.empty()) applyEllipsis(frag, para, wm, shape);
    return frag;
}

ParagraphFragment ParagraphLayouter::layoutOnce(const Paragraph& para, WritingMode wm,
                                                const LineShapeProvider& shape,
                                                size_t charStart, int maxLines,
                                                int firstLineIndex) {
    ParagraphFragment frag;
    std::vector<std::shared_ptr<const dl::Image>> images;
    std::vector<Size> imageSizes;
    std::vector<std::shared_ptr<const obj::ObjectResult>> objects;
    for (const InlineRun& r : para.runs) {
        frag.styles.push_back(r.style);
        images.push_back(r.image);
        imageSizes.push_back(r.imageSize);
        objects.push_back(r.object);
        frag.placeholders.push_back(r.placeholder);
        frag.placeholderIds.push_back(r.placeholder ? r.placeholderId : std::string());
    }
    if (frag.styles.empty()) {
        frag.styles.push_back(TextStyle{});
        frag.placeholders.push_back(false);
        frag.placeholderIds.push_back(std::string());
    }
    for (const TextStyle& st : frag.styles) {
        StyleMetrics sm;
        if (std::shared_ptr<glyphware::Face> primary = fonts_.primary(st.font)) {
            sm.baseline = baselineOffset(*primary, st.size, WritingMode::HorizontalTb);
            sm.decoration = font::decorationMetrics(fonts_, *primary, st.size);
        } else {
            sm.baseline = st.size * 0.38f;
            sm.decoration.underlineOffset = st.size * 0.1f;
            sm.decoration.underlineThickness = st.size * 0.05f;
            sm.decoration.strikeoutOffset = -st.size * 0.3f;
            sm.decoration.strikeoutThickness = st.size * 0.05f;
        }
        frag.styleMetrics.push_back(sm);
    }

    const TextStyle& base = para.baseStyle();
    const Pt baseSize = base.size;
    frag.baseSize = baseSize;
    frag.linePitch = para.style.resolvedLinePitch(baseSize);
    frag.charStart = charStart;

    const std::u16string text = para.text();
    frag.text = std::make_shared<const std::u16string>(text);

    if (charStart > text.size()) {
        frag.charEnd = text.size();
        frag.complete = true;
        return frag;
    }

    // 段落全体のスタイル区間
    std::vector<StyleRun> allRuns;
    {
        size_t off = 0;
        for (uint32_t i = 0; i < para.runs.size(); ++i) {
            const size_t n = para.runs[i].text.size();
            allRuns.push_back(StyleRun{off, off + n, i});
            off += n;
        }
    }

    BreakOptions bo = para.style.lineBreak;
    if (para.style.align != Align::Justify) bo.justify = false;

    const Pt halfEm = baseSize * 0.5f;
    int lineIndex = firstLineIndex;
    size_t pos = charStart;
    // 続きから組むときも、直前が改行なら段落の頭（一字下げが付く）
    bool paraHead = (charStart == 0) || (charStart > 0 && text[charStart - 1] == u'\n');

    while (pos <= text.size()) {
        const size_t nl = text.find(u'\n', pos);
        const size_t end = (nl == std::u16string::npos) ? text.size() : nl;
        size_t trimmed = end;
        if (trimmed > pos && text[trimmed - 1] == u'\r') --trimmed;
        const size_t nextPos = (nl == std::u16string::npos) ? text.size() + 1 : nl + 1;

        if (trimmed <= pos) {
            // 空行もそのまま 1 行分を占める
            if (maxLines >= 0 && static_cast<int>(frag.lines.size()) >= maxLines) {
                frag.charEnd = pos;
                frag.complete = false;
                return frag;
            }
            LineBox line;
            line.charStart = pos;
            line.charEnd = pos;
            line.blockMin = -halfEm;
            line.blockMax = halfEm;
            line.lineIndex = lineIndex;
            line.indent = shape.at(lineIndex).indent;
            line.paragraphEnd = true;
            frag.lines.push_back(std::move(line));
            ++lineIndex;
        } else {
            const std::u16string sub = text.substr(pos, trimmed - pos);
            const std::vector<StyleRun> runs = clipRuns(allRuns, pos, trimmed);
            const std::vector<Annotation> anns = clipAnnotations(para.annotations, pos, trimmed);

            ShapeContext sctx{fonts_, wm, para.style.orientation, &frag.styles, &images, &imageSizes, &objects,
                              &frag.placeholders, para.style.direction};
            const ShapedText shaped = shapeText(sub, runs, sctx);

            ItemBuildContext ictx{fonts_, wm, para.style.orientation, &frag.styles, &base,
                                  base.letterSpacing, para.style.preserveSpaces, bo.wrap,
                                  &para.style.tabStops, para.style.tabWidth,
                                  bo.hyphenation, bo.hyphenPenalty, bo.hyphenMinLeft, bo.hyphenMinRight};
            const std::vector<LineItem> items =
                buildLineItems(shaped, anns, para.style.spacing, ictx);

            const Pt indentPt = paraHead ? para.style.firstLineIndent * baseSize : 0.0f;
            // 1 行目は一字下げ、2 行目以降はぶら下げインデント、途中からの字下げ（Indent 注記）は行頭の文字で決まる
            // （lineIndents_ は layout() の反復で埋める）
            const ParagraphIndentShape ishape(shape, lineIndex, indentPt, para.style.hangingIndent * baseSize,
                                              lineIndents_);
            const std::vector<BreakLine> breaks = breakLines(items, ishape, bo, lineIndex);

            for (size_t bi = 0; bi < breaks.size(); ++bi) {
                const BreakLine& br = breaks[bi];
                if (maxLines >= 0 && static_cast<int>(frag.lines.size()) >= maxLines) {
                    frag.charEnd = pos + items[br.itemStart].charIndex;
                    frag.complete = false;
                    return frag;
                }

                LineBox line;
                line.length = br.width;
                line.naturalLength = br.naturalWidth;
                line.blockMin = -halfEm;
                line.blockMax = halfEm;
                line.lineIndex = br.lineIndex;
                line.indent = br.shape.indent;
                line.charStart = pos + items[br.itemStart].charIndex;
                line.charEnd = line.charStart;
                line.paragraphEnd = (bi + 1 == breaks.size());
                if (br.itemEnd < items.size() && items[br.itemEnd].isPenalty() &&
                    items[br.itemEnd].width < 0.0f) {
                    line.hanging = true;
                    line.hangWidth = -items[br.itemEnd].width;
                }
                // ハイフネーションで切れた行は、行末にハイフンを足す
                const bool hyphenated = br.itemEnd < items.size() && items[br.itemEnd].isPenalty() &&
                                        !items[br.itemEnd].breakGlyphs.empty();
                // RTL の段落: 一字下げは終端側（右）に付くので行頭はずらさない（行長は短くなっている）。Start / End は入れ替わる
                const bool rtlPara = (shaped.paragraphLevel & 1) != 0;
                if (rtlPara) line.indent = 0.0f;
                // 両端揃え以外の揃え
                if (!bo.justify) {
                    const Pt slack = br.shape.length - br.naturalWidth;
                    Align align = para.style.align;
                    if (rtlPara && align == Align::Start) align = Align::End;
                    else if (rtlPara && align == Align::End) align = Align::Start;
                    if (align == Align::End) line.indent += slack;
                    else if (align == Align::Center) line.indent += slack * 0.5f;
                }

                Pt v = 0.0f;
                // 行頭・行末のルビの掛かり抑制用: Box に付いた注記グリフ（ルビ・圏点）の範囲
                std::vector<std::pair<size_t, size_t>> attached;
                // 双方向の並べ替え用: 行の中の箱とグルーの位置・幅・レベル・グリフの範囲
                struct Piece { Pt start; Pt width; int level; bool box; size_t glyphBegin; size_t glyphEnd; };
                std::vector<Piece> pieces;
                const bool needReorder = shaped.bidi || (shaped.paragraphLevel & 1);
                for (uint32_t i = br.itemStart; i < br.itemEnd; ++i) {
                    const LineItem& item = items[i];
                    if (item.isGlue()) {
                        const Pt gw = item.natural +
                                      (br.ratio >= 0.0f ? br.ratio * item.stretch : br.ratio * item.shrink);
                        if (needReorder) pieces.push_back(Piece{v, gw, item.level, false, 0, 0});
                        v += gw;
                        continue;
                    }
                    if (!item.isBox()) continue;
                    const size_t pieceGlyphBegin = line.glyphs.size();

                    const ShapedCluster& cluster = shaped.clusters[item.clusterIndex];
                    if (!item.ownGlyphs) {
                        const Pt delta = (v + item.glyphOffset) - cluster.origin;
                        for (uint32_t g = 0; g < cluster.glyphCount; ++g) {
                            PlacedGlyph glyph = shaped.glyphs[cluster.glyphStart + g];
                            glyph.inline_ += delta;
                            glyph.charIndex += static_cast<uint32_t>(pos);
                            line.glyphs.push_back(std::move(glyph));
                        }
                        if (!cluster.upright || cluster.object) {
                            line.blockMin = std::min(line.blockMin, shaped.blockMin);
                            line.blockMax = std::max(line.blockMax, shaped.blockMax);
                        }
                    }
                    const size_t attBegin = line.glyphs.size();
                    for (PlacedGlyph glyph : item.glyphs) {
                        glyph.inline_ += v;
                        glyph.charIndex += static_cast<uint32_t>(pos);
                        line.glyphs.push_back(std::move(glyph));
                    }
                    if (!item.ownGlyphs && !item.glyphs.empty()) attached.emplace_back(attBegin, line.glyphs.size());
                    line.blockMin = std::min(line.blockMin, item.extentMin);
                    line.blockMax = std::max(line.blockMax, item.extentMax);
                    line.charEnd = std::max(line.charEnd, pos + cluster.charEnd);
                    if (needReorder) {
                        pieces.push_back(Piece{v, item.width, cluster.level, true, pieceGlyphBegin, line.glyphs.size()});
                    }
                    v += item.width;
                }
                if (hyphenated) {
                    for (PlacedGlyph glyph : items[br.itemEnd].breakGlyphs) {
                        glyph.inline_ += v;
                        glyph.charIndex = static_cast<uint32_t>(line.charEnd);
                        line.glyphs.push_back(std::move(glyph));
                    }
                    line.hyphenated = true;
                    v += items[br.itemEnd].width;
                }
                if (needReorder && !pieces.empty()) reorderBidi(line, pieces, shaped.paragraphLevel);
                // 行頭・行末ではルビを行の外へ掛けない（JLReq 3.3.6）: 行からはみ出す注記はそのぶん内側へずらす
                // （行の途中の掛かりは隣の字の上なので触らない）
                for (const auto& [b, e] : attached) {
                    Pt mn = 0.0f, mx = v;
                    for (size_t g = b; g < e; ++g) {
                        mn = std::min(mn, line.glyphs[g].inline_);
                        mx = std::max(mx, line.glyphs[g].inline_ + line.glyphs[g].advance);
                    }
                    Pt shift = 0.0f;
                    if (mn < 0.0f) shift = -mn;
                    else if (mx > v) shift = v - mx;
                    if (shift != 0.0f) for (size_t g = b; g < e; ++g) line.glyphs[g].inline_ += shift;
                }
                computeExtraLeading(line, frag.linePitch, isVertical(wm));
                frag.lines.push_back(std::move(line));
                ++lineIndex;
            }
        }

        frag.charEnd = std::min(nextPos, text.size());
        if (nl == std::u16string::npos) break;
        pos = nextPos;
        paraHead = true;
        if (pos >= text.size()) break;   // 末尾の改行の後に空行は作らない
    }
    frag.charEnd = text.size();
    frag.complete = true;
    frag.rotation = para.style.rotation;
    return frag;
}

//------------------------------------------------------------------------------

Point lineOrigin(WritingMode wm, Point origin, int lineOffset, Pt linePitch, Pt indent) {
    return lineOriginAt(wm, origin, linePitch * static_cast<float>(lineOffset), indent);
}

Point lineOriginAt(WritingMode wm, Point origin, Pt adv, Pt indent) {
    switch (wm) {
    case WritingMode::HorizontalTb: return Point{origin.x + indent, origin.y + adv};
    case WritingMode::VerticalRl:   return Point{origin.x - adv, origin.y + indent};
    case WritingMode::VerticalLr:   return Point{origin.x + adv, origin.y + indent};
    }
    return origin;
}

namespace {

/// 行内の下線／打消し線を矩形で出す。同じスタイルの連続したグリフをひとつの線にする
void emitDecorations(dl::DisplayList& out, const ParagraphFragment& frag, const LineBox& line,
                     WritingMode wm, Point lo, bool underline, size_t maxChars) {
    const bool vertical = isVertical(wm);
    auto skip = [&](const PlacedGlyph& g) {
        return g.object || g.image || g.placeholder || g.charIndex >= maxChars;
    };
    size_t i = 0;
    while (i < line.glyphs.size()) {
        const PlacedGlyph& g0 = line.glyphs[i];
        if (skip(g0)) { ++i; continue; }
        const size_t si = g0.styleIndex < frag.styles.size() ? g0.styleIndex : 0;
        const TextStyle& style = frag.styles[si];
        const std::optional<TextDecoration>& deco = underline ? style.underline : style.strikethrough;
        if (!deco) { ++i; continue; }
        size_t j = i + 1;
        while (j < line.glyphs.size() && !skip(line.glyphs[j]) &&
               line.glyphs[j].styleIndex == g0.styleIndex) ++j;
        const PlacedGlyph& g1 = line.glyphs[j - 1];

        const StyleMetrics sm = si < frag.styleMetrics.size() ? frag.styleMetrics[si] : StyleMetrics{};
        const font::DecorationMetrics& dm = sm.decoration;
        Pt thickness = deco->thickness > 0.0f ? deco->thickness
                                              : (underline ? dm.underlineThickness : dm.strikeoutThickness);
        if (thickness <= 0.0f) thickness = style.size * 0.05f;
        const Pt shift = style.baselineShift * style.size;
        Pt center;
        if (vertical) {
            // 傍線は列の右側（文字の外）。打消し線は列の中心線
            center = shift + (underline ? style.size * 0.5f + dm.underlineOffset : 0.0f) +
                     deco->offset * style.size;
        } else {
            center = sm.baseline - shift + (underline ? dm.underlineOffset : dm.strikeoutOffset) +
                     deco->offset * style.size;
        }
        const Pt from = g0.inline_ - g0.boxBefore;
        const Pt to = g1.inline_ + g1.boxAfter;
        const Point p0 = toPhysical(wm, LogicalPoint{from, center - thickness * 0.5f}, lo);
        const Point p1 = toPhysical(wm, LogicalPoint{to, center + thickness * 0.5f}, lo);
        dl::RectItem rect;
        rect.rect = Rect{std::min(p0.x, p1.x), std::min(p0.y, p1.y),
                         std::fabs(p1.x - p0.x), std::fabs(p1.y - p0.y)};
        rect.fill = deco->color ? *deco->color : style.fill;
        out.add(rect);
        i = j;
    }
}

} // namespace

void emitParagraph(dl::DisplayList& out, const ParagraphFragment& frag, WritingMode wm,
                   Point origin, int lineOffset, size_t maxChars) {
    // 段落の回転: origin を中心に回す Group で包み、中身は回さずに組む
    if (frag.rotation != 0.0f) {
        dl::DisplayList inner;
        ParagraphFragment flat = frag;
        flat.rotation = 0.0f;
        emitParagraph(inner, flat, wm, Point{0.0f, 0.0f}, lineOffset, maxChars);
        const float rad = frag.rotation * 3.14159265358979f / 180.0f;
        dl::Group grp;
        grp.xform = multiply(Matrix::translation(origin.x, origin.y), Matrix::rotation(rad));
        grp.children = std::move(inner.items);
        out.add(std::move(grp));
        return;
    }
    // スタイルごとの層（下から上）
    std::vector<std::vector<TextLayer>> layers;
    layers.reserve(frag.styles.size());
    size_t maxLayers = 1;
    for (const TextStyle& st : frag.styles) {
        layers.push_back(st.resolvedLayers());
        maxLayers = std::max(maxLayers, layers.back().size());
    }
    auto layersOf = [&](const PlacedGlyph& g) -> const std::vector<TextLayer>& {
        return layers[g.styleIndex < layers.size() ? g.styleIndex : 0];
    };

    for (size_t li = 0; li < frag.lines.size(); ++li) {
        const LineBox& line = frag.lines[li];
        const Point lo = lineOriginAt(wm, origin,
                                      frag.linePitch * static_cast<float>(lineOffset) + frag.lineCenterOffset(li),
                                      line.indent);

        emitDecorations(out, frag, line, wm, lo, true, maxChars);

        // 層は上から数えて揃える（k = 0 が最上層）。影や外側の縁取りが隣の文字の塗りに載らないよう、
        // 下の層を行全体で先に出す。画像・オブジェクト・カラーグリフは最上層のときに 1 回だけ出す
        for (size_t k = maxLayers; k-- > 0;) {
            dl::GlyphRun run;
            bool open = false;
            const TextLayer* runLayer = nullptr;
            auto flush = [&]() {
                if (open && !run.glyphs.empty()) out.add(run);
                run = dl::GlyphRun{};
                open = false;
                runLayer = nullptr;
            };

            for (const PlacedGlyph& g : line.glyphs) {
                if (g.charIndex >= maxChars) continue;
                if (g.placeholder) { flush(); continue; }
                if (g.object) {
                    if (k != 0) continue;
                    // 行内オブジェクト: 論理の箱 [inline_, inline_+w] × [block, block+h] に置く。縦組みは横倒し
                    flush();
                    const Size sz = g.object->size;
                    const Point p0 = toPhysical(wm, LogicalPoint{g.inline_, g.block}, lo);
                    const Point p1 = toPhysical(wm, LogicalPoint{g.inline_ + sz.w, g.block + sz.h}, lo);
                    const Rect box{std::min(p0.x, p1.x), std::min(p0.y, p1.y),
                                   std::fabs(p1.x - p0.x), std::fabs(p1.y - p0.y)};
                    out.add(objectGroup(*g.object, box, isVertical(wm)));
                    continue;
                }
                if (g.image) {
                    if (k != 0) continue;
                    // 行内画像: 中心を (inline_ + adv/2, block) に置く
                    flush();
                    const bool vertical = isVertical(wm);
                    const Pt adv = vertical ? g.imageSize.h : g.imageSize.w;
                    const Point c = toPhysical(wm, LogicalPoint{g.inline_ + adv * 0.5f, g.block}, lo);
                    dl::ImageItem item;
                    item.image = g.image;
                    item.xform = multiply(Matrix::translation(c.x - g.imageSize.w * 0.5f, c.y - g.imageSize.h * 0.5f),
                                          Matrix::scaling(g.imageSize.w / static_cast<float>(std::max(1, g.image->width)),
                                                          g.imageSize.h / static_cast<float>(std::max(1, g.image->height))));
                    out.add(item);
                    continue;
                }
                // カラーグリフ（絵文字）はレイヤ／ビットマップとして置く（3 backend で同じ色になる）
                if (g.face && g.face->descriptor().color && !g.monochrome) {
                    if (k != 0) continue;
                    const Point pp = toPhysical(wm, LogicalPoint{g.inline_, g.block}, lo);
                    flush();
                    if (emitColorGlyph(out, *g.face, g.gid, g.size, pp, g.xform)) continue;
                }
                const std::vector<TextLayer>& ls = layersOf(g);
                if (k >= ls.size()) continue;
                const TextLayer& layer = ls[ls.size() - 1 - k];
                if (!layer.fill && !layer.stroke) continue;
                const bool same = open && run.face == g.face && run.size == g.size &&
                                  run.embolden == g.embolden && runLayer &&
                                  runLayer->fill == layer.fill && runLayer->stroke == layer.stroke &&
                                  runLayer->offset.x == layer.offset.x && runLayer->offset.y == layer.offset.y &&
                                  runLayer->blur == layer.blur;
                if (!same) {
                    flush();
                    run.face = g.face;
                    run.size = g.size;
                    run.fill = layer.fill;
                    run.stroke = layer.stroke;
                    run.blur = layer.blur;
                    run.embolden = g.embolden;
                    run.text = frag.text;
                    runLayer = &layer;
                    open = true;
                }
                dl::Glyph dg;
                dg.gid = g.gid;
                dg.pos = toPhysical(wm, LogicalPoint{g.inline_, g.block}, lo);
                dg.pos.x += layer.offset.x;
                dg.pos.y += layer.offset.y;
                dg.xform = g.xform;
                dg.charIndex = g.charIndex;
                run.glyphs.push_back(dg);
            }
            flush();
        }

        emitDecorations(out, frag, line, wm, lo, false, maxChars);
    }
}

} // namespace typeset::inl
