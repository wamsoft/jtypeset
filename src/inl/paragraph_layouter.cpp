/**
 * paragraph_layouter.cpp — 段落の組版と表示リストへの出力
 */

#include "typeset/inl/paragraph.hpp"

#include "color_glyph.hpp"

#include <algorithm>
#include <cmath>

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
        } else if (g.image) {
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

ParagraphFragment ParagraphLayouter::layout(const Paragraph& paraIn, WritingMode wm,
                                            const LineShapeProvider& shape,
                                            size_t charStart, int maxLines,
                                            int firstLineIndex) {
    const Paragraph para = paraIn.style.preserveSpaces ? expandTabs(paraIn) : paraIn;
    // 行内オブジェクトで行送りが広がると、後ろの行の実際の位置が「行番号 × 行送り」からずれる。
    // 排除領域（回り込み）を見る LineShapeProvider にそのずれを渡して組み直す（不動点まで、最大 3 回）
    std::vector<Pt> offsets;
    ParagraphFragment frag;
    for (int iter = 0; iter < 3; ++iter) {
        const OffsetLineShape shifted(shape, offsets, firstLineIndex);
        frag = layoutOnce(para, wm, shifted, charStart, maxLines, firstLineIndex);
        std::vector<Pt> next(frag.lines.size(), 0.0f);
        Pt acc = 0.0f;
        bool any = false;
        for (size_t i = 0; i < frag.lines.size(); ++i) {
            next[i] = acc + frag.lines[i].extraBefore;
            acc += frag.lines[i].extraBefore + frag.lines[i].extraAfter;
            if (next[i] != 0.0f) any = true;
        }
        if (!any && offsets.empty()) break;
        if (next == offsets) break;
        offsets = std::move(next);
    }
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
    }
    if (frag.styles.empty()) frag.styles.push_back(TextStyle{});
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

            ShapeContext sctx{fonts_, wm, para.style.orientation, &frag.styles, &images, &imageSizes, &objects};
            const ShapedText shaped = shapeText(sub, runs, sctx);

            ItemBuildContext ictx{fonts_, wm, para.style.orientation, &frag.styles, &base,
                                  base.letterSpacing, para.style.preserveSpaces};
            const std::vector<LineItem> items =
                buildLineItems(shaped, anns, para.style.spacing, ictx);

            const Pt indentPt = paraHead ? para.style.firstLineIndent * baseSize : 0.0f;
            const IndentedLineShape ishape(shape, lineIndex, indentPt);
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
                // 両端揃え以外の揃え
                if (!bo.justify) {
                    const Pt slack = br.shape.length - br.naturalWidth;
                    if (para.style.align == Align::End) line.indent += slack;
                    else if (para.style.align == Align::Center) line.indent += slack * 0.5f;
                }

                Pt v = 0.0f;
                // 行頭・行末のルビの掛かり抑制用: Box に付いた注記グリフ（ルビ・圏点）の範囲
                std::vector<std::pair<size_t, size_t>> attached;
                for (uint32_t i = br.itemStart; i < br.itemEnd; ++i) {
                    const LineItem& item = items[i];
                    if (item.isGlue()) {
                        v += item.natural +
                             (br.ratio >= 0.0f ? br.ratio * item.stretch : br.ratio * item.shrink);
                        continue;
                    }
                    if (!item.isBox()) continue;

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
                    line.charEnd = pos + cluster.charEnd;
                    v += item.width;
                }
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
                     WritingMode wm, Point lo, bool underline) {
    const bool vertical = isVertical(wm);
    size_t i = 0;
    while (i < line.glyphs.size()) {
        const PlacedGlyph& g0 = line.glyphs[i];
        if (g0.object || g0.image) { ++i; continue; }
        const size_t si = g0.styleIndex < frag.styles.size() ? g0.styleIndex : 0;
        const TextStyle& style = frag.styles[si];
        const std::optional<TextDecoration>& deco = underline ? style.underline : style.strikethrough;
        if (!deco) { ++i; continue; }
        size_t j = i + 1;
        while (j < line.glyphs.size() && !line.glyphs[j].object && !line.glyphs[j].image &&
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
                   Point origin, int lineOffset) {
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

        emitDecorations(out, frag, line, wm, lo, true);

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
                if (g.face && g.face->descriptor().color) {
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

        emitDecorations(out, frag, line, wm, lo, false);
    }
}

} // namespace typeset::inl
