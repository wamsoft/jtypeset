#include <doctest/doctest.h>

#include <memory>

#include "typeset/block/block.hpp"
#include "typeset/font/font_set.hpp"
#include "typeset/image/image.hpp"
#include "typeset/page/flow_layouter.hpp"

using namespace typeset;

namespace {

struct Fixture {
    font::FontSet fonts;
    std::shared_ptr<glyphware::Face> jp;
    Fixture() { jp = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja"); }
    bool ok() const { return static_cast<bool>(jp); }
    TextStyle style(Pt size = 10.0f) const {
        TextStyle s;
        s.font.family = {"serif-ja"};
        s.size = size;
        return s;
    }
};

std::shared_ptr<dl::Image> solidImage(int w, int h) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 200);
    return image::fromRgba(w, h, px.data());
}

int countItems(const dl::DisplayList& list, int kind) {
    int n = 0;
    for (const dl::Item& item : list.items) {
        if (kind == 0 && std::get_if<dl::ImageItem>(&item)) ++n;
        if (kind == 1 && std::get_if<dl::RectItem>(&item)) ++n;
        if (kind == 2 && std::get_if<dl::GlyphRun>(&item)) ++n;
    }
    return n;
}

} // namespace

TEST_CASE("Region: freeInlineRange avoids exclusions (horizontal and vertical)") {
    page::Region r;
    r.area = Rect{100, 100, 200, 300};
    r.writingMode = WritingMode::HorizontalTb;
    // 左上に 80×50 の排除領域
    r.exclusions.push_back(Rect{100, 100, 80, 50});
    Pt s, e;
    r.freeInlineRange(0, 10, s, e);
    CHECK(s == doctest::Approx(80));
    CHECK(e == doctest::Approx(200));
    r.freeInlineRange(60, 70, s, e);   // 排除領域の下
    CHECK(s == doctest::Approx(0));
    CHECK(e == doctest::Approx(200));
    // 右下の排除領域
    r.exclusions = {Rect{250, 300, 50, 100}};
    r.freeInlineRange(220, 230, s, e);
    CHECK(s == doctest::Approx(0));
    CHECK(e == doctest::Approx(150));

    page::Region v;
    v.area = Rect{100, 100, 200, 300};
    v.writingMode = WritingMode::VerticalRl;
    // 右上（1 列目の上）に 60×80
    v.exclusions.push_back(Rect{240, 100, 60, 80});
    v.freeInlineRange(0, 15, s, e);    // 右端の列
    CHECK(s == doctest::Approx(80));
    CHECK(e == doctest::Approx(300));
    v.freeInlineRange(70, 85, s, e);   // 排除領域より左の列
    CHECK(s == doctest::Approx(0));
    CHECK(e == doctest::Approx(300));

    page::RegionLineShape shape(v, 0.0f, 15.0f, 5.0f);
    const inl::LineShape l0 = shape.at(0);
    CHECK(l0.indent == doctest::Approx(85));
    CHECK(l0.length == doctest::Approx(215));
    const inl::LineShape l6 = shape.at(6);
    CHECK(l6.indent == doctest::Approx(5));
}

TEST_CASE("FlowLayouter: float image shortens the lines beside it") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{400, 400};
    seq.master.margin = page::Margins{40, 40, 40, 40};
    seq.master.writingMode = WritingMode::HorizontalTb;

    block::Flow flow;
    block::ImageBlock img;
    img.image = solidImage(100, 100);
    img.size = Size{100, 100};
    img.placement = block::ImagePlacement::FloatStart;
    img.gap = 10;
    flow.addImage(img);

    // 画像の横は 210pt（21 字）× 8 行 ≈ 168 字なので、300 字あれば画像の下まで届く
    std::u16string t;
    for (int i = 0; i < 30; ++i) t += u"あいうえおかきくけこ";
    inl::Paragraph p = inl::Paragraph::plain(t, fx.style(10.0f));
    p.style.lineHeight = 1.5f;
    p.style.firstLineIndent = 0.0f;
    flow.addParagraph(p);

    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() == 1);
    CHECK(countItems(pages[0].dl, 0) == 1);

    const Rect body = seq.master.bodyRect(1);
    // 画像の右（x ≥ 40+100+10）から始まる行と、画像の下で左端から始まる行の両方がある
    bool besideImage = false, belowImage = false;
    for (const dl::Item& item : pages[0].dl.items) {
        const auto* run = std::get_if<dl::GlyphRun>(&item);
        if (!run) continue;
        for (const dl::Glyph& g : run->glyphs) {
            if (g.pos.y < body.y + 110) {
                CHECK(g.pos.x >= body.x + 110 - 0.01f);
                besideImage = true;
            } else if (g.pos.x < body.x + 5) {
                belowImage = true;
            }
        }
    }
    CHECK(besideImage);
    CHECK(belowImage);
}

TEST_CASE("FlowLayouter: table rows, rules and header repeat across pages") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{300, 200};
    seq.master.margin = page::Margins{20, 20, 20, 20};
    seq.master.writingMode = WritingMode::HorizontalTb;

    block::TableBlock t;
    t.columns = {block::TableColumn{60.0f}, block::TableColumn{0.0f}};
    auto cell = [&](const char16_t* s) {
        block::TableCell c;
        inl::Paragraph p = inl::Paragraph::plain(s, fx.style(9.0f));
        p.style.lineHeight = 1.4f;
        c.paras.push_back(p);
        return c;
    };
    block::TableRow head;
    head.header = true;
    head.cells = {cell(u"項目"), cell(u"説明")};
    t.rows.push_back(head);
    for (int i = 0; i < 14; ++i) {
        block::TableRow row;
        row.cells = {cell(u"行"), cell(u"説明の文。説明の文。")};
        t.rows.push_back(row);
    }
    block::Flow flow;
    flow.addTable(t);

    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() >= 2);

    // 版面 160pt / 行 (9*1.4 + 6 = 18.6pt) → 1 ページ 8 行前後。ヘッダはページごとに出る
    const uint32_t gidKou = fx.jp->glyphIndex(U'項');
    for (const page::Page& pg : pages) {
        int headers = 0;
        for (const dl::Item& item : pg.dl.items) {
            if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
                for (const dl::Glyph& g : run->glyphs) if (g.gid == gidKou) ++headers;
            }
        }
        CHECK(headers == 1);
        CHECK(countItems(pg.dl, 1) > 4);   // 罫線（矩形）がある
        const Rect body = seq.master.bodyRect(pg.number);
        for (const dl::Item& item : pg.dl.items) {
            if (const auto* r = std::get_if<dl::RectItem>(&item)) {
                CHECK(r->rect.y >= body.y - 1.0f);
                CHECK(r->rect.bottom() <= body.bottom() + 1.0f);
            }
        }
    }
}

TEST_CASE("FlowLayouter: rule block emits a thin rect after the paragraph") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{300, 300};
    seq.master.margin = page::Margins{30, 30, 30, 30};
    seq.master.writingMode = WritingMode::HorizontalTb;

    block::Flow flow;
    flow.addParagraph(inl::Paragraph::plain(u"段落。", fx.style(10.0f)));
    flow.addRule(0.75f, Color::rgb(0, 0, 0));
    flow.add(block::SectionBlock{2, 10.0f});
    flow.addParagraph(inl::Paragraph::plain(u"二段組。", fx.style(10.0f)));

    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() == 2);
    int rules = 0;
    for (const dl::Item& item : pages[0].dl.items) {
        if (const auto* r = std::get_if<dl::RectItem>(&item)) {
            if (r->rect.h > 0.5f && r->rect.h < 1.0f) {
                ++rules;
                CHECK(r->rect.x == doctest::Approx(30));
                CHECK(r->rect.w == doctest::Approx(240));
                CHECK(r->rect.y > 30 + 10);
            }
        }
    }
    CHECK(rules == 1);
}
