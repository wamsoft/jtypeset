#include <doctest/doctest.h>

#include <cmath>
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

bool glyphsWithin(const dl::DisplayList& list, const Rect& rect, Pt slack) {
    for (const dl::Item& item : list.items) {
        if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
            for (const dl::Glyph& g : run->glyphs) {
                if (g.pos.x < rect.x - slack || g.pos.x > rect.right() + slack ||
                    g.pos.y < rect.y - slack || g.pos.y > rect.bottom() + slack) {
                    return false;
                }
            }
        }
    }
    return true;
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

TEST_CASE("FlowLayouter: table rowspan spans rows and keeps them together") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{300, 300};
    seq.master.margin = page::Margins{20, 20, 20, 20};
    seq.master.writingMode = WritingMode::HorizontalTb;

    auto cell = [&](const char16_t* s, int rowspan) {
        block::TableCell c;
        inl::Paragraph p = inl::Paragraph::plain(s, fx.style(9.0f));
        p.style.lineHeight = 1.4f;
        c.paras.push_back(p);
        c.rowspan = rowspan;
        return c;
    };
    block::TableBlock t;
    t.columns = {block::TableColumn{60.0f}, block::TableColumn{0.0f}};
    block::TableRow r1; r1.cells = {cell(u"甲", 2), cell(u"一", 1)};
    block::TableRow r2; r2.cells = {cell(u"二", 1)};
    block::TableRow r3; r3.cells = {cell(u"乙", 1), cell(u"三", 1)};
    t.rows = {r1, r2, r3};
    block::Flow flow;
    flow.addTable(t);

    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() == 1);

    // 「甲」は 1 行目の位置、「二」は 2 行目、「乙」は 3 行目にある
    const uint32_t gKou = fx.jp->glyphIndex(U'甲'), gNi = fx.jp->glyphIndex(U'二'), gOtsu = fx.jp->glyphIndex(U'乙');
    float yKou = 0, yNi = 0, yOtsu = 0;
    for (const dl::Item& item : pages[0].dl.items) {
        if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
            for (const dl::Glyph& g : run->glyphs) {
                if (g.gid == gKou) yKou = g.pos.y;
                if (g.gid == gNi) yNi = g.pos.y;
                if (g.gid == gOtsu) yOtsu = g.pos.y;
            }
        }
    }
    CHECK(yKou < yNi);
    CHECK(yNi < yOtsu);
    // 横罫: 1 行目と 2 行目の境は右の列だけ（左は甲がまたぐ）→ 幅 60 未満の横罫は無く、
    // 右列だけの罫（幅 ≈ 200）が 1 本ある
    int rightOnly = 0;
    const Rect body = seq.master.bodyRect(1);
    for (const dl::Item& item : pages[0].dl.items) {
        if (const auto* r = std::get_if<dl::RectItem>(&item)) {
            if (r->rect.h < 1.0f && r->rect.x > body.x + 50.0f && r->rect.w > 150.0f) ++rightOnly;
        }
    }
    CHECK(rightOnly == 1);
}

TEST_CASE("FlowLayouter: spanning block mid-page balances the columns above it") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{400, 400};
    seq.master.margin = page::Margins{40, 40, 40, 40};
    seq.master.writingMode = WritingMode::HorizontalTb;
    seq.master.columns = 2;
    seq.master.columnGap = 20;

    block::Flow flow;
    // 6 行分（1 段 150pt 幅 = 15 字/行）の本文 → 1 段 20 行あるので段 0 に全部入る
    std::u16string t;
    for (int i = 0; i < 9; ++i) t += u"あいうえおかきくけこ";
    inl::Paragraph p = inl::Paragraph::plain(t, fx.style(10.0f));
    p.style.lineHeight = 1.5f;
    p.style.firstLineIndent = 0.0f;
    flow.addParagraph(p);

    inl::Paragraph h = inl::Paragraph::plain(u"段抜き見出し", fx.style(12.0f));
    h.style.lineHeight = 1.5f;
    h.style.align = Align::Start;
    block::BlockStyle hs;
    hs.spanColumns = true;
    flow.addHeading(h, 1, hs);
    flow.addParagraph(p);

    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() == 1);

    const Rect body = seq.master.bodyRect(1);
    const uint32_t gDan = fx.jp->glyphIndex(U'段');
    float headingY = -1;
    float leftMaxAbove = 0, rightMaxAbove = 0;    // 見出しより上の、左段・右段の最下行
    int leftAbove = 0, rightAbove = 0;
    for (const dl::Item& item : pages[0].dl.items) {
        if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
            for (const dl::Glyph& g : run->glyphs) if (g.gid == gDan) headingY = g.pos.y;
        }
    }
    REQUIRE(headingY > body.y);
    for (const dl::Item& item : pages[0].dl.items) {
        if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
            for (const dl::Glyph& g : run->glyphs) {
                if (g.pos.y >= headingY - 1.0f) continue;
                if (g.pos.x < body.x + body.w * 0.5f) { ++leftAbove; leftMaxAbove = std::max(leftMaxAbove, g.pos.y); }
                else { ++rightAbove; rightMaxAbove = std::max(rightMaxAbove, g.pos.y); }
            }
        }
    }
    // 見出しの上に両段とも内容があり（バランス取り）、高さの差は 1 行以内
    CHECK(leftAbove > 0);
    CHECK(rightAbove > 0);
    CHECK(std::fabs(leftMaxAbove - rightMaxAbove) <= 15.0f + 0.5f);
    // 見出しは左段の左端から始まる（全幅）
    bool headingAtLeft = false;
    for (const dl::Item& item : pages[0].dl.items) {
        if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
            for (const dl::Glyph& g : run->glyphs) if (g.gid == gDan && g.pos.x < body.x + 1.0f) headingAtLeft = true;
        }
    }
    CHECK(headingAtLeft);
}

TEST_CASE("FlowLayouter: numbered headings, cross references and TOC resolve over passes") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{400, 300};
    seq.master.margin = page::Margins{30, 30, 30, 30};
    seq.master.writingMode = WritingMode::HorizontalTb;

    block::Flow flow;
    block::TocBlock toc;
    toc.style = fx.style(10.0f);
    flow.addToc(toc);
    flow.addPageBreak();
    block::BlockStyle hs;
    hs.label = "sec-a";
    flow.addHeading(inl::Paragraph::plain(u"最初の章", fx.style(14.0f)), 1, hs, true);
    flow.addParagraph(inl::Paragraph::plain(u"第 {ref:sec-b} 章は {page:sec-b} ページ。図 {ref:fig-x} を見よ。", fx.style(10.0f)));
    block::BlockStyle hs2;
    hs2.label = "sec-b";
    hs2.breakBefore = block::BreakKind::Page;
    flow.addHeading(inl::Paragraph::plain(u"次の章", fx.style(14.0f)), 1, hs2, true);
    flow.addHeading(inl::Paragraph::plain(u"節", fx.style(12.0f)), 2, std::nullopt, true);
    block::ImageBlock img;
    img.image = solidImage(40, 40);
    img.size = Size{40, 40};
    img.caption = inl::Paragraph::plain(u"{fig}　絵", fx.style(8.0f));
    img.block.label = "fig-x";
    flow.addImage(img);

    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() == 3);

    // 目次（1 ページ目）に見出し 3 つ分のページ番号「2」「3」「3」が出る: 数字のグリフを数える
    auto countGid = [&](const dl::DisplayList& l, char32_t c) {
        const uint32_t gid = fx.jp->glyphIndex(c);
        int n = 0;
        for (const dl::Item& item : l.items) {
            if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
                for (const dl::Glyph& g : run->glyphs) if (g.gid == gid) ++n;
            }
        }
        return n;
    };
    CHECK(countGid(pages[0].dl, U'次') >= 1);      // 「次の章」
    CHECK(countGid(pages[0].dl, U'3') >= 2);       // ページ番号 3 が 2 回（次の章・節）
    // 本文の参照が解決している: 「第 2 章は 3 ページ。図 1 を見よ。」→ '2' '3' '1' がある
    CHECK(countGid(pages[1].dl, U'2') >= 1);
    CHECK(countGid(pages[1].dl, U'3') >= 1);
    // しおりが 3 つ
    int bookmarks = 0;
    for (const page::Page& pg : pages) {
        for (const dl::Item& item : pg.dl.items) if (std::get_if<dl::Bookmark>(&item)) ++bookmarks;
    }
    CHECK(bookmarks == 3);
}

TEST_CASE("FlowLayouter: a table row taller than the page is split across pages") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{300, 200};
    seq.master.margin = page::Margins{20, 20, 20, 20};
    seq.master.writingMode = WritingMode::HorizontalTb;

    block::TableBlock t;
    t.columns = {block::TableColumn{50.0f}, block::TableColumn{0.0f}};
    std::u16string longText;
    for (int i = 0; i < 40; ++i) longText += u"長いセルの文章。";
    auto cell = [&](std::u16string s) {
        block::TableCell c;
        inl::Paragraph p = inl::Paragraph::plain(std::move(s), fx.style(9.0f));
        p.style.lineHeight = 1.4f;
        c.paras.push_back(p);
        return c;
    };
    block::TableRow head;
    head.header = true;
    head.cells = {cell(u"項目"), cell(u"説明")};
    block::TableRow r1;
    r1.cells = {cell(u"長い"), cell(longText)};
    block::TableRow r2;
    r2.cells = {cell(u"短い"), cell(u"おわり。")};
    t.rows = {head, r1, r2};
    block::Flow flow;
    flow.addTable(t);

    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() >= 2);
    // 長いセルの文字が複数ページに分かれ、全部合わせると元の文字数になる（ヘッダ・他セル分を除く）
    const uint32_t gNaga = fx.jp->glyphIndex(U'長');
    int total = 0;
    for (const page::Page& pg : pages) {
        for (const dl::Item& item : pg.dl.items) {
            if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
                for (const dl::Glyph& g : run->glyphs) if (g.gid == gNaga) ++total;
            }
        }
    }
    CHECK(total == 40 + 1);   // 本文 40 回 ＋ セル「長い」
    // 各ページのグリフが版面内
    for (const page::Page& pg : pages) {
        CHECK(glyphsWithin(pg.dl, seq.master.bodyRect(pg.number), 12.0f));
    }
}

TEST_CASE("FlowLayouter: list markers, code background and inline image") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{300, 300};
    seq.master.margin = page::Margins{30, 30, 30, 30};
    seq.master.writingMode = WritingMode::HorizontalTb;

    block::Flow flow;
    block::ListBlock list;
    list.marker = block::ListBlock::Marker::Numbered;
    list.items.push_back(inl::Paragraph::plain(u"一つ目", fx.style(10.0f)));
    list.items.push_back(inl::Paragraph::plain(u"二つ目", fx.style(10.0f)));
    flow.addList(list);

    inl::Paragraph code = inl::Paragraph::plain(u"  indented\nline", fx.style(9.0f));
    code.style.preserveSpaces = true;
    code.style.align = Align::Start;
    block::BlockStyle cs;
    cs.background = Color::rgb(230, 230, 230);
    cs.padding = 4.0f;
    flow.addParagraph(code, cs);

    inl::Paragraph withImage;
    withImage.runs.push_back(inl::InlineRun{u"前", fx.style(10.0f)});
    withImage.addImage(solidImage(10, 10), Size{12, 12}, fx.style(10.0f));
    withImage.runs.push_back(inl::InlineRun{u"後", fx.style(10.0f)});
    flow.addParagraph(withImage);

    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() == 1);
    const dl::DisplayList& l = pages[0].dl;
    // 番号 "1." "2." のグリフ
    const uint32_t g1 = fx.jp->glyphIndex(U'1'), g2 = fx.jp->glyphIndex(U'2');
    int n1 = 0, n2 = 0;
    for (const dl::Item& item : l.items) {
        if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
            for (const dl::Glyph& g : run->glyphs) { if (g.gid == g1) ++n1; if (g.gid == g2) ++n2; }
        }
    }
    CHECK(n1 == 1);
    CHECK(n2 == 1);
    // 背景の矩形と行内画像
    CHECK(countItems(l, 1) >= 1);
    CHECK(countItems(l, 0) == 1);
    // 空白保持: "  indented" の先頭 i は行頭より右にある
    const uint32_t gi = fx.jp->glyphIndex(U'i');
    float xi = -1;
    for (const dl::Item& item : l.items) {
        if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
            for (const dl::Glyph& g : run->glyphs) if (g.gid == gi && xi < 0) xi = g.pos.x;
        }
    }
    CHECK(xi > 30.0f + 3.0f);
}

TEST_CASE("FlowLayouter: footnotes go to the bottom of the column with superscript markers") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{300, 320};
    seq.master.margin = page::Margins{20, 20, 20, 20};
    seq.master.writingMode = WritingMode::HorizontalTb;

    const TextStyle body = fx.style(10.0f);
    const TextStyle noteStyle = fx.style(7.0f);
    const TextStyle marker = inl::superscriptStyle(body);

    block::Flow flow;
    inl::Paragraph p;
    p.runs.push_back(inl::InlineRun{u"脚注のある文章", body});
    p.addFootnote(inl::Paragraph::plain(u"一つ目の注。", noteStyle), marker);
    p.runs.push_back(inl::InlineRun{u"と、もう一つ", body});
    p.addFootnote(inl::Paragraph::plain(u"二つ目の注は少し長くて、二行になるかもしれない文章を書いておく。", noteStyle), marker);
    p.runs.push_back(inl::InlineRun{u"。", body});
    flow.addParagraph(p);
    for (int i = 0; i < 3; ++i) flow.addParagraph(inl::Paragraph::plain(u"本文が続く。本文が続く。本文が続く。", body));

    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() == 1);
    const Rect bodyRect = seq.master.bodyRect(1);

    float bodyBaseline = -1.0f;          // 1 行目の本文のベースライン
    float markerY = -1.0f;
    float noteMinY = 1e9f, noteMaxY = -1e9f;
    float bodyMaxY = -1e9f;
    int noteGlyphs = 0;
    for (const dl::Item& item : pages[0].dl.items) {
        const auto* run = std::get_if<dl::GlyphRun>(&item);
        if (!run || run->glyphs.empty()) continue;
        if (run->size == doctest::Approx(7.0f)) {
            for (const dl::Glyph& g : run->glyphs) { noteMinY = std::min(noteMinY, g.pos.y); noteMaxY = std::max(noteMaxY, g.pos.y); }
            noteGlyphs += static_cast<int>(run->glyphs.size());
        } else if (run->size == doctest::Approx(6.0f)) {
            if (markerY < 0.0f) markerY = run->glyphs.front().pos.y;
        } else {
            if (bodyBaseline < 0.0f) bodyBaseline = run->glyphs.front().pos.y;
            for (const dl::Glyph& g : run->glyphs) bodyMaxY = std::max(bodyMaxY, g.pos.y);
        }
    }
    REQUIRE(bodyBaseline > 0.0f);
    REQUIRE(markerY > 0.0f);
    // 記号は本文のベースラインより上（上付き）
    CHECK(markerY < bodyBaseline - 2.0f);
    // 注は版面の下端側にあり、本文より下
    REQUIRE(noteGlyphs > 0);
    CHECK(noteMaxY <= bodyRect.bottom() + 0.5f);
    CHECK(noteMinY > bodyMaxY);
    // 注の頭に "1 " "2 " が付く: 7pt の '1' と '2' がある
    auto countSmall = [&](char32_t c) {
        const uint32_t gid = fx.jp->glyphIndex(c);
        int n = 0;
        for (const dl::Item& item : pages[0].dl.items) {
            if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
                if (run->size != doctest::Approx(7.0f)) continue;
                for (const dl::Glyph& g : run->glyphs) if (g.gid == gid) ++n;
            }
        }
        return n;
    };
    CHECK(countSmall(U'1') == 1);
    CHECK(countSmall(U'2') == 1);
}

TEST_CASE("FlowLayouter: footnotes push body text to the next page instead of overlapping") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{300, 200};
    seq.master.margin = page::Margins{20, 20, 20, 20};
    seq.master.writingMode = WritingMode::HorizontalTb;
    const TextStyle body = fx.style(10.0f);
    const TextStyle noteStyle = fx.style(8.0f);

    block::Flow flow;
    for (int i = 0; i < 4; ++i) {
        inl::Paragraph p;
        p.runs.push_back(inl::InlineRun{u"注のある段落", body});
        std::u16string note;
        for (int k = 0; k < 6; ++k) note += u"注の本文。";
        p.addFootnote(inl::Paragraph::plain(note, noteStyle), inl::superscriptStyle(body));
        p.runs.push_back(inl::InlineRun{u"がいくつも続く。がいくつも続く。がいくつも続く。", body});
        flow.addParagraph(p);
    }
    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() >= 2);
    // どのページでも、本文（10pt）の最大 y < 注（8pt）の最小 y、注は版面内
    int notesTotal = 0;
    for (const page::Page& pg : pages) {
        float bodyMax = -1e9f, noteMin = 1e9f, noteMax = -1e9f;
        int notes = 0;
        for (const dl::Item& item : pg.dl.items) {
            const auto* run = std::get_if<dl::GlyphRun>(&item);
            if (!run) continue;
            const bool isNote = run->size == doctest::Approx(8.0f);
            for (const dl::Glyph& g : run->glyphs) {
                if (isNote) { noteMin = std::min(noteMin, g.pos.y); noteMax = std::max(noteMax, g.pos.y); ++notes; }
                else if (run->size == doctest::Approx(10.0f)) bodyMax = std::max(bodyMax, g.pos.y);
            }
        }
        if (notes > 0) {
            CHECK(bodyMax < noteMin);
            CHECK(noteMax <= seq.master.bodyRect(pg.number).bottom() + 0.5f);
        }
        notesTotal += notes;
    }
    CHECK(notesTotal > 0);
    // 番号は連番: 8pt の '4' がある
    const uint32_t g4 = fx.jp->glyphIndex(U'4');
    int n4 = 0;
    for (const page::Page& pg : pages) {
        for (const dl::Item& item : pg.dl.items) {
            if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
                if (run->size != doctest::Approx(8.0f)) continue;
                for (const dl::Glyph& g : run->glyphs) if (g.gid == g4) ++n4;
            }
        }
    }
    CHECK(n4 == 1);
}

TEST_CASE("FlowLayouter: vertical table keeps header column at the right and glyphs inside the body") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{320, 300};
    seq.master.margin = page::Margins{20, 20, 20, 20};
    seq.master.writingMode = WritingMode::VerticalRl;

    block::TableBlock t;
    t.columns = {block::TableColumn{0.0f}, block::TableColumn{0.0f}};
    auto cell = [&](std::u16string s) {
        block::TableCell c;
        c.paras.push_back(inl::Paragraph::plain(std::move(s), fx.style(9.0f)));
        return c;
    };
    block::TableRow head;
    head.header = true;
    head.cells = {cell(u"項目"), cell(u"値")};
    t.rows.push_back(head);
    for (const char16_t* name : {u"行一", u"行二", u"行三", u"行四"}) {
        block::TableRow r;
        r.cells = {cell(name), cell(u"内容の文章")};
        t.rows.push_back(r);
    }
    block::Flow flow;
    flow.addParagraph(inl::Paragraph::plain(u"縦組みの表。", fx.style(10.0f)));
    flow.addTable(t);
    flow.addParagraph(inl::Paragraph::plain(u"表の後の段落。", fx.style(10.0f)));

    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() == 1);
    const Rect body = seq.master.bodyRect(1);
    CHECK(glyphsWithin(pages[0].dl, body, 10.0f));

    // ヘッダ（項目）は最初の行 = 一番右。「行一」…「行四」は左へ進む
    auto xOf = [&](char32_t c) {
        const uint32_t gid = fx.jp->glyphIndex(c);
        float x = -1.0f;
        for (const dl::Item& item : pages[0].dl.items) {
            if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
                for (const dl::Glyph& g : run->glyphs) if (g.gid == gid && run->size == doctest::Approx(9.0f)) x = g.pos.x;
            }
        }
        return x;
    };
    const float xHead = xOf(U'項'), x1 = xOf(U'一'), x4 = xOf(U'四');
    REQUIRE(xHead > 0.0f);
    REQUIRE(x1 > 0.0f);
    REQUIRE(x4 > 0.0f);
    CHECK(xHead > x1);
    CHECK(x1 > x4);
    // 罫線（矩形）は版面内
    for (const dl::Item& item : pages[0].dl.items) {
        if (const auto* r = std::get_if<dl::RectItem>(&item)) {
            CHECK(r->rect.x >= body.x - 1.0f);
            CHECK(r->rect.right() <= body.right() + 1.0f);
        }
    }
}

TEST_CASE("FlowLayouter: keepWithNext carries a rule between heading and paragraph") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{300, 220};
    seq.master.margin = page::Margins{20, 20, 20, 20};
    seq.master.writingMode = WritingMode::HorizontalTb;

    block::Flow flow;
    // 段の下端近くまで埋める
    for (int i = 0; i < 7; ++i) flow.addParagraph(inl::Paragraph::plain(u"埋め草の段落。", fx.style(10.0f)));
    block::BlockStyle hs;
    hs.keepWithNext = true;
    flow.addHeading(inl::Paragraph::plain(u"見出し", fx.style(14.0f)), 1, hs);
    flow.addRule(1.0f, Color::rgb(200, 0, 0));
    std::u16string body;
    for (int i = 0; i < 8; ++i) body += u"見出しの後の本文。";
    block::BlockStyle ps;
    ps.keepTogether = true;   // 段落全体が残りに入らないので次のページへ → 見出しと罫線も一緒に移る
    flow.addParagraph(inl::Paragraph::plain(body, fx.style(10.0f)), ps);

    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() == 2);
    // 見出し（14pt）と赤い罫線は本文と同じ 2 ページ目にあり、1 ページ目には残らない
    auto hasHeading = [&](const page::Page& pg) {
        for (const dl::Item& item : pg.dl.items) {
            if (const auto* run = std::get_if<dl::GlyphRun>(&item)) if (run->size == doctest::Approx(14.0f)) return true;
        }
        return false;
    };
    auto hasRedRule = [&](const page::Page& pg) {
        for (const dl::Item& item : pg.dl.items) {
            if (const auto* r = std::get_if<dl::RectItem>(&item)) if (r->fill.r == 200 && r->fill.g == 0) return true;
        }
        return false;
    };
    CHECK_FALSE(hasHeading(pages[0]));
    CHECK_FALSE(hasRedRule(pages[0]));
    CHECK(hasHeading(pages[1]));
    CHECK(hasRedRule(pages[1]));
}

TEST_CASE("FlowLayouter: index collects {index:} markers with pages and sorts by reading") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{300, 200};
    seq.master.margin = page::Margins{20, 20, 20, 20};
    seq.master.writingMode = WritingMode::HorizontalTb;
    const TextStyle body = fx.style(10.0f);

    block::Flow flow;
    inl::Paragraph p1 = inl::Paragraph::plain(u"組版{index:くみはん|組版}の話。ルビ{index:るび|ルビ}も。", body);
    p1.annotations.push_back(inl::Annotation::ruby(0, 2, u"くみはん"));   // 記号を消しても位置がずれない
    flow.addParagraph(p1);
    flow.addPageBreak();
    flow.addParagraph(inl::Paragraph::plain(u"二ページ目でも組版{index:くみはん|組版}。Ascii{index:ascii|ASCII}。", body));
    flow.addPageBreak();
    block::IndexBlock idx;
    idx.style = fx.style(9.0f);
    flow.addIndex(idx);

    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() == 3);

    auto textOf = [&](const page::Page& pg, float size) {
        // gid 列を文字に戻せないので、特定文字の有無で見る
        std::vector<uint32_t> gids;
        for (const dl::Item& item : pg.dl.items) {
            if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
                if (run->size == doctest::Approx(size)) for (const dl::Glyph& g : run->glyphs) gids.push_back(g.gid);
            }
        }
        return gids;
    };
    auto has = [&](const std::vector<uint32_t>& gids, char32_t c) {
        const uint32_t gid = fx.jp->glyphIndex(c);
        return std::find(gids.begin(), gids.end(), gid) != gids.end();
    };
    // 本文には '{' が残らない
    CHECK_FALSE(has(textOf(pages[0], 10.0f), U'{'));
    CHECK_FALSE(has(textOf(pages[1], 10.0f), U'{'));
    // 索引ページ: 用語（組・ル・A）とページ番号 1, 2、見出し文字（か・ら・A）がある
    const std::vector<uint32_t> ix = textOf(pages[2], 9.0f);
    CHECK(has(ix, U'組'));
    CHECK(has(ix, U'ル'));
    CHECK(has(ix, U'A'));
    CHECK(has(ix, U'1'));
    CHECK(has(ix, U'2'));
    CHECK(has(ix, U','));   // 組版は 1, 2 の 2 ページ
    // 読み順: "ascii" < "くみはん" < "るび" → A、組、ル の順（y が増える）
    auto yOf = [&](char32_t c) {
        const uint32_t gid = fx.jp->glyphIndex(c);
        for (const dl::Item& item : pages[2].dl.items) {
            if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
                if (run->size != doctest::Approx(9.0f)) continue;
                for (const dl::Glyph& g : run->glyphs) if (g.gid == gid) return g.pos.y;
            }
        }
        return -1.0f;
    };
    CHECK(yOf(U'A') < yOf(U'組'));
    CHECK(yOf(U'組') < yOf(U'ル'));
    // ルビは消した記号のぶんずれず「組版」の上に乗る（ルビの x が本文先頭付近）
    float rubyX = 1e9f, firstX = 1e9f;
    for (const dl::Item& item : pages[0].dl.items) {
        if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
            for (const dl::Glyph& g : run->glyphs) {
                if (run->size < 9.0f) rubyX = std::min(rubyX, g.pos.x);
                else firstX = std::min(firstX, g.pos.x);
            }
        }
    }
    CHECK(rubyX < firstX + 12.0f);
}
