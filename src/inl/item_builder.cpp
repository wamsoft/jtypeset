/**
 * item_builder.cpp — シェイピング結果 → Box / Glue / Penalty 列
 *
 * インライン注記（ルビ・縦中横・圏点・割注・字取り）もここで解決する。
 * richtext LineItemBuilder の移植。座標を論理座標（inline / block）にし、
 * 注記の付く側を書字方向で切り替え、欧文単語内の分割可否を UAX #14 に任せる。
 */

#include "typeset/inl/item_builder.hpp"

#include <algorithm>
#include <cmath>

#include "typeset/inl/line_breaker.hpp"
#include "typeset/inl/shaper.hpp"
#include "typeset/text/hyphenation.hpp"
#include "typeset/text/line_break.hpp"
#include "typeset/text/utf.hpp"

namespace typeset::inl {

using text::CharClass;

namespace {

//------------------------------------------------------------------------------
// 禁則とボディ
//------------------------------------------------------------------------------

/**
 * 2 つのクラスタの間で改行できるか
 * @param uaxAllowed UAX #14 が「前のクラスタの直後で切ってよい」と言っているか
 */
bool canBreakBetween(CharClass before, CharClass after, bool uaxAllowed) {
    if (text::isLineEndProhibited(before)) return false;
    if (text::isLineStartProhibited(after)) return false;
    if (before == CharClass::Inseparable && after == CharClass::Inseparable) return false;
    // 欧文同士は UAX #14 に従う（単語内は切らない、ハイフンの後ろ等は切れる）
    if (text::isWestern(before) && text::isWestern(after)) return uaxAllowed;
    if (before == CharClass::PrefixAbbr && text::isWestern(after)) return false;
    if (text::isWestern(before) && after == CharClass::PostfixAbbr) return false;
    return true;
}

/// 禁則が「弱い」（有限ペナルティにする）クラスか
bool isWeakLineStart(CharClass c, KinsokuLevel level) {
    if (level == KinsokuLevel::Strict) return false;
    switch (c) {
    case CharClass::SmallKana:
    case CharClass::Prolonged:
    case CharClass::Iteration:
    case CharClass::Hyphen:
        return true;
    case CharClass::FullStop:
    case CharClass::Comma:
    case CharClass::CloseBracket:
    case CharClass::MiddleDot:
    case CharClass::Dividing:
        return level == KinsokuLevel::Loose;
    default:
        return false;
    }
}

/// 和欧間・約物を除いた「文字」の和字か（Word 折返しで切らない対象）
bool isJapaneseLetter(CharClass c) {
    switch (c) {
    case CharClass::Ideographic:
    case CharClass::Hiragana:
    case CharClass::Katakana:
    case CharClass::SmallKana:
    case CharClass::Prolonged:
    case CharClass::Iteration:
    case CharClass::Inseparable:
    case CharClass::Unknown:
        return true;
    default:
        return false;
    }
}

/**
 * 2 クラスタの間で切るときのペナルティ。0 = 切ってよい、kInfinitePenalty = 切らない、その間 = 弱い禁則
 * @param cpBefore,cpAfter 前後の文字（禁則の追加・除外の照合用）
 */
float breakPenaltyBetween(CharClass before, CharClass after, bool uaxAllowed, char32_t cpBefore, char32_t cpAfter,
                          const SpacingOptions& opts, WrapMode wrap) {
    if (wrap == WrapMode::None) return kInfinitePenalty;
    auto listed = [](const std::u16string& list, char32_t cp) {
        std::u16string one;
        text::appendCodePoint(one, cp);
        return !list.empty() && list.find(one) != std::u16string::npos;
    };
    // 追加・除外リスト（文字クラスより優先）
    if (listed(opts.lineEndProhibited, cpBefore)) return kInfinitePenalty;
    if (listed(opts.lineStartProhibited, cpAfter)) return kInfinitePenalty;
    const bool endAllowed = listed(opts.lineEndAllowed, cpBefore);
    const bool startAllowed = listed(opts.lineStartAllowed, cpAfter);

    if (!endAllowed && text::isLineEndProhibited(before)) return kInfinitePenalty;
    if (!startAllowed && text::isLineStartProhibited(after)) {
        return isWeakLineStart(after, opts.kinsoku) ? opts.weakKinsokuPenalty : kInfinitePenalty;
    }
    if (before == CharClass::Inseparable && after == CharClass::Inseparable) return kInfinitePenalty;
    if (text::isWestern(before) && text::isWestern(after)) {
        if (wrap == WrapMode::Char) return 0.0f;
        return uaxAllowed ? 0.0f : kInfinitePenalty;
    }
    if (before == CharClass::PrefixAbbr && text::isWestern(after)) return kInfinitePenalty;
    if (text::isWestern(before) && after == CharClass::PostfixAbbr) return kInfinitePenalty;
    if (wrap == WrapMode::Word && isJapaneseLetter(before) && isJapaneseLetter(after)) return kInfinitePenalty;
    return 0.0f;
}

/// 詰めた仮想ボディの中でのグリフ位置
Pt bodyGlyphOffset(CharClass cls, Pt shapedAdvance, Pt bodyWidth) {
    const Pt slack = shapedAdvance - bodyWidth;
    if (slack <= 0.0f) return 0.0f;
    switch (text::getBodyAlign(cls)) {
    case text::BodyAlign::End:    return -slack;
    case text::BodyAlign::Center: return -slack * 0.5f;
    default:                      return 0.0f;
    }
}

/// ルビがこの文字に掛かってよいか（JLReq: 掛けられるのは仮名・漢字）
bool canRubyOverhang(CharClass cls) {
    if (!text::isJapanese(cls)) return false;
    if (text::isHalfWidthPunctuation(cls)) return false;
    switch (cls) {
    case CharClass::Dividing:
    case CharClass::IdeographicSpace:
    case CharClass::Inseparable:
        return false;
    default:
        return true;
    }
}

TextStyle derivedStyle(const ItemBuildContext& ctx, Pt size) {
    TextStyle s = ctx.baseStyle ? *ctx.baseStyle : TextStyle{};
    s.size = size;
    s.letterSpacing = 0.0f;
    return s;
}

//------------------------------------------------------------------------------
// ルビ
//------------------------------------------------------------------------------

struct RubyResult {
    std::vector<PlacedGlyph> glyphs;   ///< inline_ は親範囲の先頭からの相対
    Pt endGap = 0.0f;                  ///< 親範囲の前後に入れる固定アキ
    Pt innerGap = 0.0f;                ///< 親 Box の間に入れる固定アキ
    Pt extentMin = 0.0f, extentMax = 0.0f;
    bool valid = false;
};

RubyResult layoutRuby(const std::u16string& rubyText, const ItemBuildContext& ctx,
                      Pt em, Pt rubySize, Pt parentWidth, int parentCount,
                      bool canHangBefore, bool canHangAfter, uint32_t styleIndex) {
    RubyResult out;
    if (rubyText.empty() || parentCount <= 0) return out;

    const TextStyle rubyStyle = derivedStyle(ctx, rubySize);
    const ShapedText shaped = shapeText(rubyText, rubyStyle, ctx.fonts, ctx.writingMode,
                                        TextOrientation::Mixed);
    if (shaped.clusters.empty()) return out;

    const Pt rubyLength = shaped.advance;
    const int n = static_cast<int>(shaped.clusters.size());

    // ルビの位置: 親のボディに接して外側へ（縦組みは右、横組みは上）
    const float side = annotationSide(ctx.writingMode);
    const Pt shift = side * (em * 0.5f + rubySize * 0.5f);
    if (side > 0.0f) out.extentMax = em * 0.5f + rubySize;
    else             out.extentMin = -(em * 0.5f + rubySize);

    Pt startV;
    std::vector<Pt> clusterV(n);
    if (rubyLength <= parentWidth) {
        // 中付き: ルビ文字の間と前後（前後は半分）へ均等に配分する
        const Pt extra = parentWidth - rubyLength;
        const Pt gap = extra / static_cast<float>(n);
        startV = gap * 0.5f;
        for (int i = 0; i < n; ++i) {
            clusterV[i] = startV + shaped.clusters[i].origin + gap * static_cast<float>(i);
        }
    } else {
        // ルビのほうが長い: まず前後の文字へ掛け、それでも足りない分だけ親を広げる
        const Pt overflow = rubyLength - parentWidth;
        const Pt hangBefore = canHangBefore ? std::min(overflow * 0.5f, rubySize) : 0.0f;
        const Pt hangAfter = canHangAfter ? std::min(overflow * 0.5f, rubySize) : 0.0f;
        const Pt rest = std::max(0.0f, overflow - hangBefore - hangAfter);
        if (rest > 0.0f) {
            // 親文字列の前後 1 : 文字間 2 の比で配る
            const Pt unit = rest / (2.0f * static_cast<float>(parentCount));
            out.endGap = unit;
            out.innerGap = unit * 2.0f;
        }
        startV = -hangBefore;
        for (int i = 0; i < n; ++i) clusterV[i] = startV + shaped.clusters[i].origin;
    }

    out.glyphs.reserve(shaped.glyphs.size());
    for (int i = 0; i < n; ++i) {
        const ShapedCluster& c = shaped.clusters[i];
        const Pt delta = clusterV[i] - c.origin;
        for (uint32_t g = 0; g < c.glyphCount; ++g) {
            PlacedGlyph glyph = shaped.glyphs[c.glyphStart + g];
            glyph.block += shift;
            glyph.inline_ += delta;
            glyph.styleIndex = styleIndex;
            glyph.annotation = true;
            out.glyphs.push_back(std::move(glyph));
        }
    }
    out.valid = true;
    return out;
}

/**
 * 熟語ルビ — 親文字 1 字ずつのルビを、はみ出す分だけ隣の親文字へ掛けて並べる
 *
 * 前提: 各部分の長さの合計が親文字列の長さ以下（超える場合は呼び出し側がグループルビへ落とす）。
 * 各部分を親の中央に置きたい位置から始め、前の部分との重なりを解消し、末端を超えた分を戻す。
 * @return 親範囲の先頭からの相対位置で置いたグリフ列
 */
RubyResult layoutJukugoRuby(const std::vector<ShapedText>& parts, const std::vector<Pt>& parentWidths,
                            Pt em, Pt rubySize, uint32_t styleIndex, WritingMode wm) {
    RubyResult out;
    const size_t n = parts.size();
    if (n == 0 || n != parentWidths.size()) return out;

    std::vector<Pt> pStart(n + 1, 0.0f);
    for (size_t i = 0; i < n; ++i) pStart[i + 1] = pStart[i] + parentWidths[i];
    const Pt total = pStart[n];

    std::vector<Pt> x(n);
    for (size_t i = 0; i < n; ++i) x[i] = pStart[i] + (parentWidths[i] - parts[i].advance) * 0.5f;
    // 前から: 重なりを解消
    x[0] = std::max(0.0f, x[0]);
    for (size_t i = 1; i < n; ++i) x[i] = std::max(x[i], x[i - 1] + parts[i - 1].advance);
    // 後ろから: 末端を超えた分を戻す
    x[n - 1] = std::min(x[n - 1], total - parts[n - 1].advance);
    for (size_t i = n - 1; i > 0; --i) x[i - 1] = std::min(x[i - 1], x[i] - parts[i - 1].advance);
    x[0] = std::max(0.0f, x[0]);

    const float side = annotationSide(wm);
    const Pt shift = side * (em * 0.5f + rubySize * 0.5f);
    if (side > 0.0f) out.extentMax = em * 0.5f + rubySize;
    else             out.extentMin = -(em * 0.5f + rubySize);

    for (size_t i = 0; i < n; ++i) {
        for (const PlacedGlyph& src : parts[i].glyphs) {
            PlacedGlyph g = src;
            g.block += shift;
            g.inline_ += x[i];
            g.styleIndex = styleIndex;
            g.annotation = true;
            out.glyphs.push_back(std::move(g));
        }
    }
    out.valid = true;
    return out;
}

//------------------------------------------------------------------------------
// 縦中横・割注（Box 本体を差し替えるもの）
//------------------------------------------------------------------------------

struct CompositeResult {
    std::vector<PlacedGlyph> glyphs;
    Pt width = 0.0f;
    bool valid = false;
};

/**
 * 縦中横 — 半角数字等を 1em 角に正立で収める
 *
 * 中身を横組みで組み、1em に入らない分だけ圧縮して 1em 角の Box にする。
 */
CompositeResult layoutTateChuYoko(const std::u16string& sub, const ItemBuildContext& ctx,
                                  Pt em, uint32_t styleIndex) {
    CompositeResult out;
    if (sub.empty() || !isVertical(ctx.writingMode)) return out;

    const TextStyle style = derivedStyle(ctx, em);
    const ShapedText shaped = shapeText(sub, style, ctx.fonts, WritingMode::HorizontalTb);
    if (shaped.glyphs.empty()) return out;

    const Pt runWidth = shaped.advance;
    const float scale = (runWidth > em && runWidth > 0.0f) ? (em / runWidth) : 1.0f;

    out.glyphs.reserve(shaped.glyphs.size());
    for (const PlacedGlyph& src : shaped.glyphs) {
        PlacedGlyph g = src;
        // 横組みの (inline, block) → 縦組みの (block, inline)。em box の中心を Box の中央へ
        g.block = (src.inline_ - runWidth * 0.5f) * scale;
        g.inline_ = em * 0.5f + src.block * scale;
        g.advance = src.advance * scale;
        g.boxBefore = g.inline_;        // 縦中横の箱は 1em
        g.boxAfter = em - g.inline_;
        Mat2 s;
        s.xx = scale;
        g.xform = multiply(s, src.xform);
        g.styleIndex = styleIndex;
        out.glyphs.push_back(std::move(g));
    }
    out.width = em;
    out.valid = true;
    return out;
}

/**
 * 割注 — 行内に 2 行の子ブロックを組む
 *
 * 子は本文の半分の文字サイズで組み、2 段に割る（縦組みは右→左、横組みは上→下）。
 * 段の長さは「全体の半分」から始めて、2 行に収まるまで少しずつ伸ばす。
 */
CompositeResult layoutWarichu(const std::u16string& content, const ItemBuildContext& ctx,
                              Pt em, float scale, const SpacingOptions& opts,
                              uint32_t styleIndex) {
    CompositeResult out;
    if (content.empty()) return out;

    const TextStyle childStyle = derivedStyle(ctx, em * scale);
    const std::vector<TextStyle> childStyles{childStyle};
    const ShapedText shaped = shapeText(content, childStyle, ctx.fonts, ctx.writingMode,
                                        TextOrientation::Mixed);
    if (shaped.clusters.empty()) return out;

    ItemBuildContext childCtx = ctx;
    childCtx.styles = &childStyles;
    childCtx.baseStyle = &childStyle;
    childCtx.letterSpacing = 0.0f;
    SpacingOptions childOpts = opts;
    childOpts.hangingPunctuation = false;
    const std::vector<LineItem> items = buildLineItems(shaped, {}, childOpts, childCtx);

    Pt total = 0.0f;
    for (const LineItem& it : items) {
        if (it.isBox()) total += it.width;
        else if (it.isGlue()) total += it.natural;
    }

    BreakOptions breakOpts;
    breakOpts.strategy = LineBreakStrategy::Greedy;
    breakOpts.justify = false;

    std::vector<BreakLine> breaks;
    Pt target = std::max(childStyle.size, total * 0.5f);
    for (int attempt = 0; attempt < 32; ++attempt) {
        breaks = breakLines(items, ConstantLineShape(target), breakOpts);
        if (breaks.size() <= 2) break;
        target *= 1.08f;
    }
    if (breaks.empty()) return out;

    const float side = annotationSide(ctx.writingMode);
    const Pt quarter = childStyle.size * 0.5f;

    for (size_t li = 0; li < breaks.size() && li < 2; ++li) {
        const BreakLine& br = breaks[li];
        const Pt shift = (li == 0) ? side * quarter : -side * quarter;

        Pt v = 0.0f;
        for (uint32_t i = br.itemStart; i < br.itemEnd; ++i) {
            const LineItem& item = items[i];
            if (item.isGlue()) { v += item.natural; continue; }
            if (!item.isBox()) continue;

            if (item.ownGlyphs) {
                for (PlacedGlyph glyph : item.glyphs) {
                    glyph.block += shift;
                    glyph.inline_ += v;
                    glyph.styleIndex = styleIndex;
                    glyph.annotation = true;
                    out.glyphs.push_back(std::move(glyph));
                }
            } else {
                const ShapedCluster& c = shaped.clusters[item.clusterIndex];
                const Pt delta = (v + item.glyphOffset) - c.origin;
                for (uint32_t g = 0; g < c.glyphCount; ++g) {
                    PlacedGlyph glyph = shaped.glyphs[c.glyphStart + g];
                    glyph.block += shift;
                    glyph.inline_ += delta;
                    glyph.styleIndex = styleIndex;
                    glyph.annotation = true;
                    out.glyphs.push_back(std::move(glyph));
                }
            }
            v += item.width;
        }
        out.width = std::max(out.width, br.naturalWidth);
    }
    out.valid = !out.glyphs.empty();
    return out;
}

//------------------------------------------------------------------------------
// 圏点
//------------------------------------------------------------------------------

bool layoutEmphasisMark(EmphasisMark mark, const ItemBuildContext& ctx, Pt em, Pt markSize,
                        Pt parentWidth, uint32_t styleIndex, std::vector<PlacedGlyph>& out,
                        float side) {
    std::u16string t;
    text::appendCodePoint(t, emphasisMarkCodePoint(mark));
    const TextStyle markStyle = derivedStyle(ctx, markSize);
    const ShapedText shaped = shapeText(t, markStyle, ctx.fonts, ctx.writingMode,
                                        TextOrientation::Upright);
    if (shaped.glyphs.empty()) return false;

    // 圏点は字面（インク）の中心を親文字の中央に合わせる。ゴマ点 U+FE45 は縦組み用の
    // 字形で、横組みでは em box の中で字面が偏っているため、送りではなく字面で揃える
    const Pt targetBlock = side * (em * 0.5f + markSize * 0.5f);
    const Pt targetInline = parentWidth * 0.5f;
    const bool vertical = isVertical(ctx.writingMode);
    for (PlacedGlyph glyph : shaped.glyphs) {
        Pt cx = 0.0f, cy = 0.0f;   // ペン原点から字面中心へ（物理、y-down）
        glyphware::GlyphMetrics m;
        if (glyph.face && glyph.face->glyphMetricsUnscaled(glyph.gid, m)) {
            const float s = markSize / font::unitsPerEm(*glyph.face);
            cx = (m.bearingX + m.width * 0.5f) * s;
            cy = -(m.bearingY - m.height * 0.5f) * s;
        }
        const Pt inkInline = vertical ? glyph.inline_ + cy : glyph.inline_ + cx;
        const Pt inkBlock = vertical ? glyph.block + cx : glyph.block + cy;
        glyph.inline_ += targetInline - inkInline;
        glyph.block += targetBlock - inkBlock;
        glyph.styleIndex = styleIndex;
        glyph.annotation = true;
        out.push_back(std::move(glyph));
    }
    return true;
}

//------------------------------------------------------------------------------
// 注記の解決
//------------------------------------------------------------------------------

struct ResolvedAnnotation {
    const Annotation* ann = nullptr;
    uint32_t clusterStart = 0;
    uint32_t clusterEnd = 0;   ///< exclusive
};

std::vector<ResolvedAnnotation> resolveAnnotations(const ShapedText& shaped,
                                                   const std::vector<Annotation>& annotations) {
    std::vector<ResolvedAnnotation> out;
    for (const Annotation& ann : annotations) {
        if (ann.end <= ann.start) continue;
        ResolvedAnnotation r;
        r.ann = &ann;
        bool found = false;
        for (uint32_t ci = 0; ci < shaped.clusters.size(); ++ci) {
            const ShapedCluster& c = shaped.clusters[ci];
            if (c.charStart >= ann.start && c.charStart < ann.end) {
                if (!found) { r.clusterStart = ci; found = true; }
                r.clusterEnd = ci + 1;
            }
        }
        if (found) out.push_back(r);
    }
    return out;
}

std::vector<std::u16string> splitMonoRuby(const std::u16string& t) {
    std::vector<std::u16string> parts;
    size_t pos = 0;
    while (true) {
        const size_t bar = t.find(u'|', pos);
        if (bar == std::u16string::npos) { parts.push_back(t.substr(pos)); break; }
        parts.push_back(t.substr(pos, bar - pos));
        pos = bar + 1;
    }
    return parts;
}

} // namespace

//------------------------------------------------------------------------------

std::vector<LineItem> buildLineItems(const ShapedText& shaped,
                                     const std::vector<Annotation>& annotations,
                                     const SpacingOptions& opts,
                                     const ItemBuildContext& ctx) {
    std::vector<LineItem> items;
    if (shaped.clusters.empty() || !ctx.styles) return items;
    items.reserve(shaped.clusters.size() * 3 + 2);

    const TextStyle& base = ctx.baseStyle ? *ctx.baseStyle : (*ctx.styles)[0];
    const Pt baseEm = base.size;
    const uint32_t clusterCount = static_cast<uint32_t>(shaped.clusters.size());
    auto emOf = [&](uint32_t ci) -> Pt {
        const uint32_t si = shaped.clusters[ci].styleIndex;
        return si < ctx.styles->size() ? (*ctx.styles)[si].size : baseEm;
    };

    // 欧文の分割機会（UAX #14）
    const std::vector<text::BreakOpportunity> uax =
        text::lineBreakOpportunities(shaped.sourceText, base.language.c_str());

    //--------------------------------------------------------------------------
    // 前処理: 注記をクラスタ単位の「ボディ幅・固定アキ・付随グリフ」へ落とす
    //--------------------------------------------------------------------------
    const std::vector<ResolvedAnnotation> resolved = resolveAnnotations(shaped, annotations);

    constexpr int kNone = -1;
    std::vector<int> composite(clusterCount, kNone);
    std::vector<uint8_t> skipped(clusterCount, 0);
    std::vector<uint8_t> noBreak(clusterCount, 0);
    std::vector<Pt> bodyWidths(clusterCount, 0.0f);
    std::vector<Pt> gapBefore(clusterCount + 1, 0.0f);
    std::vector<std::vector<PlacedGlyph>> attached(clusterCount);
    std::vector<Pt> extentMin(clusterCount, 0.0f), extentMax(clusterCount, 0.0f);
    std::vector<CompositeResult> composites(resolved.size());

    // 1) 合成 Box（縦中横・割注）
    for (int ri = 0; ri < static_cast<int>(resolved.size()); ++ri) {
        const ResolvedAnnotation& r = resolved[ri];
        if (r.ann->type != AnnotationType::TateChuYoko && r.ann->type != AnnotationType::Warichu) {
            continue;
        }
        const size_t s = shaped.clusters[r.clusterStart].charStart;
        const size_t e = shaped.clusters[r.clusterEnd - 1].charEnd;
        const uint32_t si = shaped.clusters[r.clusterStart].styleIndex;
        const Pt em = emOf(r.clusterStart);
        if (r.ann->type == AnnotationType::TateChuYoko) {
            composites[ri] = layoutTateChuYoko(shaped.sourceText.substr(s, e - s), ctx, em, si);
        } else {
            const std::u16string content =
                r.ann->text.empty() ? shaped.sourceText.substr(s, e - s) : r.ann->text;
            composites[ri] = layoutWarichu(content, ctx, em, r.ann->scale, opts, si);
        }
        if (!composites[ri].valid) continue;
        composite[r.clusterStart] = ri;
        for (uint32_t ci = r.clusterStart + 1; ci < r.clusterEnd; ++ci) skipped[ci] = 1;
    }

    // 2) 仮想ボディ幅
    for (uint32_t ci = 0; ci < clusterCount; ++ci) {
        if (skipped[ci]) continue;
        if (composite[ci] != kNone) {
            bodyWidths[ci] = composites[composite[ci]].width;
            continue;
        }
        const ShapedCluster& c = shaped.clusters[ci];
        if (c.object) {
            bodyWidths[ci] = c.advance;   // 行内画像などはシェイパーの送りがそのまま箱
            continue;
        }
        const Pt em = emOf(ci);
        // palt 等でフォントが詰めている文字は送りをそのまま箱にする（JLReq の半角化と二重に詰めない）
        const bool proportional = c.styleIndex < ctx.styles->size() && (*ctx.styles)[c.styleIndex].hasProportionalFeature();
        float bodyEm = (opts.punctuationSpacing && !proportional) ? text::getBodyWidth(c.charClass) : 0.0f;
        if (!opts.punctuationSpacing && !proportional && text::isJapanese(c.charClass)) bodyEm = 1.0f;
        Pt w = (bodyEm > 0.0f) ? bodyEm * em : c.advance;
        if (bodyEm > 0.0f && c.advance > 0.0f) w = std::min(w, c.advance);
        bodyWidths[ci] = w;
    }

    // 3) ルビ・圏点・字取り
    for (const ResolvedAnnotation& r : resolved) {
        std::vector<uint32_t> parents;
        Pt parentWidth = 0.0f;
        for (uint32_t k = r.clusterStart; k < r.clusterEnd; ++k) {
            if (skipped[k]) continue;
            parents.push_back(k);
            parentWidth += bodyWidths[k];
        }
        if (parents.empty()) continue;
        const int parentCount = static_cast<int>(parents.size());
        const Pt em = emOf(parents.front());
        const uint32_t si = shaped.clusters[parents.front()].styleIndex;
        // 注記と親文字の間隔（Annotation::offset、親の em）: 組んだ注記のグリフを注記側へずらす
        const Pt annOffset = r.ann->offset * em;
        const float annSide = annotationSide(ctx.writingMode);
        auto applyOffset = [&](RubyResult& rr) {
            if (annOffset == 0.0f || !rr.valid) return;
            for (PlacedGlyph& g : rr.glyphs) g.block += annSide * annOffset;
            if (annSide > 0.0f) rr.extentMax += annOffset; else rr.extentMin -= annOffset;
        };

        switch (r.ann->type) {
        case AnnotationType::Ruby: {
            const Pt rubySize = em * r.ann->scale;
            std::vector<std::u16string> parts;
            if (r.ann->rubyMode != RubyMode::Group) parts = splitMonoRuby(r.ann->text);

            // 熟語ルビ: 各部分を組んで、親に収まるか／熟語に収まるかで置き方を決める
            bool jukugoHandled = false;
            if (r.ann->rubyMode == RubyMode::Jukugo && static_cast<int>(parts.size()) == parentCount) {
                const TextStyle rubyStyle = derivedStyle(ctx, rubySize);
                std::vector<ShapedText> shapedParts;
                std::vector<Pt> pw;
                bool allFit = true;
                Pt totalRuby = 0.0f;
                for (int pi = 0; pi < parentCount; ++pi) {
                    shapedParts.push_back(shapeText(parts[pi], rubyStyle, ctx.fonts, ctx.writingMode,
                                                    TextOrientation::Mixed));
                    pw.push_back(bodyWidths[parents[pi]]);
                    totalRuby += shapedParts.back().advance;
                    if (shapedParts.back().advance > pw.back() + 0.01f) allFit = false;
                }
                if (allFit) {
                    // 全部が親に収まる: 下のモノルビ経路で 1 字ずつ中付きにする
                } else if (totalRuby <= parentWidth + 0.01f) {
                    RubyResult rr = layoutJukugoRuby(shapedParts, pw, em, rubySize, si, ctx.writingMode);
                    applyOffset(rr);
                    if (rr.valid) {
                        const uint32_t head = parents.front();
                        attached[head].insert(attached[head].end(), rr.glyphs.begin(), rr.glyphs.end());
                        extentMin[head] = std::min(extentMin[head], rr.extentMin);
                        extentMax[head] = std::max(extentMax[head], rr.extentMax);
                        for (uint32_t ci = r.clusterStart + 1; ci < r.clusterEnd; ++ci) noBreak[ci] = 1;
                        jukugoHandled = true;
                    }
                } else {
                    // 熟語全体でも収まらない: グループルビとして親文字列を広げる
                    std::u16string joined;
                    for (const std::u16string& p : parts) joined += p;
                    const bool hangBefore = (r.clusterStart > 0) &&
                        canRubyOverhang(shaped.clusters[r.clusterStart - 1].charClass);
                    const bool hangAfter = (r.clusterEnd < clusterCount) &&
                        canRubyOverhang(shaped.clusters[r.clusterEnd].charClass);
                    RubyResult rr = layoutRuby(joined, ctx, em, rubySize, parentWidth, parentCount,
                                               hangBefore, hangAfter, si);
                    applyOffset(rr);
                    if (rr.valid) {
                        const uint32_t head = parents.front();
                        attached[head].insert(attached[head].end(), rr.glyphs.begin(), rr.glyphs.end());
                        extentMin[head] = std::min(extentMin[head], rr.extentMin);
                        extentMax[head] = std::max(extentMax[head], rr.extentMax);
                        if (rr.endGap > 0.0f) {
                            gapBefore[r.clusterStart] += rr.endGap;
                            gapBefore[r.clusterEnd] += rr.endGap;
                        }
                        if (rr.innerGap > 0.0f) {
                            for (int pi = 1; pi < parentCount; ++pi) gapBefore[parents[pi]] += rr.innerGap;
                        }
                        for (uint32_t ci = r.clusterStart + 1; ci < r.clusterEnd; ++ci) noBreak[ci] = 1;
                    }
                    jukugoHandled = true;
                }
            }
            if (jukugoHandled) break;

            if (r.ann->rubyMode != RubyMode::Group && static_cast<int>(parts.size()) == parentCount) {
                for (int pi = 0; pi < parentCount; ++pi) {
                    const uint32_t k = parents[pi];
                    RubyResult rr = layoutRuby(parts[pi], ctx, em, rubySize, bodyWidths[k], 1,
                                               false, false, si);
                    applyOffset(rr);
                    if (!rr.valid) continue;
                    attached[k].insert(attached[k].end(), rr.glyphs.begin(), rr.glyphs.end());
                    extentMin[k] = std::min(extentMin[k], rr.extentMin);
                    extentMax[k] = std::max(extentMax[k], rr.extentMax);
                    if (rr.endGap > 0.0f) {
                        gapBefore[k] += rr.endGap;
                        gapBefore[k + 1] += rr.endGap;
                    }
                }
            } else {
                const bool hangBefore = (r.clusterStart > 0) &&
                    canRubyOverhang(shaped.clusters[r.clusterStart - 1].charClass);
                const bool hangAfter = (r.clusterEnd < clusterCount) &&
                    canRubyOverhang(shaped.clusters[r.clusterEnd].charClass);
                RubyResult rr = layoutRuby(r.ann->text, ctx, em, rubySize, parentWidth,
                                           parentCount, hangBefore, hangAfter, si);
                applyOffset(rr);
                if (!rr.valid) break;
                const uint32_t head = parents.front();
                attached[head].insert(attached[head].end(), rr.glyphs.begin(), rr.glyphs.end());
                extentMin[head] = std::min(extentMin[head], rr.extentMin);
                extentMax[head] = std::max(extentMax[head], rr.extentMax);
                if (rr.endGap > 0.0f) {
                    gapBefore[r.clusterStart] += rr.endGap;
                    gapBefore[r.clusterEnd] += rr.endGap;
                }
                if (rr.innerGap > 0.0f) {
                    for (int pi = 1; pi < parentCount; ++pi) gapBefore[parents[pi]] += rr.innerGap;
                }
                for (uint32_t ci = r.clusterStart + 1; ci < r.clusterEnd; ++ci) noBreak[ci] = 1;
            }
            break;
        }
        case AnnotationType::Emphasis: {
            const Pt markSize = em * r.ann->scale;
            const float side = annotationSide(ctx.writingMode) * (r.ann->oppositeSide ? -1.0f : 1.0f);
            for (uint32_t k : parents) {
                const size_t before = attached[k].size();
                if (layoutEmphasisMark(r.ann->mark, ctx, em, markSize, bodyWidths[k], si, attached[k], side)) {
                    for (size_t g = before; g < attached[k].size(); ++g) attached[k][g].block += side * annOffset;
                    if (side > 0.0f) extentMax[k] = std::max(extentMax[k], em * 0.5f + markSize + annOffset);
                    else             extentMin[k] = std::min(extentMin[k], -(em * 0.5f + markSize + annOffset));
                }
            }
            break;
        }
        case AnnotationType::Jidori: {
            const Pt extra = r.ann->jidoriEm * em - parentWidth;
            if (extra <= 0.0f) break;
            if (parentCount == 1) {
                gapBefore[r.clusterStart] += extra * 0.5f;
                gapBefore[r.clusterEnd] += extra * 0.5f;
            } else {
                const Pt gap = extra / static_cast<float>(parentCount - 1);
                for (int pi = 1; pi < parentCount; ++pi) gapBefore[parents[pi]] += gap;
            }
            for (uint32_t ci = r.clusterStart + 1; ci < r.clusterEnd; ++ci) noBreak[ci] = 1;
            break;
        }
        case AnnotationType::TateChuYoko:
        case AnnotationType::Warichu:
        case AnnotationType::Indent:
        case AnnotationType::MoveTo:
            break;
        }
    }

    //--------------------------------------------------------------------------
    // 本体
    //--------------------------------------------------------------------------
    bool prevWasBox = false;
    bool prevProportional = false;
    CharClass prevClass = CharClass::Unknown;
    Pt prevBoxWidth = 0.0f;
    size_t prevCharBegin = 0;
    // タブ・MoveTo 用: 段落頭からの自然幅（1 行目に載る前提の位置）
    Pt naturalPos = 0.0f;
    auto naturalWidthUntilTab = [&](uint32_t from) {
        Pt w = 0.0f;
        for (uint32_t k = from; k < clusterCount; ++k) {
            if (skipped[k]) continue;
            const ShapedCluster& c = shaped.clusters[k];
            if (c.charStart < shaped.sourceText.size() && shaped.sourceText[c.charStart] == u'\t') break;
            w += (c.charClass == CharClass::Space && composite[k] == kNone && !ctx.preserveSpaces) ? c.advance : bodyWidths[k];
        }
        return w;
    };
    auto tabStopWidth = [&](Pt pos, uint32_t nextCluster, Pt em) {
        // 次のタブ位置（無ければ tabWidth × em ごと）。Center / Right / Decimal は続く文字の幅を差し引く
        const Pt textW = naturalWidthUntilTab(nextCluster);
        if (ctx.tabStops && !ctx.tabStops->empty()) {
            for (const TabStop& ts : *ctx.tabStops) {
                Pt target = ts.position;
                if (ts.align == TabAlign::Right) target -= textW;
                else if (ts.align == TabAlign::Center) target -= textW * 0.5f;
                else if (ts.align == TabAlign::Decimal) {
                    Pt beforeDot = 0.0f;
                    for (uint32_t k = nextCluster; k < clusterCount; ++k) {
                        if (skipped[k]) continue;
                        const ShapedCluster& c = shaped.clusters[k];
                        if (c.charStart >= shaped.sourceText.size() || shaped.sourceText[c.charStart] == u'\t') break;
                        if (text::codePointAt(shaped.sourceText, c.charStart) == ts.decimalChar) break;
                        beforeDot += bodyWidths[k];
                    }
                    target -= beforeDot;
                }
                if (ts.position > pos + 0.01f) return std::max(0.0f, target - pos);
            }
            return em * 0.5f;    // タブ位置を使い切ったら半角のアキ
        }
        const Pt unit = std::max(1, ctx.tabWidth) * em;
        const Pt next = (std::floor(pos / unit + 1e-4f) + 1.0f) * unit;
        return next - pos;
    };
    // --- 欧文のハイフネーション: 単語の中で切ってよいクラスタに印を付ける ---
    // hyphenAt[ci] = そのクラスタの手前で切ってよい（行末にハイフンが出る）
    std::vector<uint8_t> hyphenAt(clusterCount, 0);
    if (ctx.wrap != WrapMode::None) {
        for (uint32_t ci = 0; ci < clusterCount; ++ci) {
            if (skipped[ci]) continue;
            const ShapedCluster& c = shaped.clusters[ci];
            // ソフトハイフンは辞書が無くても常に分割位置。字面は出さず、次の文字の手前で切る
            // （ソフトハイフン自体は前の行に残るが幅 0 なので見えない）
            if (c.charStart < shaped.sourceText.size() &&
                text::codePointAt(shaped.sourceText, c.charStart) == text::kSoftHyphen) {
                for (uint32_t next = ci + 1; next < clusterCount; ++next) {
                    if (skipped[next]) continue;
                    hyphenAt[next] = 1;
                    break;
                }
            }
        }
        if (ctx.hyphenation) {
            for (uint32_t ci = 0; ci < clusterCount;) {
                if (skipped[ci] || !text::isWestern(shaped.clusters[ci].charClass)) { ++ci; continue; }
                // 単語（欧文の文字が続く範囲。数字・記号で切れる）
                uint32_t end = ci;
                std::u32string word;
                std::vector<uint32_t> clusterOf;      // 単語の k 文字目 → クラスタ番号
                while (end < clusterCount && !skipped[end] &&
                       text::isWestern(shaped.clusters[end].charClass)) {
                    const size_t at = shaped.clusters[end].charStart;
                    if (at >= shaped.sourceText.size()) break;
                    const char32_t cp = text::codePointAt(shaped.sourceText, at);
                    if (shaped.clusters[end].charClass == text::CharClass::Digit || cp == text::kSoftHyphen) break;
                    word += cp;
                    clusterOf.push_back(end);
                    ++end;
                }
                if (word.size() >= 4) {
                    const uint32_t si = shaped.clusters[ci].styleIndex;
                    const std::string language = si < ctx.styles->size() ? (*ctx.styles)[si].language : std::string();
                    if (const text::Hyphenator* h = ctx.hyphenation->find(language)) {
                        for (size_t k : h->hyphenate(word, ctx.hyphenMinLeft, ctx.hyphenMinRight)) {
                            if (k < clusterOf.size()) hyphenAt[clusterOf[k]] = 1;
                        }
                    }
                }
                ci = std::max(end, ci + 1);
            }
        }
    }
    // ハイフンの箱（行末に出すグリフ）。スタイルごとに 1 回だけ組む
    std::map<uint32_t, std::pair<Pt, std::vector<PlacedGlyph>>> hyphenBoxes;
    auto hyphenBox = [&](uint32_t styleIndex) -> const std::pair<Pt, std::vector<PlacedGlyph>>& {
        auto it = hyphenBoxes.find(styleIndex);
        if (it != hyphenBoxes.end()) return it->second;
        const TextStyle& st = styleIndex < ctx.styles->size() ? (*ctx.styles)[styleIndex] : *ctx.baseStyle;
        const ShapedText sh = shapeText(u"-", st, ctx.fonts, ctx.writingMode, ctx.orientation);
        std::vector<PlacedGlyph> glyphs = sh.glyphs;
        for (PlacedGlyph& g : glyphs) g.styleIndex = styleIndex;
        return hyphenBoxes.emplace(styleIndex, std::make_pair(sh.advance, std::move(glyphs))).first->second;
    };

    // MoveTo 注記（クラスタ番号 → 位置）
    std::vector<Pt> moveTo(clusterCount, -1.0f);
    for (const Annotation& a : annotations) {
        if (a.type != AnnotationType::MoveTo) continue;
        for (uint32_t k = 0; k < clusterCount; ++k) {
            if (shaped.clusters[k].charStart == a.start) { moveTo[k] = a.position; break; }
        }
    }
    Pt prevEm = baseEm;
    size_t prevCharEnd = 0;

    for (uint32_t ci = 0; ci < clusterCount; ++ci) {
        if (skipped[ci]) continue;
        const ShapedCluster& cluster = shaped.clusters[ci];
        const CharClass cls = cluster.charClass;
        const int compIdx = composite[ci];
        const Pt em = emOf(ci);
        const bool proportional = compIdx == kNone && cluster.styleIndex < ctx.styles->size() &&
                                  (*ctx.styles)[cluster.styleIndex].hasProportionalFeature();

        // タブ: 次のタブストップまでの、描かない箱（空白を保持する段落では先に空白へ展開されている）
        if (compIdx == kNone && cluster.charStart < shaped.sourceText.size() &&
            shaped.sourceText[cluster.charStart] == u'\t') {
            const Pt w = tabStopWidth(naturalPos, ci + 1, em);
            LineItem tab = LineItem::box(w, ci, cluster.charStart);
            tab.ownGlyphs = true;      // グリフ無し
            items.push_back(std::move(tab));
            naturalPos += w;
            prevWasBox = false;        // タブの後ろにはアキを入れない（切れ目にもしない）
            prevClass = CharClass::Space;
            prevCharEnd = cluster.charEnd;
            continue;
        }
        // MoveTo: この文字を指定位置から始める（手前なら空きの箱を入れる）
        if (moveTo[ci] >= 0.0f && moveTo[ci] > naturalPos + 0.01f) {
            const Pt w = moveTo[ci] - naturalPos;
            LineItem gap = LineItem::box(w, ci, cluster.charStart);
            gap.ownGlyphs = true;
            items.push_back(std::move(gap));
            naturalPos += w;
            prevWasBox = false;
            prevClass = CharClass::Space;
        }

        // 欧文間隔は Box ではなく Glue（そこが唯一の欧文の切れ目）。空白を保持する段落では固定幅の箱
        if (cls == CharClass::Space && compIdx == kNone && !ctx.preserveSpaces) {
            const Pt w = cluster.advance;
            // Glue は前が Box なら常に切れ目になるので、折り返さない指定では手前に無限ペナルティを置く
            if (ctx.wrap == WrapMode::None && prevWasBox) {
                items.push_back(LineItem::penaltyItem(kInfinitePenalty, 0.0f, cluster.charStart));
            }
            items.push_back(LineItem::gluePt(w, w * 0.5f, w / 3.0f, cluster.charStart));
            items.back().level = cluster.level;    // 双方向の並べ替えで空白自身のレベルを使う（L1 の解決済み）
            naturalPos += w;
            prevWasBox = false;
            prevClass = cls;
            prevCharEnd = cluster.charEnd;
            continue;
        }

        CharClass spacingClass = (compIdx != kNone) ? CharClass::Ideographic : cls;
        if (cls == CharClass::Hyphen && text::isWestern(prevClass)) {
            // 欧文に挟まれたハイフン類（en dash 等）は欧文の一部として組む
            uint32_t nj = ci + 1;
            while (nj < clusterCount && skipped[nj]) ++nj;
            if (nj < clusterCount && text::isWestern(shaped.clusters[nj].charClass)) {
                spacingClass = CharClass::Western;
            }
        }
        const Pt boxWidth = bodyWidths[ci];

        // ハイフネーションの位置（辞書またはソフトハイフン）: 単語の途中なのでアキは入れず、
        // 「切ったらハイフンが出る」Penalty だけ置く
        if (prevWasBox && hyphenAt[ci] && !noBreak[ci]) {
            const auto& hb = hyphenBox(cluster.styleIndex);
            LineItem p = LineItem::penaltyItem(ctx.hyphenPenalty, hb.first, cluster.charStart);
            p.breakGlyphs = hb.second;
            items.push_back(std::move(p));
        } else if (prevWasBox) {
            const bool latinBoundary =
                (text::isJapanese(prevClass) && text::isWestern(spacingClass)) ||
                (text::isWestern(prevClass) && text::isJapanese(spacingClass));
            text::GlueSpec spec = text::getSpacing(prevClass, spacingClass);
            if (latinBoundary ? !opts.latinGap : !opts.punctuationSpacing) spec = text::GlueSpec{};
            // palt 等で詰めた文字の前後の約物のアキはフォントに任せる（和欧間のアキは残す）
            if (!latinBoundary && (proportional || prevProportional)) spec = text::GlueSpec{};
            const Pt glueEm = std::max(prevEm, em);
            const Pt natural = spec.natural * glueEm + ctx.letterSpacing * em + gapBefore[ci];
            Pt stretch = spec.stretch * glueEm;
            Pt shrink = spec.shrink * glueEm;
            // 和字同士の字間は追い出し（字間を空けて揃える）のために少し伸びる
            if (text::isJapanese(prevClass) && text::isJapanese(spacingClass)) {
                stretch += opts.kanjiSkipStretch * glueEm;
                shrink += opts.kanjiSkipShrink * glueEm;
            }

            const bool uaxAllowed = prevCharEnd > 0 && prevCharEnd - 1 < uax.size() &&
                                    uax[prevCharEnd - 1] != text::BreakOpportunity::Prohibited;
            const char32_t cpBefore = (prevCharEnd > 0) ? text::codePointAt(shaped.sourceText, prevCharBegin) : 0;
            const char32_t cpAfter = text::codePointAt(shaped.sourceText, cluster.charStart);
            float breakPenalty = breakPenaltyBetween(prevClass, spacingClass, uaxAllowed, cpBefore, cpAfter,
                                                     opts, ctx.wrap);
            if (noBreak[ci]) breakPenalty = kInfinitePenalty;
            const bool breakable = breakPenalty < kInfinitePenalty;

            // ぶら下げ: 句読点の直後は「幅が負の Penalty」で切る
            if (breakable && breakPenalty <= 0.0f && opts.hangingPunctuation && text::isHangable(prevClass)) {
                items.push_back(LineItem::penaltyItem(0.0f, -prevBoxWidth, cluster.charStart));
            } else if (!breakable) {
                items.push_back(LineItem::penaltyItem(kInfinitePenalty, 0.0f, cluster.charStart));
            } else if (breakPenalty > 0.0f) {
                items.push_back(LineItem::penaltyItem(breakPenalty, 0.0f, cluster.charStart));   // 弱い禁則
            }
            if (breakable || natural != 0.0f || stretch != 0.0f || shrink != 0.0f) {
                items.push_back(LineItem::gluePt(natural, stretch, shrink, cluster.charStart));
                naturalPos += natural;
            }
        }
        naturalPos += boxWidth;

        // ソフトハイフンは分割位置を示すだけで、字面は出さない
        if (compIdx == kNone && cluster.charStart < shaped.sourceText.size() &&
            text::codePointAt(shaped.sourceText, cluster.charStart) == text::kSoftHyphen) {
            LineItem soft = LineItem::box(0.0f, ci, cluster.charStart);
            soft.ownGlyphs = true;
            items.push_back(std::move(soft));
            prevWasBox = true;
            prevProportional = proportional;
            prevCharBegin = cluster.charStart;
            prevClass = prevClass;      // 直前の文字クラスを保つ（単語の続きとして扱う）
            prevBoxWidth = 0.0f;
            prevEm = em;
            prevCharEnd = cluster.charEnd;
            continue;
        }

        LineItem box = LineItem::box(boxWidth, ci, cluster.charStart);
        if (compIdx != kNone) {
            box.glyphs = composites[compIdx].glyphs;
            box.ownGlyphs = true;
        } else {
            box.glyphOffset = bodyGlyphOffset(cls, cluster.advance, boxWidth);
        }
        if (!attached[ci].empty()) {
            box.glyphs.insert(box.glyphs.end(), attached[ci].begin(), attached[ci].end());
            box.extentMin = extentMin[ci];
            box.extentMax = extentMax[ci];
        }
        // 合成・付随グリフは別のテキストを組んだものなので、親 Box の文字位置へ揃える
        for (PlacedGlyph& glyph : box.glyphs) glyph.charIndex = static_cast<uint32_t>(cluster.charStart);
        items.push_back(std::move(box));

        prevWasBox = true;
        prevProportional = proportional;
        prevCharBegin = cluster.charStart;
        prevClass = spacingClass;
        prevBoxWidth = boxWidth;
        prevEm = em;
        prevCharEnd = cluster.charEnd;
    }

    // 段落の終端（TeX と同じ形）
    const size_t tailIndex = shaped.clusters.back().charEnd;
    items.push_back(LineItem::penaltyItem(kInfinitePenalty, 0.0f, tailIndex));
    items.push_back(LineItem::gluePt(0.0f, 1.0e6f, 0.0f, tailIndex));
    items.push_back(LineItem::penaltyItem(kForcedBreakPenalty, 0.0f, tailIndex));
    return items;
}

} // namespace typeset::inl
