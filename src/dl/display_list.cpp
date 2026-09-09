/**
 * display_list.cpp
 */

#include "typeset/dl/display_list.hpp"

#include <glyphware/Face.h>

namespace typeset::dl {

namespace {

void accumulate(const std::vector<Item>& items, const Matrix& ctm, Rect& acc) {
    auto addRect = [&](const Rect& r) {
        // 4 隅を変換して外接矩形を取る
        const Point c[4] = {ctm.apply({r.x, r.y}), ctm.apply({r.right(), r.y}),
                            ctm.apply({r.right(), r.bottom()}), ctm.apply({r.x, r.bottom()})};
        Pt x0 = c[0].x, y0 = c[0].y, x1 = x0, y1 = y0;
        for (const Point& p : c) {
            x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
        }
        acc = acc.unite(Rect{x0, y0, x1 - x0, y1 - y0});
    };

    for (const Item& item : items) {
        if (const auto* run = std::get_if<GlyphRun>(&item)) {
            for (const Glyph& g : run->glyphs) {
                // em box（ペン原点の上 1em × 幅 1em）で概算
                const Rect em{g.pos.x, g.pos.y - run->size, run->size, run->size};
                addRect(em);
            }
        } else if (const auto* path = std::get_if<PathItem>(&item)) {
            Rect b = path->path.controlBounds();
            if (path->stroke) {
                const Pt w = path->stroke->width;
                b = Rect{b.x - w, b.y - w, b.w + 2 * w, b.h + 2 * w};
            }
            addRect(b);
        } else if (const auto* rect = std::get_if<RectItem>(&item)) {
            addRect(rect->rect);
        } else if (const auto* img = std::get_if<ImageItem>(&item)) {
            if (img->image) {
                const Matrix m = multiply(ctm, img->xform);
                Rect acc2;
                Rect px{0, 0, static_cast<Pt>(img->image->width), static_cast<Pt>(img->image->height)};
                const Point c[4] = {m.apply({px.x, px.y}), m.apply({px.right(), px.y}),
                                    m.apply({px.right(), px.bottom()}), m.apply({px.x, px.bottom()})};
                Pt x0 = c[0].x, y0 = c[0].y, x1 = x0, y1 = y0;
                for (const Point& p : c) {
                    x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
                    x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
                }
                acc = acc.unite(Rect{x0, y0, x1 - x0, y1 - y0});
                (void)acc2;
            }
        } else if (const auto* group = std::get_if<Group>(&item)) {
            accumulate(group->children, multiply(ctm, group->xform), acc);
        }
    }
}

} // namespace

Rect runBounds(const GlyphRun& run) {
    if (run.glyphs.empty()) return Rect{};
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (const Glyph& g : run.glyphs) {
        // em box（ペン原点の上 1em × 幅 1em）で概算
        x0 = std::min(x0, g.pos.x);
        y0 = std::min(y0, g.pos.y - run.size);
        x1 = std::max(x1, g.pos.x + run.size);
        y1 = std::max(y1, g.pos.y);
    }
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

Rect bounds(const DisplayList& list) {
    Rect acc;
    accumulate(list.items, Matrix::identity(), acc);
    return acc;
}

} // namespace typeset::dl
