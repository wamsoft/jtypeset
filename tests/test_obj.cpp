/**
 * test_obj.cpp — 外部オブジェクト（SVG 読み込み・ハンドラ登録・配置）
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <variant>

#include "typeset/backend/raster.hpp"
#include "typeset/block/block.hpp"
#include "typeset/font/font_set.hpp"
#include "typeset/inl/paragraph.hpp"
#include "typeset/inl/shaper.hpp"
#include "typeset/obj/object.hpp"
#include "typeset/obj/svg_import.hpp"
#include "typeset/page/flow_layouter.hpp"
#include "typeset/page/page.hpp"

using namespace typeset;

namespace {

Rect pathBounds(const std::vector<dl::Item>& items, const Matrix& ctm = Matrix{}) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    bool any = false;
    for (const dl::Item& item : items) {
        if (const auto* p = std::get_if<dl::PathItem>(&item)) {
            for (const Point& q : p->path.pts) {
                const Point t = ctm.apply(q);
                x0 = std::min(x0, t.x); y0 = std::min(y0, t.y);
                x1 = std::max(x1, t.x); y1 = std::max(y1, t.y);
                any = true;
            }
        } else if (const auto* g = std::get_if<dl::Group>(&item)) {
            const Rect r = pathBounds(g->children, multiply(ctm, g->xform));
            if (r.w > 0.0f || r.h > 0.0f) {
                x0 = std::min(x0, r.x); y0 = std::min(y0, r.y);
                x1 = std::max(x1, r.right()); y1 = std::max(y1, r.bottom());
                any = true;
            }
        }
    }
    if (!any) return Rect{};
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

int countPaths(const std::vector<dl::Item>& items) {
    int n = 0;
    for (const dl::Item& item : items) {
        if (std::get_if<dl::PathItem>(&item)) ++n;
        else if (const auto* g = std::get_if<dl::Group>(&item)) n += countPaths(g->children);
    }
    return n;
}

const char* kSvg = R"svg(<?xml version="1.0"?>
<!-- typeset baseline="30" -->
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"
     width="100px" height="40px" viewBox="0 0 200 80">
  <defs>
    <path id="dot" d="M0 0 h10 v10 h-10 z"/>
  </defs>
  <g fill="#ff0000" transform="translate(10,10)">
    <rect x="0" y="0" width="50" height="20" style="fill:#00ff00; stroke:none"/>
    <use xlink:href="#dot" x="100" y="0"/>
    <path d="M 0 40 C 20 20, 40 60, 60 40 S 100 20, 120 40 Q 140 60 160 40 T 180 40 A 5 5 0 0 1 190 40 Z" fill="none" stroke="blue" stroke-width="2"/>
    <circle cx="150" cy="60" r="5"/>
    <line x1="0" y1="70" x2="180" y2="70" stroke="black"/>
    <polygon points="0,60 10,70 0,70" fill="rgb(0,0,255)"/>
    <text x="0" y="0">ignored</text>
  </g>
</svg>)svg";

} // namespace

TEST_CASE("svg import: size, baseline, elements, defs/use, transform") {
    obj::ObjectResult res;
    obj::SvgImportOptions so;
    REQUIRE(obj::importSvg(kSvg, so, res));
    CHECK(res.ok());
    CHECK(res.size.w == doctest::Approx(75.0f));      // 100px = 75pt
    CHECK(res.size.h == doctest::Approx(30.0f));
    CHECK(res.hasBaseline);
    CHECK(res.baseline == doctest::Approx(30.0f));
    // rect, use→path, path, circle, line, polygon = 6 個。text は無視
    CHECK(countPaths(res.items) == 6);

    // viewBox 200×80 → 75×30pt: 倍率 0.375。translate(10,10) 後の rect (0,0,50,20) → (3.75, 3.75, 18.75, 7.5)
    const auto* rect = std::get_if<dl::PathItem>(&res.items[0]);
    REQUIRE(rect);
    REQUIRE(rect->fill.has_value());
    CHECK(rect->fill->g == 255);
    CHECK(rect->fill->r == 0);
    CHECK_FALSE(rect->stroke.has_value());
    const Rect rb = pathBounds({res.items[0]});
    CHECK(rb.x == doctest::Approx(3.75f));
    CHECK(rb.y == doctest::Approx(3.75f));
    CHECK(rb.w == doctest::Approx(18.75f));
    CHECK(rb.h == doctest::Approx(7.5f));

    // use: defs の path を (100,0) にずらして、親の fill（赤）を継承
    const auto* used = std::get_if<dl::PathItem>(&res.items[1]);
    REQUIRE(used);
    REQUIRE(used->fill.has_value());
    CHECK(used->fill->r == 255);
    const Rect ub = pathBounds({res.items[1]});
    CHECK(ub.x == doctest::Approx((10 + 100) * 0.375f));
    CHECK(ub.w == doctest::Approx(10 * 0.375f));

    // 曲線: fill none, stroke blue, 線幅は倍率が掛かる
    const auto* curve = std::get_if<dl::PathItem>(&res.items[2]);
    REQUIRE(curve);
    CHECK_FALSE(curve->fill.has_value());
    REQUIRE(curve->stroke.has_value());
    CHECK(curve->stroke->color.b == 255);
    CHECK(curve->stroke->width == doctest::Approx(2.0f * 0.375f));

    // 全体が箱の中
    const Rect all = pathBounds(res.items);
    CHECK(all.x >= -0.01f);
    CHECK(all.y >= -0.01f);
    CHECK(all.right() <= res.size.w + 0.01f);
    CHECK(all.bottom() <= res.size.h + 0.01f);
}

TEST_CASE("svg import: MathJax-style ex units and vertical-align") {
    const char* svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="4ex" height="2ex" viewBox="0 -500 2000 1000"
        style="vertical-align: -0.5ex;"><path d="M0 0h2000v-500h-2000z"/></svg>)";
    obj::ObjectResult res;
    obj::SvgImportOptions so;
    so.fontSize = 10.0f;
    so.exRatio = 0.5f;
    REQUIRE(obj::importSvg(svg, so, res));
    CHECK(res.size.w == doctest::Approx(20.0f));   // 4ex × 5pt
    CHECK(res.size.h == doctest::Approx(10.0f));
    CHECK(res.hasBaseline);
    CHECK(res.baseline == doctest::Approx(10.0f - 2.5f));   // 下端から 0.5ex 上
    const Rect b = pathBounds(res.items);
    CHECK(b.y == doctest::Approx(0.0f));
    CHECK(b.h == doctest::Approx(5.0f));
}

TEST_CASE("svg import: rejects non-svg") {
    obj::ObjectResult res;
    CHECK_FALSE(obj::importSvg("<html></html>", {}, res));
    CHECK_FALSE(res.error.empty());
}

TEST_CASE("object registry: function handler, cache, missing handler") {
    obj::ObjectRegistry reg;
    int calls = 0;
    reg.add("box", [&](const obj::ObjectRequest& req) {
        ++calls;
        Path p;
        p.addRect(Rect{0, 0, req.fontSize * 2, req.fontSize});
        dl::PathItem item;
        item.path = p;
        item.fill = Color::rgb(0, 0, 0);
        return obj::makeResult(Size{req.fontSize * 2, req.fontSize}, req.fontSize * 0.8f, {item});
    });
    obj::ObjectRequest req;
    req.handler = "box";
    req.source = u"x";
    req.fontSize = 10.0f;
    auto a = reg.render(req);
    auto b = reg.render(req);
    CHECK(a.get() == b.get());
    CHECK(calls == 1);
    CHECK(a->ok());
    CHECK(a->size.w == doctest::Approx(20.0f));
    req.fontSize = 12.0f;
    reg.render(req);
    CHECK(calls == 2);
    CHECK(reg.cacheSize() == 2);

    req.handler = "nope";
    auto c = reg.render(req);
    CHECK_FALSE(c->ok());
    CHECK(reg.errors().size() == 1);

    const std::string json = obj::requestToJson(req);
    CHECK(json.find("\"handler\":\"nope\"") != std::string::npos);
    CHECK(json.find("\"fontSize\":12.000") != std::string::npos);
}

TEST_CASE("objects in flow: inline baseline alignment and block placement with equation number") {
    font::FontSet fonts;
    auto jp = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
    if (!jp) { MESSAGE("fonts not found; skipping"); return; }
    TextStyle body;
    body.font.family = {"serif-ja"};
    body.size = 10.0f;

    obj::ObjectRegistry reg;
    // 高さ 20pt、ベースラインは上から 15pt の箱
    reg.add("box", [](const obj::ObjectRequest& req) {
        Path p;
        p.addRect(Rect{0, 0, 30, 20});
        dl::PathItem item;
        item.path = p;
        item.fill = Color::rgb(0, 0, 0);
        obj::ObjectResult r = obj::makeResult(Size{30, 20}, 15.0f, {item});
        if (req.params.count("wide")) {
            r.size = Size{1000, 20};
            Path q; q.addRect(Rect{0, 0, 1000, 20});
            std::get<dl::PathItem>(r.items[0]).path = q;
        }
        return r;
    });

    page::PageSequence seq;
    seq.master.size = Size{300, 300};
    seq.master.margin = page::Margins{20, 20, 20, 20};
    seq.master.writingMode = WritingMode::HorizontalTb;

    block::Flow flow;
    inl::Paragraph p;
    p.runs.push_back(inl::InlineRun{u"式", body});
    p.addObject("box", u"a", {}, body);
    p.runs.push_back(inl::InlineRun{u"の後。", body});
    flow.addParagraph(p);

    block::BlockStyle lbl;
    lbl.label = "eq-a";
    flow.addObject("box", u"E=mc^2", body, {}, true, lbl);
    flow.addObject("box", u"wide", body, {{"wide", "1"}}, true);
    flow.addParagraph(inl::Paragraph::plain(u"式 {ref:eq-a} を見よ。", body));
    flow.addObject("missing", u"x", body);   // 未登録 → 代替テキスト

    page::FlowLayoutOptions opts;
    opts.objects = &reg;
    page::FlowLayouter layouter(fonts);
    const auto pages = layouter.layout(flow, seq, opts);
    REQUIRE(pages.size() == 1);
    const dl::DisplayList& l = pages[0].dl;

    // 行内: 文字のベースラインとオブジェクトのベースライン（上端 + 15）が一致する
    std::optional<float> textBaselineY;
    std::vector<Rect> groups;
    for (const dl::Item& item : l.items) {
        if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
            if (!textBaselineY && !run->glyphs.empty()) textBaselineY = run->glyphs.front().pos.y;
        } else if (const auto* g = std::get_if<dl::Group>(&item)) {
            groups.push_back(pathBounds(g->children, g->xform));
        }
    }
    REQUIRE(textBaselineY.has_value());
    REQUIRE(groups.size() == 3);
    CHECK(groups[0].y + 15.0f == doctest::Approx(*textBaselineY).epsilon(0.01));
    CHECK(groups[0].h == doctest::Approx(20.0f));

    // 別行立て 1: 中央揃え
    const Rect body1 = seq.master.bodyRect(1);
    CHECK(groups[1].x + groups[1].w * 0.5f == doctest::Approx(body1.x + body1.w * 0.5f).epsilon(0.01));
    CHECK(groups[1].w == doctest::Approx(30.0f));
    // 別行立て 2: 幅 1000 は段に収まるよう縮む（番号ぶんを空けて）
    CHECK(groups[2].w < body1.w);
    CHECK(groups[2].w > body1.w * 0.5f);
    CHECK(groups[2].h < 20.0f);

    // 式番号 (1) (2) と参照 "式 (1)" → '(' が 3 つ以上、'1' が 2 つ以上
    auto count = [&](char32_t c) {
        const uint32_t gid = jp->glyphIndex(c);
        int n = 0;
        for (const dl::Item& item : l.items) {
            if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
                for (const dl::Glyph& g : run->glyphs) if (g.gid == gid) ++n;
            }
        }
        return n;
    };
    CHECK(count(U'(') >= 3);
    CHECK(count(U'1') >= 2);
    CHECK(count(U'2') >= 1);
    // 代替テキスト "[missing: no handler]" の '[' がある
    CHECK(count(U'[') >= 1);
    CHECK(reg.errors().size() >= 1);
}

TEST_CASE("objects in flow: vertical text places the object sideways") {
    font::FontSet fonts;
    auto jp = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
    if (!jp) { MESSAGE("fonts not found; skipping"); return; }
    TextStyle body;
    body.font.family = {"serif-ja"};
    body.size = 10.0f;

    obj::ObjectRegistry reg;
    reg.add("box", [](const obj::ObjectRequest&) {
        Path p;
        p.addRect(Rect{0, 0, 40, 10});
        dl::PathItem item;
        item.path = p;
        item.fill = Color::rgb(0, 0, 0);
        return obj::makeResult(Size{40, 10}, 8.0f, {item});
    });
    page::PageSequence seq;
    seq.master.size = Size{300, 300};
    seq.master.margin = page::Margins{20, 20, 20, 20};
    seq.master.writingMode = WritingMode::VerticalRl;

    block::Flow flow;
    inl::Paragraph p;
    p.runs.push_back(inl::InlineRun{u"式", body});
    p.addObject("box", u"a", {}, body);
    p.runs.push_back(inl::InlineRun{u"の後。", body});
    flow.addParagraph(p);
    flow.addObject("box", u"b", body);

    page::FlowLayoutOptions opts;
    opts.objects = &reg;
    page::FlowLayouter layouter(fonts);
    const auto pages = layouter.layout(flow, seq, opts);
    REQUIRE(pages.size() == 1);
    std::vector<Rect> groups;
    for (const dl::Item& item : pages[0].dl.items) {
        if (const auto* g = std::get_if<dl::Group>(&item)) groups.push_back(pathBounds(g->children, g->xform));
    }
    REQUIRE(groups.size() == 2);
    // 横倒し: 物理では幅 10・高さ 40
    CHECK(groups[0].w == doctest::Approx(10.0f));
    CHECK(groups[0].h == doctest::Approx(40.0f));
    CHECK(groups[1].w == doctest::Approx(10.0f));
    CHECK(groups[1].h == doctest::Approx(40.0f));
    // 行内のものは 1 行目（右端の行）の中心に乗る: 版面右端から 行送りの半分
    const Rect body1 = seq.master.bodyRect(1);
    CHECK(groups[0].x + groups[0].w * 0.5f < body1.right());
    CHECK(groups[0].x + groups[0].w * 0.5f > body1.right() - 20.0f);
}

TEST_CASE("objects in flow: a tall inline object widens the line pitch instead of overlapping") {
    font::FontSet fonts;
    auto jp = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
    if (!jp) { MESSAGE("fonts not found; skipping"); return; }
    TextStyle body;
    body.font.family = {"serif-ja"};
    body.size = 10.0f;

    obj::ObjectRegistry reg;
    reg.add("tall", [](const obj::ObjectRequest&) {
        Path p;
        p.addRect(Rect{0, 0, 20, 40});
        dl::PathItem item;
        item.path = p;
        item.fill = Color::rgb(0, 0, 0);
        return obj::makeResult(Size{20, 40}, 30.0f, {item});   // 上 30 / 下 10
    });
    page::PageSequence seq;
    seq.master.size = Size{300, 400};
    seq.master.margin = page::Margins{20, 20, 20, 20};
    seq.master.writingMode = WritingMode::HorizontalTb;

    block::Flow flow;
    inl::Paragraph p;
    p.runs.push_back(inl::InlineRun{u"前の行の文章がここにあって、", body});
    p.addObject("tall", u"x", {}, body);
    std::u16string rest;
    for (int i = 0; i < 6; ++i) rest += u"後ろの文章が続く。";
    p.runs.push_back(inl::InlineRun{rest, body});
    flow.addParagraph(p);
    flow.addParagraph(inl::Paragraph::plain(u"次の段落。", body));

    page::FlowLayoutOptions opts;
    opts.objects = &reg;
    page::FlowLayouter layouter(fonts);
    const auto pages = layouter.layout(flow, seq, opts);
    REQUIRE(pages.size() == 1);

    // オブジェクトの箱と、各行のベースライン y を集める
    std::optional<Rect> box;
    std::vector<float> baselines;
    for (const dl::Item& item : pages[0].dl.items) {
        if (const auto* g = std::get_if<dl::Group>(&item)) {
            box = pathBounds(g->children, g->xform);
        } else if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
            for (const dl::Glyph& gl : run->glyphs) {
                bool found = false;
                for (float y : baselines) if (std::fabs(y - gl.pos.y) < 0.5f) { found = true; break; }
                if (!found) baselines.push_back(gl.pos.y);
            }
        }
    }
    REQUIRE(box.has_value());
    REQUIRE(baselines.size() >= 3);
    std::sort(baselines.begin(), baselines.end());
    // 1 行目のベースラインはオブジェクトの上端 + 30
    CHECK(baselines[0] == doctest::Approx(box->y + 30.0f).epsilon(0.01));
    // 2 行目の文字（上端 ≒ ベースライン − 0.88em）はオブジェクトの下端より下
    CHECK(baselines[1] - body.size * 0.9f >= box->bottom() - 0.5f);
    // 行送りが広がったのは 1 行目の周りだけ: 2 行目→3 行目は通常の行送り
    const float normal = baselines[2] - baselines[1];
    CHECK(baselines[1] - baselines[0] > normal + 5.0f);
    // 1 行目の上にも広がる: 版面上端から 1 行目のベースラインまでが 30 以上
    const Rect bodyRect = seq.master.bodyRect(1);
    CHECK(baselines[0] - bodyRect.y >= 30.0f - 0.5f);
}
