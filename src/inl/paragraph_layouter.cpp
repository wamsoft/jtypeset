/**
 * paragraph_layouter.cpp — 段落の組版と表示リストへの出力
 */

#include "typeset/inl/paragraph.hpp"

#include <algorithm>

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

ParagraphFragment ParagraphLayouter::layout(const Paragraph& para, WritingMode wm,
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
                    for (PlacedGlyph glyph : item.glyphs) {
                        glyph.inline_ += v;
                        glyph.charIndex += static_cast<uint32_t>(pos);
                        line.glyphs.push_back(std::move(glyph));
                    }
                    line.blockMin = std::min(line.blockMin, item.extentMin);
                    line.blockMax = std::max(line.blockMax, item.extentMax);
                    line.charEnd = pos + cluster.charEnd;
                    v += item.width;
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

void emitParagraph(dl::DisplayList& out, const ParagraphFragment& frag, WritingMode wm,
                   Point origin, int lineOffset) {
    for (size_t li = 0; li < frag.lines.size(); ++li) {
        const LineBox& line = frag.lines[li];
        const Point lo = lineOriginAt(wm, origin,
                                      frag.linePitch * static_cast<float>(lineOffset) + frag.lineCenterOffset(li),
                                      line.indent);

        dl::GlyphRun run;
        bool open = false;
        auto flush = [&]() {
            if (open && !run.glyphs.empty()) out.add(run);
            run = dl::GlyphRun{};
            open = false;
        };

        for (const PlacedGlyph& g : line.glyphs) {
            if (g.object) {
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
            const TextStyle& style = (g.styleIndex < frag.styles.size())
                                         ? frag.styles[g.styleIndex] : frag.styles.front();
            const bool same = open && run.face == g.face && run.size == g.size &&
                              run.embolden == g.embolden && run.fill == style.fill &&
                              run.stroke.has_value() == style.stroke.has_value();
            if (!same) {
                flush();
                run.face = g.face;
                run.size = g.size;
                run.fill = style.fill;
                run.stroke = style.stroke;
                run.embolden = g.embolden;
                run.text = frag.text;
                open = true;
            }
            dl::Glyph dg;
            dg.gid = g.gid;
            dg.pos = toPhysical(wm, LogicalPoint{g.inline_, g.block}, lo);
            dg.xform = g.xform;
            dg.charIndex = g.charIndex;
            run.glyphs.push_back(dg);
        }
        flush();
    }
}

} // namespace typeset::inl
