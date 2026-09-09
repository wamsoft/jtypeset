/**
 * shaper.cpp — Itemizer ＋ HarfBuzz シェイピング
 */

#include "typeset/inl/shaper.hpp"

#include <algorithm>

#include <hb.h>
#include <glyphware/Bidi.h>

#include "typeset/dl/glyph_transform.hpp"
#include "typeset/text/orientation.hpp"
#include "typeset/text/utf.hpp"

namespace typeset::inl {

namespace {

struct Segment {
    size_t start = 0;
    size_t end = 0;
    std::shared_ptr<glyphware::Face> face;
    bool upright = true;
    uint32_t styleIndex = 0;
    uint8_t level = 0;      ///< UAX #9 の埋め込みレベル
};

/**
 * UAX #9 の埋め込みレベルを UTF-16 の位置ごとに求める（glyphware の SheenBidi。UTF-8 で解析して写す）
 * @return 段落レベル（0 = LTR、1 = RTL）
 */
int bidiLevels(const std::u16string& text, Direction dir, std::vector<uint8_t>& levels) {
    levels.assign(text.size(), 0);
    std::string utf8;
    std::vector<size_t> offset(text.size() + 1, 0);
    for (size_t i = 0; i < text.size();) {
        size_t len = 1;
        const char32_t cp = text::codePointAt(text, i, len);
        offset[i] = utf8.size();
        if (len == 2 && i + 1 < text.size()) offset[i + 1] = utf8.size();
        std::u16string one;
        text::appendCodePoint(one, cp);
        utf8 += text::utf16ToUtf8(one);
        i += len;
    }
    offset[text.size()] = utf8.size();
    const glyphware::BaseDirection base = dir == Direction::Ltr ? glyphware::BaseDirection::LTR
                                        : dir == Direction::Rtl ? glyphware::BaseDirection::RTL
                                                                : glyphware::BaseDirection::Auto;
    const glyphware::BidiResult r = glyphware::bidiAnalyze(utf8, base);
    for (size_t i = 0; i < text.size(); ++i) {
        const size_t o = offset[i];
        levels[i] = o < r.levels.size() ? r.levels[o] : static_cast<uint8_t>(r.paragraphLevel);
    }
    return r.paragraphLevel;
}

/// 絵文字の結合要素（ZWJ・異体字セレクタ・肌色修飾子・タグ・キーキャップ）。前の文字と同じ face・向きで
/// 同じセグメントに入れないと、HarfBuzz が結合（ZWJ シーケンス・国旗・肌色）を作れない
bool isEmojiExtender(char32_t cp) {
    return cp == 0x200D || cp == 0xFE0E || cp == 0xFE0F || cp == 0x20E3 ||
           (cp >= 0x1F3FB && cp <= 0x1F3FF) || (cp >= 0xE0020 && cp <= 0xE007F);
}

bool resolveUpright(WritingMode wm, TextOrientation ori, char32_t cp) {
    if (!isVertical(wm)) return true;
    switch (ori) {
    case TextOrientation::Upright: return true;
    case TextOrientation::Sideways: return false;
    default: return text::getCharOrientation(cp) == text::CharOrientation::Upright;
    }
}

/// スタイル・face・向きで分割する
std::vector<Segment> itemize(const std::u16string& text, const std::vector<StyleRun>& runs,
                             const ShapeContext& ctx, const std::vector<uint8_t>& levels) {
    std::vector<Segment> segs;
    const bool vertical = isVertical(ctx.writingMode);
    for (const StyleRun& run : runs) {
        if (run.end <= run.start || !ctx.styles || run.styleIndex >= ctx.styles->size()) continue;
        const TextStyle& style = (*ctx.styles)[run.styleIndex];
        const TextOrientation ori = style.orientation.value_or(ctx.orientation);
        const size_t end = std::min(run.end, text.size());
        for (size_t i = run.start; i < end;) {
            size_t len = 1;
            const char32_t cp = text::codePointAt(text, i, len);
            if (isEmojiExtender(cp) && !segs.empty() && segs.back().end == i &&
                segs.back().styleIndex == run.styleIndex) {
                segs.back().end = i + len;   // 前の文字に付ける（face・向きを引き継ぐ）
                i += len;
                continue;
            }
            auto face = ctx.fonts.resolve(style.font, cp, style.language);
            const bool upright = resolveUpright(ctx.writingMode, ori, cp);
            // 正立の縦組みは上から下へ論理順に置くので双方向の並べ替えをしない（CSS の upright と同じ）
            const uint8_t level = (vertical && upright) ? 0 : (i < levels.size() ? levels[i] : 0);
            if (!segs.empty()) {
                Segment& last = segs.back();
                if (last.end == i && last.face == face && last.upright == upright &&
                    last.styleIndex == run.styleIndex && last.level == level) {
                    last.end = i + len;
                    i += len;
                    continue;
                }
            }
            Segment s;
            s.start = i;
            s.end = i + len;
            s.level = level;
            s.face = face;
            s.upright = upright;
            s.styleIndex = run.styleIndex;
            segs.push_back(std::move(s));
            i += len;
        }
    }
    return segs;
}

} // namespace

Pt baselineOffset(const glyphware::Face& face, Pt size, WritingMode wm) {
    Pt ascent = 0.0f, descent = 0.0f;
    font::faceAscentDescent(face, size, ascent, descent);
    if (isVertical(wm)) {
        // 横倒し: アセントは +block（右）側へ倒れる。帯の中点を中心線へ
        return (descent - ascent) * 0.5f;
    }
    // 横組み: ベースラインは中心線より (ascent − descent)/2 だけ下
    return (ascent - descent) * 0.5f;
}

ShapedText shapeText(const std::u16string& text, const TextStyle& style,
                     font::FontSet& fonts, WritingMode wm, TextOrientation orientation) {
    std::vector<TextStyle> styles{style};
    ShapeContext ctx{fonts, wm, orientation, &styles};
    std::vector<StyleRun> runs{StyleRun{0, text.size(), 0}};
    return shapeText(text, runs, ctx);
}

ShapedText shapeText(const std::u16string& text, const std::vector<StyleRun>& runs,
                     const ShapeContext& ctx) {
    ShapedText result;
    result.sourceText = text;
    if (text.empty() || !ctx.styles) return result;

    const bool vertical = isVertical(ctx.writingMode);
    std::vector<uint8_t> levels;
    result.paragraphLevel = bidiLevels(text, ctx.direction, levels);
    const std::vector<Segment> segs = itemize(text, runs, ctx, levels);
    for (const Segment& s : segs) if (s.level != 0) result.bidi = true;

    hb_buffer_t* buffer = hb_buffer_create();
    const uint16_t* raw = reinterpret_cast<const uint16_t*>(text.data());

    Pt pen = 0.0f;
    Pt blockMin = 0.0f, blockMax = 0.0f;
    bool haveExtent = false;

    for (const Segment& seg : segs) {
        const TextStyle& style = (*ctx.styles)[seg.styleIndex];

        // 行内オブジェクト（数式など）: 1 クラスタの箱。横組みはベースラインを本文に揃え、縦組みは横倒しで中心へ
        if (ctx.objects && seg.styleIndex < ctx.objects->size() && (*ctx.objects)[seg.styleIndex] &&
            (*ctx.objects)[seg.styleIndex]->ok()) {
            const std::shared_ptr<const obj::ObjectResult>& ob = (*ctx.objects)[seg.styleIndex];
            const Pt adv = ob->size.w;      // 縦組みでも横倒しなので幅が送り
            const Pt h = ob->size.h;
            Pt top = -h * 0.5f;
            if (!vertical && ob->hasBaseline) {
                std::shared_ptr<glyphware::Face> primary = ctx.fonts.primary(style.font);
                if (primary) top = baselineOffset(*primary, style.size, ctx.writingMode) - ob->baseline;
            }

            ShapedCluster sc;
            sc.glyphStart = static_cast<uint32_t>(result.glyphs.size());
            sc.glyphCount = 1;
            sc.charStart = seg.start;
            sc.charEnd = seg.end;
            sc.origin = pen;
            sc.advance = adv;
            sc.upright = true;
            sc.styleIndex = seg.styleIndex;
            sc.charClass = text::CharClass::Ideographic;
            sc.object = true;

            PlacedGlyph g;
            g.object = ob;
            g.inline_ = pen;
            g.block = top;
            g.advance = adv;
            g.boxAfter = adv;
            g.size = style.size;
            g.charIndex = static_cast<uint32_t>(seg.start);
            g.styleIndex = seg.styleIndex;
            result.glyphs.push_back(std::move(g));
            result.clusters.push_back(sc);
            pen += adv;
            blockMin = std::min(blockMin, top);
            blockMax = std::max(blockMax, top + h);
            haveExtent = true;
            continue;
        }

        // 行内画像／プレースホルダ: 1 クラスタの箱として置く（中心を行の中心線へ）。プレースホルダは描かない空箱
        const bool placeholder = ctx.placeholders && seg.styleIndex < ctx.placeholders->size() &&
                                 (*ctx.placeholders)[seg.styleIndex];
        if (placeholder ||
            (ctx.images && seg.styleIndex < ctx.images->size() && (*ctx.images)[seg.styleIndex])) {
            const std::shared_ptr<const dl::Image> img =
                placeholder ? nullptr : (*ctx.images)[seg.styleIndex];
            Size sz = (ctx.imageSizes && seg.styleIndex < ctx.imageSizes->size())
                          ? (*ctx.imageSizes)[seg.styleIndex] : Size{};
            if (img) {
                const float pw = static_cast<float>(std::max(1, img->width));
                const float ph = static_cast<float>(std::max(1, img->height));
                if (sz.w <= 0.0f && sz.h <= 0.0f) { sz.w = pw; sz.h = ph; }
                else if (sz.w <= 0.0f) sz.w = sz.h * pw / ph;
                else if (sz.h <= 0.0f) sz.h = sz.w * ph / pw;
            } else {
                if (sz.w <= 0.0f) sz.w = style.size;
                if (sz.h <= 0.0f) sz.h = style.size;
            }
            const Pt adv = vertical ? sz.h : sz.w;
            const Pt half = vertical ? sz.w * 0.5f : sz.h * 0.5f;

            ShapedCluster sc;
            sc.glyphStart = static_cast<uint32_t>(result.glyphs.size());
            sc.glyphCount = 1;
            sc.charStart = seg.start;
            sc.charEnd = seg.end;
            sc.origin = pen;
            sc.advance = adv;
            sc.upright = true;
            sc.styleIndex = seg.styleIndex;
            sc.charClass = text::CharClass::Ideographic;
            sc.object = true;

            PlacedGlyph g;
            g.image = img;
            g.placeholder = placeholder;
            g.imageSize = sz;
            g.inline_ = pen;
            g.block = 0.0f;
            g.advance = adv;
            g.boxAfter = adv;
            g.size = style.size;
            g.charIndex = static_cast<uint32_t>(seg.start);
            g.styleIndex = seg.styleIndex;
            result.glyphs.push_back(std::move(g));
            result.clusters.push_back(sc);
            pen += adv;
            blockMin = std::min(blockMin, -half);
            blockMax = std::max(blockMax, half);
            haveExtent = true;
            continue;
        }

        if (!seg.face) continue;
        const Pt size = style.size;
        const float upem = font::unitsPerEm(*seg.face);
        const float s = size / upem;

        // 行のメトリクス基準は第一候補フォント（無ければこの face）
        std::shared_ptr<glyphware::Face> primary = ctx.fonts.primary(style.font);
        if (!primary) primary = seg.face;
        Pt ascent = 0.0f, descent = 0.0f;
        font::faceAscentDescent(*primary, size, ascent, descent);
        const Pt baseline = baselineOffset(*primary, size, ctx.writingMode);

        // フェイクボールド／斜体（明示指定、またはフォントに該当スタイルが無いとき）
        const glyphware::FontDescriptor& desc = seg.face->descriptor();
        const bool fakeBold = style.fakeBold ||
            (style.font.weight >= 600 && font::effectiveWeight(*seg.face) < 600);
        const bool fakeItalic = style.fakeItalic ||
            (style.font.italic && desc.slant == glyphware::Slant::Normal);
        const Pt embolden = fakeBold ? dl::fakeBoldWidth(size) : 0.0f;
        const float skew = fakeItalic ? dl::kFakeItalicSkew : 0.0f;
        const Pt shift = style.baselineShift * size;    // 注記側が正

        hb_font_t* hbFont = ctx.fonts.hbFont(*seg.face);

        hb_buffer_clear_contents(buffer);
        hb_buffer_add_utf16(buffer, raw, static_cast<int>(text.size()),
                            static_cast<unsigned int>(seg.start),
                            static_cast<int>(seg.end - seg.start));
        hb_buffer_guess_segment_properties(buffer);
        // カラー絵文字フォントの正立セグメントは横方向でシェイプする（HarfBuzz は縦方向で ZWJ シーケンスや
        // 国旗の結合を作れない）。置くときに列の中心へ正立で置く
        const bool emojiUpright = vertical && seg.upright && seg.face->descriptor().color;
        const bool rtl = (seg.level & 1) != 0;
        hb_buffer_set_direction(buffer, (vertical && seg.upright && !emojiUpright) ? HB_DIRECTION_TTB
                                                                                   : (rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR));
        if (!style.language.empty()) {
            hb_buffer_set_language(buffer, hb_language_from_string(style.language.c_str(), -1));
        }
        std::vector<hb_feature_t> features;
        for (const std::string& f : style.features) {
            hb_feature_t ft;
            if (hb_feature_from_string(f.c_str(), -1, &ft)) features.push_back(ft);
        }
        hb_shape(hbFont, buffer, features.empty() ? nullptr : features.data(),
                 static_cast<unsigned int>(features.size()));

        unsigned int n = 0;
        const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer, &n);
        const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buffer, nullptr);
        if (n == 0) continue;

        const float rotation = (vertical && !seg.upright) ? kSidewaysRotation : 0.0f;
        const Mat2 xform = dl::glyphMatrix(rotation, style.scaleX, style.scaleY, skew);
        // 送り方向に効くスケール
        const float advScale = vertical ? (seg.upright ? style.scaleY : style.scaleX) : style.scaleX;
        const float blockScale = vertical ? (seg.upright ? style.scaleX : style.scaleY) : style.scaleY;

        // クラスタ（同じ cluster 値のグリフの並び）。RTL は HarfBuzz が視覚順（左から右）で返すので、
        // 論理順（後ろから）に並べ替えて置く。行を確定したあと、レベルで視覚順に並べ直す
        std::vector<std::pair<unsigned int, unsigned int>> groups;
        for (unsigned int i = 0; i < n;) {
            unsigned int j = i;
            while (j + 1 < n && info[j + 1].cluster == info[i].cluster) ++j;
            groups.emplace_back(i, j);
            i = j + 1;
        }
        if (rtl) std::reverse(groups.begin(), groups.end());
        for (const auto& grp : groups) {
            const unsigned int i = grp.first;
            const unsigned int j = grp.second;

            ShapedCluster sc;
            sc.glyphStart = static_cast<uint32_t>(result.glyphs.size());
            sc.charStart = info[i].cluster;
            if (rtl) sc.charEnd = (i > 0) ? info[i - 1].cluster : seg.end;
            else     sc.charEnd = (j + 1 < n) ? info[j + 1].cluster : seg.end;
            sc.origin = pen;
            sc.upright = seg.upright;
            sc.styleIndex = seg.styleIndex;
            sc.level = seg.level;
            sc.charClass = text::getCharClass(text::codePointAt(text, sc.charStart));

            Pt clusterAdvance = 0.0f;
            // 正立絵文字: クラスタ全体（合成された複数グリフ）を横に並べたまま列の中心へ置く
            Pt emojiWidth = 0.0f, emojiX = 0.0f, emojiAsc = 0.0f, emojiDesc = 0.0f;
            if (emojiUpright) {
                for (unsigned int k = i; k <= j; ++k) emojiWidth += pos[k].x_advance * s * advScale;
                font::faceAscentDescent(*seg.face, size, emojiAsc, emojiDesc);
            }
            for (unsigned int k = i; k <= j; ++k) {
                PlacedGlyph g;
                g.face = seg.face;
                g.gid = info[k].codepoint;
                g.charIndex = info[k].cluster;
                g.size = size;
                g.xform = xform;
                g.embolden = embolden;
                g.styleIndex = seg.styleIndex;

                const float xo = pos[k].x_offset * s;
                const float yo = pos[k].y_offset * s;
                Pt adv;
                if (emojiUpright) {
                    // 横方向のシェイプ結果を正立で置く: クラスタの幅の中心を列の中心に、ベースラインは上端＋アセント。
                    // 列方向の送りはクラスタで 1 回（アセント＋ディセント = 絵文字の高さ）
                    g.block = -emojiWidth * 0.5f + emojiX + xo * blockScale;
                    g.inline_ = pen + emojiAsc - yo * blockScale;
                    g.boxBefore = emojiAsc - yo * blockScale;
                    g.boxAfter = emojiDesc + yo * blockScale;
                    emojiX += pos[k].x_advance * s * advScale;
                    adv = (k == j) ? (emojiAsc + emojiDesc) : 0.0f;
                } else if (vertical && seg.upright) {
                    // TTB: HarfBuzz は縦原点を差し引いた offset を返すので、
                    // グリフは水平原点でペン位置に置けばよい。y は上向き正
                    g.block = xo * blockScale;
                    g.inline_ = pen - yo * advScale;
                    adv = -pos[k].y_advance * s * advScale;
                    g.boxBefore = -yo * advScale;
                    g.boxAfter = adv + yo * advScale;
                } else if (vertical) {
                    // 横倒し: 横組みで組んでから列へ 90 度倒す
                    //   ローカル (lx, ly[y-up]) → (block, inline) = (ly + baseline, lx)
                    g.block = yo * blockScale + baseline;
                    g.inline_ = pen + xo * advScale;
                    adv = pos[k].x_advance * s * advScale;
                    g.boxBefore = xo * advScale;
                    g.boxAfter = adv - xo * advScale;
                } else {
                    g.inline_ = pen + xo * advScale;
                    g.block = baseline - yo * blockScale;
                    adv = pos[k].x_advance * s * advScale;
                    g.boxBefore = xo * advScale;
                    g.boxAfter = adv - xo * advScale;
                }
                g.block += vertical ? shift : -shift;
                g.advance = adv;
                clusterAdvance += adv;
                pen += adv;
                result.glyphs.push_back(std::move(g));
            }
            sc.glyphCount = static_cast<uint32_t>(result.glyphs.size()) - sc.glyphStart;
            sc.advance = clusterAdvance;
            result.clusters.push_back(sc);
        }

        if (vertical && !seg.upright) {
            blockMin = std::min(blockMin, baseline - descent);
            blockMax = std::max(blockMax, baseline + ascent);
        } else {
            blockMin = std::min(blockMin, -size * 0.5f);
            blockMax = std::max(blockMax, size * 0.5f);
        }
        haveExtent = true;
    }

    hb_buffer_destroy(buffer);

    result.advance = pen;
    if (haveExtent) {
        result.blockMin = blockMin;
        result.blockMax = blockMax;
    }
    return result;
}

} // namespace typeset::inl
