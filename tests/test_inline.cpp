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
