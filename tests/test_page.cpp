#include <doctest/doctest.h>

#include <memory>

#include "typeset/block/block.hpp"
#include "typeset/font/font_set.hpp"
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

/// ページ内の全グリフのペン位置が rect（余裕付き）に入っているか
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

int glyphCount(const dl::DisplayList& list) {
    int n = 0;
    for (const dl::Item& item : list.items) {
        if (const auto* run = std::get_if<dl::GlyphRun>(&item)) n += static_cast<int>(run->glyphs.size());
    }
    return n;
}

} // namespace

TEST_CASE("PageMaster: body rect and columns") {
    page::PageMaster m;
    m.size = Size{400, 600};
    m.margin = page::Margins{50, 60, 30, 20};
    m.writingMode = WritingMode::HorizontalTb;
    m.duplex = true;
    const Rect odd = m.bodyRect(1);     // 横組み: 奇数ページは左がノド
    CHECK(odd.x == doctest::Approx(30));
    CHECK(odd.w == doctest::Approx(350));
    CHECK(odd.y == doctest::Approx(50));
    CHECK(odd.h == doctest::Approx(490));
    const Rect even = m.bodyRect(2);
    CHECK(even.x == doctest::Approx(20));

    const auto cols = m.columnRects(odd, 2, 10);
    REQUIRE(cols.size() == 2);
    CHECK(cols[0].w == doctest::Approx(170));
    CHECK(cols[1].x == doctest::Approx(30 + 170 + 10));

    m.writingMode = WritingMode::VerticalRl;
    const auto vcols = m.columnRects(odd, 2, 10);
    CHECK(vcols[0].h == doctest::Approx(240));
    CHECK(vcols[1].y == doctest::Approx(50 + 240 + 10));
}

TEST_CASE("FlowLayouter: paragraphs overflow to columns and pages, glyphs stay in the body") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    for (WritingMode wm : {WritingMode::HorizontalTb, WritingMode::VerticalRl}) {
        page::PageSequence seq;
        seq.master.size = Size{300, 300};
        seq.master.margin = page::Margins{30, 30, 30, 30};
        seq.master.writingMode = wm;
        seq.master.columns = 2;
        seq.master.columnGap = 12;

        block::Flow flow;
        const std::u16string para =
            u"吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。何でも薄暗いじめじめした所でニャーニャー泣いていた事だけは記憶している。";
        for (int i = 0; i < 6; ++i) {
            inl::Paragraph p = inl::Paragraph::plain(para, fx.style(10.0f));
            p.style.firstLineIndent = 1.0f;
            block::BlockStyle bs;
            bs.orphans = 2;
            bs.widows = 2;
            flow.addParagraph(std::move(p), bs);
        }

        page::FlowLayouter layouter(fx.fonts);
        const auto pages = layouter.layout(flow, seq);
        REQUIRE(pages.size() >= 2);
        int total = 0;
        for (const page::Page& pg : pages) {
            const Rect body = seq.master.bodyRect(pg.number);
            CHECK(glyphsWithin(pg.dl, body, 10.0f));
            total += glyphCount(pg.dl);
        }
        CHECK(total == static_cast<int>(para.size()) * 6);
    }
}

TEST_CASE("FlowLayouter: heading with keepWithNext moves with its paragraph") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{300, 200};
    seq.master.margin = page::Margins{20, 20, 20, 20};
    seq.master.writingMode = WritingMode::HorizontalTb;

    // 版面 260×160pt、行送り 17pt → 9 行、1 行 26 字。204 字（8 行）の段落の後に見出しを
    // 置くと、見出しは段末に 1 行入るが、次の段落が入らないので一緒に次ページへ行く
    block::Flow flow;
    std::u16string fillerText;
    for (int i = 0; i < 4; ++i) {
        fillerText += u"あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん";
    }
    fillerText += u"あいうえおかきくけこさしすせそたちつてと";
    inl::Paragraph filler = inl::Paragraph::plain(fillerText, fx.style(10.0f));
    filler.style.lineHeight = 1.7f;
    flow.addParagraph(filler);

    inl::Paragraph h = inl::Paragraph::plain(u"見出し", fx.style(10.0f));
    h.style.lineHeight = 1.7f;
    block::BlockStyle hs;
    hs.keepWithNext = true;
    flow.addHeading(h, 1, hs);

    inl::Paragraph next = inl::Paragraph::plain(u"本文の段落が続く。本文の段落が続く。", fx.style(10.0f));
    next.style.lineHeight = 1.7f;
    block::BlockStyle bs;
    bs.orphans = 2;
    flow.addParagraph(next, bs);

    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() == 2);
    // 1 ページ目に見出しのグリフ（「見」）が無いこと
    const uint32_t gidMi = fx.jp->glyphIndex(U'見');
    bool onFirst = false;
    for (const dl::Item& item : pages[0].dl.items) {
        if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
            for (const dl::Glyph& g : run->glyphs) if (g.gid == gidMi) onFirst = true;
        }
    }
    CHECK_FALSE(onFirst);
    CHECK(glyphCount(pages[1].dl) == 3 + 18);   // 見出し 3 字 ＋ 本文 18 字
}

TEST_CASE("FlowLayouter: labeled block indents the body and running text is substituted") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }

    page::PageSequence seq;
    seq.master.size = Size{300, 300};
    seq.master.margin = page::Margins{40, 40, 30, 30};
    seq.master.writingMode = WritingMode::HorizontalTb;
    page::RunningText f;
    f.para = inl::Paragraph::plain(u"{page}", fx.style(8.0f));
    f.para.style.align = Align::Center;
    f.offset = 15;
    seq.master.footer = f;

    block::Flow flow;
    flow.addLabeled(inl::Paragraph::plain(u"太郎", fx.style(10.0f)),
                    inl::Paragraph::plain(u"本文はラベルの後ろから始まり、折り返しても同じ位置で揃う。本文はラベルの後ろから始まる。", fx.style(10.0f)),
                    40.0f, 10.0f);

    page::FlowLayouter layouter(fx.fonts);
    const auto pages = layouter.layout(flow, seq);
    REQUIRE(pages.size() == 1);
    const Rect body = seq.master.bodyRect(1);

    int labelGlyphs = 0, bodyGlyphs = 0, footerGlyphs = 0;
    for (const dl::Item& item : pages[0].dl.items) {
        const auto* run = std::get_if<dl::GlyphRun>(&item);
        if (!run) continue;
        for (const dl::Glyph& g : run->glyphs) {
            if (g.pos.y > body.bottom()) { ++footerGlyphs; continue; }
            if (g.pos.x < body.x + 40.0f) ++labelGlyphs;
            else {
                ++bodyGlyphs;
                CHECK(g.pos.x >= body.x + 50.0f - 0.01f);
            }
        }
    }
    CHECK(labelGlyphs == 2);
    CHECK(bodyGlyphs > 30);
    CHECK(footerGlyphs == 1);   // "1"
}
