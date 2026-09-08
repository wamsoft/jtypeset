#include <doctest/doctest.h>

#include <cmath>
#include <memory>

#include "typeset/font/font_set.hpp"
#include "typeset/inl/item_builder.hpp"
#include "typeset/inl/paragraph.hpp"
#include "typeset/inl/shaper.hpp"
#include "typeset/text/char_class.hpp"
#include "typeset/text/line_break.hpp"
#include "typeset/text/utf.hpp"

using namespace typeset;

namespace {

/// テスト用フォント（リポジトリルートで実行する前提。無ければ nullptr）
struct Fixture {
    font::FontSet fonts;
    std::shared_ptr<glyphware::Face> jp;
    std::shared_ptr<glyphware::Face> latin;
    Fixture() {
        jp = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
        latin = fonts.loadFile("data/NotoSerif-Regular.ttf", "serif");
    }
    bool ok() const { return jp && latin; }
    TextStyle style(Pt size = 10.0f) const {
        TextStyle s;
        s.font.family = {"serif-ja", "serif"};
        s.size = size;
        return s;
    }
};

int kinsokuViolations(const inl::ParagraphFragment& frag) {
    int n = 0;
    const std::u16string& t = *frag.text;
    for (const inl::LineBox& l : frag.lines) {
        if (l.charEnd <= l.charStart) continue;
        if (text::isLineStartProhibited(text::getCharClass(text::codePointAt(t, l.charStart)))) ++n;
        if (text::isLineEndProhibited(text::getCharClass(text::codePointAt(t, l.charEnd - 1)))) ++n;
    }
    return n;
}

} // namespace

TEST_CASE("char classes") {
    CHECK(text::getCharClass(U'。') == text::CharClass::FullStop);
    CHECK(text::getCharClass(U'「') == text::CharClass::OpenBracket);
    CHECK(text::getCharClass(U'あ') == text::CharClass::Hiragana);
    CHECK(text::getCharClass(U'漢') == text::CharClass::Ideographic);
    CHECK(text::getCharClass(U'A') == text::CharClass::Western);
    CHECK(text::getCharClass(U'7') == text::CharClass::Digit);
    CHECK(text::isLineStartProhibited(text::getCharClass(U'」')));
    CHECK(text::isLineEndProhibited(text::getCharClass(U'（')));
    const text::GlueSpec g = text::getSpacing(text::CharClass::FullStop, text::CharClass::Hiragana);
    CHECK(g.natural == doctest::Approx(0.5f));
}

TEST_CASE("UAX#14 opportunities via libunibreak") {
    const auto b = text::lineBreakOpportunities(u"foo-bar baz", "en");
    REQUIRE(b.size() == 11);
    CHECK(b[3] == text::BreakOpportunity::Allowed);     // after '-'
    CHECK(b[0] == text::BreakOpportunity::Prohibited);  // inside "foo"
    CHECK(b[7] == text::BreakOpportunity::Allowed);     // after ' '
}

TEST_CASE("shaping: vertical upright and horizontal advances") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    const TextStyle st = fx.style(10.0f);

    const inl::ShapedText v = inl::shapeText(u"あい", st, fx.fonts, WritingMode::VerticalRl);
    REQUIRE(v.clusters.size() == 2);
    CHECK(v.advance == doctest::Approx(20.0f).epsilon(0.02));
    CHECK(v.clusters[1].origin == doctest::Approx(10.0f).epsilon(0.02));
    // 正立グリフは中心線を挟んで置かれる（x_offset ≈ -advance/2）
    CHECK(v.glyphs[0].block == doctest::Approx(-5.0f).epsilon(0.05));

    const inl::ShapedText h = inl::shapeText(u"あい", st, fx.fonts, WritingMode::HorizontalTb);
    REQUIRE(h.clusters.size() == 2);
    CHECK(h.advance == doctest::Approx(20.0f).epsilon(0.02));
    // ベースラインは中心線より下（ascent > descent）
    CHECK(h.glyphs[0].block > 0.0f);

    // 横倒しラン（縦組み中の欧文）
    const inl::ShapedText s = inl::shapeText(u"Ab", st, fx.fonts, WritingMode::VerticalRl);
    REQUIRE(s.clusters.size() == 2);
    CHECK_FALSE(s.clusters[0].upright);
    CHECK(s.glyphs[0].xform.xx == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(s.glyphs[0].xform.yx == doctest::Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("item builder: punctuation body compression and kinsoku penalties") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    const TextStyle st = fx.style(10.0f);
    std::vector<TextStyle> styles{st};
    const inl::ShapedText shaped = inl::shapeText(u"あ「い」。う", st, fx.fonts, WritingMode::VerticalRl);
    inl::ItemBuildContext ctx{fx.fonts, WritingMode::VerticalRl, TextOrientation::Mixed, &styles, &st, 0.0f};
    const auto items = inl::buildLineItems(shaped, {}, SpacingOptions{}, ctx);

    int boxes = 0, infPenalties = 0;
    float halfBoxes = 0;
    for (const inl::LineItem& it : items) {
        if (it.isBox()) {
            ++boxes;
            if (std::fabs(it.width - 5.0f) < 0.01f) ++halfBoxes;
        }
        if (it.isPenalty() && it.penalty >= inl::kInfinitePenalty) ++infPenalties;
    }
    CHECK(boxes == 6);
    CHECK(halfBoxes == 3);          // 「 」 。 が半角ボディ
    CHECK(infPenalties >= 3);       // 「の後・」の前・。の前 ＋ 終端
}

TEST_CASE("paragraph: vertical and horizontal lines are justified and kinsoku-free") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::Paragraph para = inl::Paragraph::plain(
        u"吾輩は猫である。名前はまだ無い。どこで生れたか、とんと見当がつかぬ。"
        u"「何でも薄暗いじめじめした所でニャーニャー泣いていた事だけは記憶している」。ABC def!",
        fx.style(10.0f));
    para.style.firstLineIndent = 1.0f;
    inl::ParagraphLayouter layouter(fx.fonts);

    for (WritingMode wm : {WritingMode::VerticalRl, WritingMode::HorizontalTb}) {
        const inl::ConstantLineShape shape(150.0f);
        const inl::ParagraphFragment frag = layouter.layout(para, wm, shape);
        REQUIRE(frag.lines.size() >= 3);
        CHECK(frag.complete);
        CHECK(kinsokuViolations(frag) == 0);
        CHECK(frag.lines[0].indent == doctest::Approx(10.0f));
        for (size_t i = 0; i + 1 < frag.lines.size(); ++i) {
            CHECK(frag.lines[i].length == doctest::Approx(150.0f - frag.lines[i].indent).epsilon(0.005));
        }
        // 文字範囲が連続している
        for (size_t i = 0; i + 1 < frag.lines.size(); ++i) {
            CHECK(frag.lines[i].charEnd == frag.lines[i + 1].charStart);
        }
        CHECK(frag.lines.back().charEnd == frag.text->size());
    }
}

TEST_CASE("paragraph: resume from charStart and maxLines") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::Paragraph para = inl::Paragraph::plain(
        u"あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん",
        fx.style(10.0f));
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(100.0f);

    const inl::ParagraphFragment all = layouter.layout(para, WritingMode::HorizontalTb, shape);
    const inl::ParagraphFragment head = layouter.layout(para, WritingMode::HorizontalTb, shape, 0, 2);
    REQUIRE(head.lines.size() == 2);
    CHECK_FALSE(head.complete);
    CHECK(head.charEnd == all.lines[2].charStart);

    const inl::ParagraphFragment tail =
        layouter.layout(para, WritingMode::HorizontalTb, shape, head.charEnd, -1, 2);
    CHECK(tail.complete);
    CHECK(tail.lines.size() + 2 == all.lines.size());
    CHECK(tail.lines[0].lineIndex == 2);
    CHECK(tail.lines[0].charStart == head.charEnd);
}

TEST_CASE("paragraph: ruby extends block extent on the annotation side") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::Paragraph para = inl::Paragraph::plain(u"漢字とかな", fx.style(10.0f));
    para.annotations.push_back(inl::Annotation::ruby(0, 2, u"かんじ"));
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(200.0f);

    const auto v = layouter.layout(para, WritingMode::VerticalRl, shape);
    REQUIRE(v.lines.size() == 1);
    CHECK(v.lines[0].blockMax == doctest::Approx(10.0f));    // 右へ 0.5em + ルビ 0.5em
    CHECK(v.lines[0].blockMin == doctest::Approx(-5.0f));
    CHECK(v.lines[0].glyphs.size() == 5 + 3);

    const auto h = layouter.layout(para, WritingMode::HorizontalTb, shape);
    REQUIRE(h.lines.size() == 1);
    CHECK(h.lines[0].blockMin == doctest::Approx(-10.0f));   // 上へ
    CHECK(h.lines[0].blockMax == doctest::Approx(5.0f));
}

TEST_CASE("jukugo ruby: overhang within the compound, fallback to group when too long") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(300.0f);

    // 各部分が親に収まる → モノルビと同じ（親は広がらない）
    {
        inl::Paragraph para = inl::Paragraph::plain(u"書生です", fx.style(10.0f));
        para.annotations.push_back(inl::Annotation::ruby(0, 2, u"しょ|せい", inl::RubyMode::Jukugo));
        const auto f = layouter.layout(para, WritingMode::HorizontalTb, shape);
        REQUIRE(f.lines.size() == 1);
        CHECK(f.lines[0].naturalLength == doctest::Approx(40.0f).epsilon(0.01));
        CHECK(f.lines[0].glyphs.size() == 4 + 4);
    }
    // 「にんげんじゅう」(7 字 = 35pt) は親 3 字 (30pt) より長い → グループとして親を広げる
    {
        inl::Paragraph para = inl::Paragraph::plain(u"人間中で", fx.style(10.0f));
        para.annotations.push_back(inl::Annotation::ruby(0, 3, u"にん|げん|じゅう", inl::RubyMode::Jukugo));
        const auto f = layouter.layout(para, WritingMode::HorizontalTb, shape);
        REQUIRE(f.lines.size() == 1);
        CHECK(f.lines[0].naturalLength > 40.0f);
    }
    // 「しょう|しゃ」(5 字 = 25pt) は親 2 字 (20pt) より長いが、熟語内で前後にずらせば…収まらない
    // → 「じょ|うしゃ」のような 2+3 で 25pt > 20pt もグループ。3 字の親に 2+2+1 (25pt ≤ 30pt) は熟語内で収まる
    {
        inl::Paragraph para = inl::Paragraph::plain(u"大学生の", fx.style(10.0f));
        para.annotations.push_back(inl::Annotation::ruby(0, 3, u"だい|がく|せい", inl::RubyMode::Jukugo));
        const auto f = layouter.layout(para, WritingMode::HorizontalTb, shape);
        REQUIRE(f.lines.size() == 1);
        // 各部分 2 字 (10pt) は親 1 字 (10pt) に収まるのでモノルビ経路
        CHECK(f.lines[0].naturalLength == doctest::Approx(40.0f).epsilon(0.01));
        inl::Paragraph para2 = inl::Paragraph::plain(u"大学生の", fx.style(10.0f));
        para2.annotations.push_back(inl::Annotation::ruby(0, 3, u"だいい|がく|せ", inl::RubyMode::Jukugo));
        const auto f2 = layouter.layout(para2, WritingMode::HorizontalTb, shape);
        REQUIRE(f2.lines.size() == 1);
        // 「だいい」(15pt) は親 1 字からはみ出すが熟語 3 字 (30pt) には収まる → 親は広がらない
        CHECK(f2.lines[0].naturalLength == doctest::Approx(40.0f).epsilon(0.01));
        // ルビのグリフは親範囲の先頭から 0 以上、30pt 以下に収まる
        for (const inl::PlacedGlyph& g : f2.lines[0].glyphs) {
            if (g.size < 6.0f) {
                CHECK(g.inline_ >= -0.01f);
                CHECK(g.inline_ <= 30.0f + 0.01f);
            }
        }
    }
}

TEST_CASE("ruby at line head / line end does not protrude outside the line") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    const TextStyle body = fx.style(10.0f);
    // 4 字で改行し、2 行目の先頭に長いルビ（6 字 × 0.5 = 3em > 2em）が来る
    inl::Paragraph p = inl::Paragraph::plain(u"ああああ漢字ああああ", body);
    p.annotations.push_back(inl::Annotation::ruby(4, 6, u"かんじかんじ"));
    p.style.spacing.punctuationSpacing = true;
    inl::ParagraphLayouter layouter(fx.fonts);
    for (WritingMode wm : {WritingMode::HorizontalTb, WritingMode::VerticalRl}) {
        const inl::ConstantLineShape shape(40.0f);
        const inl::ParagraphFragment frag = layouter.layout(p, wm, shape);
        REQUIRE(frag.lines.size() >= 2);
        const inl::LineBox& l1 = frag.lines[1];
        Pt rubyMin = 1e9f, rubyMax = -1e9f;
        int rubyGlyphs = 0;
        for (const inl::PlacedGlyph& g : l1.glyphs) {
            if (g.size < 9.0f) {   // ルビ（5pt）
                rubyMin = std::min(rubyMin, g.inline_);
                rubyMax = std::max(rubyMax, g.inline_ + g.advance);
                ++rubyGlyphs;
            }
        }
        REQUIRE(rubyGlyphs == 6);
        CHECK(rubyMin >= -0.01f);          // 行頭より前へ出ない
        CHECK(rubyMax <= 40.0f + 0.01f);   // 行末より後ろへ出ない
    }
    // 行末に来る場合: 先頭 2 字 + 漢字 で 4 字ちょうど、ルビは行末側へ 0.5em 掛かろうとする
    inl::Paragraph q = inl::Paragraph::plain(u"ああ漢字ああああ", body);
    q.annotations.push_back(inl::Annotation::ruby(2, 4, u"かんじかんじ"));
    const inl::ConstantLineShape shape(40.0f);
    const inl::ParagraphFragment frag = layouter.layout(q, WritingMode::HorizontalTb, shape);
    REQUIRE(frag.lines.size() >= 2);
    Pt rubyMax = -1e9f;
    for (const inl::PlacedGlyph& g : frag.lines[0].glyphs) {
        if (g.size < 9.0f) rubyMax = std::max(rubyMax, g.inline_ + g.advance);
    }
    CHECK(rubyMax <= frag.lines[0].length + 0.01f);
}

TEST_CASE("emphasis marks can be placed on the opposite side") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::Paragraph p = inl::Paragraph::plain(u"強調する文字", fx.style(10.0f));
    p.annotations.push_back(inl::Annotation::emphasis(0, 2));
    p.annotations.push_back(inl::Annotation::emphasis(2, 4, inl::EmphasisMark::Sesame, 0.5f, true));
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(200.0f);
    for (WritingMode wm : {WritingMode::HorizontalTb, WritingMode::VerticalRl}) {
        const inl::ParagraphFragment frag = layouter.layout(p, wm, shape);
        REQUIRE(frag.lines.size() == 1);
        std::vector<Pt> marks;
        for (const inl::PlacedGlyph& g : frag.lines[0].glyphs) if (g.size < 9.0f) marks.push_back(g.block);
        REQUIRE(marks.size() == 4);
        const float side = inl::annotationSide(wm);
        // 最初の 2 つは注記側、次の 2 つは反対側
        CHECK(marks[0] * side > 0.0f);
        CHECK(marks[1] * side > 0.0f);
        CHECK(marks[2] * side < 0.0f);
        CHECK(marks[3] * side < 0.0f);
    }
}

TEST_CASE("preserveSpaces expands tabs to tab stops") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    TextStyle st = fx.style(10.0f);
    st.font.family = {"serif"};   // 欧文フォント（空白幅が一定）
    inl::Paragraph p = inl::Paragraph::plain(u"a\tb\nab\tc\n        d", st);
    p.style.preserveSpaces = true;
    p.style.align = Align::Start;
    p.style.tabWidth = 4;
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(300.0f);
    const inl::ParagraphFragment frag = layouter.layout(p, WritingMode::HorizontalTb, shape);
    REQUIRE(frag.lines.size() == 3);
    auto xOfGid = [&](const inl::LineBox& line, char32_t c) {
        const uint32_t gid = fx.latin->glyphIndex(c);
        for (const inl::PlacedGlyph& g : line.glyphs) if (g.gid == gid) return g.inline_;
        return -1.0f;
    };
    const float xb = xOfGid(frag.lines[0], U'b');   // "a" + 3 spaces → 4 桁目
    const float xc = xOfGid(frag.lines[1], U'c');   // "ab" + 2 spaces → 4 桁目
    const float xd = xOfGid(frag.lines[2], U'd');   // 8 spaces → 8 桁目
    REQUIRE(xb > 0.0f);
    REQUIRE(xc > 0.0f);
    REQUIRE(xd > 0.0f);
    // 空白の幅と a/b の幅は違うので厳密には一致しないが、タブが消えていれば b は a の直後（< 8pt）になる。展開されていれば十分右
    CHECK(xb > 12.0f);
    CHECK(xc > 12.0f);
    CHECK(xd > xb);   // 8 桁は 4 桁より右
}
