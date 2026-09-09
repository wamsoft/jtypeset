/**
 * query.cpp — 組んだあとの取り出し口
 *
 * 文字の箱（charBoxes）、文字範囲 → 矩形（rectsFor）、プレースホルダの位置、点 → 文字（hitTest）、
 * キャレット（caretRect）、1 行計測（measureText）。ホストがリンク・選択・キャレット・段階表示・
 * ウィジェットの重ね合わせを作るための問い合わせで、組版そのものには関わらない
 */

#include "typeset/inl/paragraph.hpp"

#include <algorithm>
#include <cmath>

#include "typeset/inl/item_builder.hpp"
#include "typeset/inl/shaper.hpp"

namespace typeset::inl {

namespace {

Rect rectFromLogical(WritingMode wm, Point lo, Pt i0, Pt i1, Pt b0, Pt b1) {
    const Point p0 = toPhysical(wm, LogicalPoint{i0, b0}, lo);
    const Point p1 = toPhysical(wm, LogicalPoint{i1, b1}, lo);
    return Rect{std::min(p0.x, p1.x), std::min(p0.y, p1.y), std::fabs(p1.x - p0.x), std::fabs(p1.y - p0.y)};
}

CharBox boxOf(const ParagraphFragment& frag, size_t li, const PlacedGlyph& g, bool vertical) {
    CharBox b;
    b.lineIndex = li;
    b.charIndex = g.charIndex;
    b.inlineStart = g.inline_ - g.boxBefore;
    b.inlineEnd = g.inline_ + g.boxAfter;
    b.styleIndex = g.styleIndex;
    b.gid = g.gid;
    b.size = g.size;
    b.face = g.face;
    if (g.object) {
        b.object = true;
        b.blockMin = g.block;
        b.blockMax = g.block + g.object->size.h;
    } else if (g.image || g.placeholder) {
        b.image = static_cast<bool>(g.image);
        b.placeholder = g.placeholder;
        const Pt half = vertical ? g.imageSize.w * 0.5f : g.imageSize.h * 0.5f;
        b.blockMin = g.block - half;
        b.blockMax = g.block + half;
    } else {
        // em box。中心線から baselineShift ぶんずれる（注記側が正）
        const TextStyle& st = frag.styles.empty() ? TextStyle{}
                              : frag.styles[std::min<size_t>(g.styleIndex, frag.styles.size() - 1)];
        const Pt shift = st.baselineShift * st.size;
        const Pt center = vertical ? shift : -shift;
        b.blockMin = center - g.size * 0.5f;
        b.blockMax = center + g.size * 0.5f;
    }
    return b;
}

/// 行 li の文字の箱と、その行の行頭
std::vector<CharBox> boxesOf(const ParagraphFragment& frag, WritingMode wm, size_t li) {
    std::vector<CharBox> out;
    if (li >= frag.lines.size()) return out;
    const bool vertical = isVertical(wm);
    for (const PlacedGlyph& g : frag.lines[li].glyphs) {
        if (g.annotation) continue;
        out.push_back(boxOf(frag, li, g, vertical));
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const CharBox& a, const CharBox& b) { return a.inlineStart < b.inlineStart; });
    return out;
}

} // namespace

Rect CharBox::rect(WritingMode wm, Point lineOrigin) const {
    return rectFromLogical(wm, lineOrigin, inlineStart, inlineEnd, blockMin, blockMax);
}

std::vector<CharBox> charBoxes(const ParagraphFragment& frag, WritingMode wm, size_t lineIndex) {
    return boxesOf(frag, wm, lineIndex);
}

Point lineOriginOf(const ParagraphFragment& frag, WritingMode wm, Point origin, size_t lineIndex,
                   int lineOffset) {
    const Pt indent = lineIndex < frag.lines.size() ? frag.lines[lineIndex].indent : 0.0f;
    return lineOriginAt(wm, origin, frag.linePitch * static_cast<float>(lineOffset) + frag.lineCenterOffset(lineIndex),
                        indent);
}

std::vector<Rect> rectsFor(const ParagraphFragment& frag, WritingMode wm, Point origin,
                           size_t charStart, size_t charEnd, int lineOffset) {
    std::vector<Rect> out;
    if (charEnd <= charStart) return out;
    for (size_t li = 0; li < frag.lines.size(); ++li) {
        const LineBox& line = frag.lines[li];
        if (line.charEnd <= charStart || line.charStart >= charEnd) continue;
        Pt i0 = 0.0f, i1 = 0.0f, b0 = 0.0f, b1 = 0.0f;
        bool any = false;
        for (const CharBox& b : boxesOf(frag, wm, li)) {
            if (b.charIndex < charStart || b.charIndex >= charEnd) continue;
            if (!any) {
                i0 = b.inlineStart; i1 = b.inlineEnd; b0 = b.blockMin; b1 = b.blockMax;
                any = true;
            } else {
                i0 = std::min(i0, b.inlineStart);
                i1 = std::max(i1, b.inlineEnd);
                b0 = std::min(b0, b.blockMin);
                b1 = std::max(b1, b.blockMax);
            }
        }
        if (!any) continue;
        out.push_back(rectFromLogical(wm, lineOriginOf(frag, wm, origin, li, lineOffset), i0, i1, b0, b1));
    }
    return out;
}

std::vector<PlaceholderRect> placeholderRects(const ParagraphFragment& frag, WritingMode wm, Point origin,
                                              int lineOffset) {
    std::vector<PlaceholderRect> out;
    for (size_t li = 0; li < frag.lines.size(); ++li) {
        const Point lo = lineOriginOf(frag, wm, origin, li, lineOffset);
        for (const CharBox& b : boxesOf(frag, wm, li)) {
            if (!b.placeholder) continue;
            PlaceholderRect pr;
            pr.id = b.styleIndex < frag.placeholderIds.size() ? frag.placeholderIds[b.styleIndex] : std::string();
            pr.charIndex = b.charIndex;
            pr.rect = b.rect(wm, lo);
            out.push_back(std::move(pr));
        }
    }
    return out;
}

std::optional<HitResult> hitTest(const ParagraphFragment& frag, WritingMode wm, Point origin, Point p,
                                 int lineOffset) {
    for (size_t li = 0; li < frag.lines.size(); ++li) {
        const LineBox& line = frag.lines[li];
        const Point lo = lineOriginOf(frag, wm, origin, li, lineOffset);
        const LogicalPoint q = toLogical(wm, p, lo);
        // 行送りの箱: 中心線の前後 pitch/2 ＋ 行内オブジェクトのための追加。縦組みの block は右が正だが箱は対称
        const Pt half = frag.linePitch * 0.5f;
        Pt lo_b = -half, hi_b = half;
        if (isVertical(wm)) {
            // 行送りは rl なら左（block 負）、lr なら右へ進む。extraBefore は送りの手前側
            if (wm == WritingMode::VerticalRl) { hi_b += line.extraBefore; lo_b -= line.extraAfter; }
            else                               { lo_b -= line.extraBefore; hi_b += line.extraAfter; }
        } else {
            lo_b -= line.extraBefore;
            hi_b += line.extraAfter;
        }
        if (q.block < lo_b || q.block >= hi_b) continue;

        HitResult r;
        r.lineIndex = li;
        const std::vector<CharBox> boxes = boxesOf(frag, wm, li);
        if (boxes.empty()) {
            r.charIndex = static_cast<uint32_t>(line.charStart);
            return r;
        }
        if (q.inline_ < boxes.front().inlineStart) {
            r.charIndex = boxes.front().charIndex;
            return r;
        }
        for (size_t i = 0; i < boxes.size(); ++i) {
            const CharBox& b = boxes[i];
            if (q.inline_ >= b.inlineStart && q.inline_ < b.inlineEnd) {
                r.charIndex = b.charIndex;
                r.inside = true;
                r.after = q.inline_ > (b.inlineStart + b.inlineEnd) * 0.5f;
                return r;
            }
            if (i + 1 < boxes.size() && q.inline_ >= b.inlineEnd && q.inline_ < boxes[i + 1].inlineStart) {
                // 箱の間（グルー）: 近い方
                const bool nearPrev = (q.inline_ - b.inlineEnd) <= (boxes[i + 1].inlineStart - q.inline_);
                r.charIndex = nearPrev ? b.charIndex : boxes[i + 1].charIndex;
                r.after = nearPrev;
                return r;
            }
        }
        r.charIndex = static_cast<uint32_t>(line.charEnd);
        r.after = true;
        return r;
    }
    return std::nullopt;
}

std::optional<Rect> caretRect(const ParagraphFragment& frag, WritingMode wm, Point origin, size_t charIndex,
                              int lineOffset, Pt thickness) {
    for (size_t li = 0; li < frag.lines.size(); ++li) {
        const LineBox& line = frag.lines[li];
        if (charIndex < line.charStart || charIndex > line.charEnd) continue;
        // 行末の位置（次の行の行頭でもある）は前の行の行末側に置く
        const Point lo = lineOriginOf(frag, wm, origin, li, lineOffset);
        const std::vector<CharBox> boxes = boxesOf(frag, wm, li);
        Pt pos = 0.0f, b0 = 0.0f, b1 = 0.0f;
        if (boxes.empty()) {
            const Pt em = frag.styles.empty() ? frag.baseSize : frag.styles.front().size;
            b0 = -em * 0.5f;
            b1 = em * 0.5f;
        } else {
            const CharBox* target = nullptr;
            for (const CharBox& b : boxes) {
                if (b.charIndex >= charIndex && (!target || b.charIndex < target->charIndex ||
                                                  (b.charIndex == target->charIndex && b.inlineStart < target->inlineStart))) {
                    target = &b;
                }
            }
            if (target) {
                pos = target->inlineStart;
                b0 = target->blockMin;
                b1 = target->blockMax;
            } else {
                const CharBox& last = boxes.back();
                pos = last.inlineEnd;
                b0 = last.blockMin;
                b1 = last.blockMax;
            }
        }
        return rectFromLogical(wm, lo, pos - thickness * 0.5f, pos + thickness * 0.5f, b0, b1);
    }
    return std::nullopt;
}

TextMetrics measureText(font::FontSet& fonts, const std::u16string& text, const TextStyle& style,
                        WritingMode wm, TextOrientation orientation) {
    const ShapedText shaped = shapeText(text, style, fonts, wm, orientation);
    TextMetrics m;
    m.advance = shaped.advance;
    m.clusterCount = shaped.clusters.size();
    m.glyphCount = shaped.glyphs.size();
    // 注記側（横組み: 上、縦組み: 右）を ascent とする
    if (annotationSide(wm) < 0.0f) {
        m.ascent = std::max(0.0f, -shaped.blockMin);
        m.descent = std::max(0.0f, shaped.blockMax);
    } else {
        m.ascent = std::max(0.0f, shaped.blockMax);
        m.descent = std::max(0.0f, -shaped.blockMin);
    }
    return m;
}

} // namespace typeset::inl
