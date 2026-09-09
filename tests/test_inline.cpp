#include <doctest/doctest.h>

#include <cmath>
#include <memory>
#include <variant>

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
    // 和欧間アキ（四分）は漢字・仮名と欧文の間だけ。括弧類・句読点・区切り約物と欧文の間はベタ（表 3）
    using CC = text::CharClass;
    CHECK(text::getSpacing(CC::Ideographic, CC::Western).natural == doctest::Approx(0.25f));
    CHECK(text::getSpacing(CC::Western, CC::Hiragana).natural == doctest::Approx(0.25f));
    CHECK(text::getSpacing(CC::OpenBracket, CC::Western).natural == 0.0f);
    CHECK(text::getSpacing(CC::Western, CC::CloseBracket).natural == 0.0f);
    CHECK(text::getSpacing(CC::Western, CC::FullStop).natural == 0.0f);
    CHECK(text::getSpacing(CC::Western, CC::Comma).natural == 0.0f);
    CHECK(text::getSpacing(CC::Western, CC::Dividing).natural == 0.0f);
    CHECK(text::getSpacing(CC::Western, CC::IdeographicSpace).natural == 0.0f);
    // 欧文 → 始め括弧類、終わり括弧類 → 欧文は括弧側の二分アキ
    CHECK(text::getSpacing(CC::Western, CC::OpenBracket).natural == doctest::Approx(0.5f));
    CHECK(text::getSpacing(CC::CloseBracket, CC::Western).natural == doctest::Approx(0.5f));
    CHECK(text::getSpacing(CC::MiddleDot, CC::Western).natural == doctest::Approx(0.25f));
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

TEST_CASE("emoji: color layers are emitted as filled paths and ZWJ sequences stay together in vertical text") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    // カラー絵文字フォント: make fontdata の Noto-COLRv1（COLR v1）、無ければ Windows の Segoe UI Emoji
    auto emoji = fx.fonts.loadFile("data/Noto-COLRv1.ttf", "emoji");
    if (!emoji) emoji = fx.fonts.loadFile("C:/Windows/Fonts/seguiemj.ttf", "emoji");
    if (!emoji) { MESSAGE("no color emoji font; skipping"); return; }
    REQUIRE(emoji->descriptor().color);

    TextStyle st = fx.style(14.0f);
    st.font.family = {"serif-ja", "emoji"};
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(300.0f);

    // 横組み: 😀 はカラーレイヤ（塗り付きの PathItem、黒以外の色を含む）として出る
    {
        inl::Paragraph p = inl::Paragraph::plain(u"あ😀い", st);
        const inl::ParagraphFragment frag = layouter.layout(p, WritingMode::HorizontalTb, shape);
        REQUIRE(frag.lines.size() == 1);
        dl::DisplayList out;
        inl::emitParagraph(out, frag, WritingMode::HorizontalTb, Point{0, 20});
        int colored = 0, glyphRuns = 0;
        for (const dl::Item& item : out.items) {
            if (const auto* pi = std::get_if<dl::PathItem>(&item)) {
                if (pi->fill && (pi->fill->r != pi->fill->g || pi->fill->g != pi->fill->b)) ++colored;
            } else if (std::get_if<dl::GlyphRun>(&item)) {
                ++glyphRuns;
            }
        }
        CHECK(colored >= 1);      // 黄色い顔など
        CHECK(glyphRuns >= 1);    // 「あ」「い」は通常のグリフ
    }
    // 縦組み: 👨‍👩‍👧（ZWJ シーケンス）が 1 つのクラスタになり、正立で置かれる
    {
        inl::Paragraph p = inl::Paragraph::plain(u"あ👨\u200D👩\u200D👧い", st);
        const inl::ParagraphFragment frag = layouter.layout(p, WritingMode::VerticalRl, shape);
        REQUIRE(frag.lines.size() == 1);
        // 絵文字フォントのグリフは 1 クラスタにまとまり（フォントによっては合成の複数グリフ）、列方向には 1 つぶんしか進まない。
        // 「い」の位置は「あ」＋絵文字 1 つぶん（アセント＋ディセント）の先
        float emojiMinInline = 1e9f, emojiMaxInline = -1e9f, iInline = -1.0f, aInline = -1.0f;
        int emojiClusters = 0;
        uint32_t lastChar = 9999;
        for (const inl::PlacedGlyph& g : frag.lines[0].glyphs) {
            if (g.face == emoji) {
                emojiMinInline = std::min(emojiMinInline, g.inline_);
                emojiMaxInline = std::max(emojiMaxInline, g.inline_);
                if (g.charIndex != lastChar) { ++emojiClusters; lastChar = g.charIndex; }
            } else if (g.charIndex == 0) {
                aInline = g.inline_;
            } else {
                iInline = g.inline_;
            }
        }
        CHECK(emojiClusters == 1);
        CHECK(emojiMaxInline - emojiMinInline < 1.0f);       // 合成グリフは同じ行位置（横に並ぶ）
        CHECK(iInline > aInline + 14.0f);                    // 「い」は「あ」より 1 字＋絵文字ぶん先
        CHECK(iInline < aInline + 14.0f * 3.0f);             // 3 つ縦に並んだら 4 字ぶん先になる
    }
    // 国旗（Regional Indicator の対）も 1 クラスタで正立（Segoe では "JP" の字形になるが、向きと結合を見る）
    {
        inl::Paragraph p = inl::Paragraph::plain(u"あ🇯🇵い", st);
        const inl::ParagraphFragment frag = layouter.layout(p, WritingMode::VerticalRl, shape);
        REQUIRE(frag.lines.size() == 1);
        int emojiGlyphs = 0;
        bool upright = true;
        for (const inl::PlacedGlyph& g : frag.lines[0].glyphs) {
            if (g.face == emoji) { ++emojiGlyphs; if (!g.xform.isIdentity()) upright = false; }
        }
        CHECK(emojiGlyphs >= 1);
        CHECK(upright);
    }
}

TEST_CASE("emoji: bitmap (CBDT) color fonts are emitted as images at the text size") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    auto emoji = fx.fonts.loadFile("data/NotoColorEmoji.ttf", "emoji");
    if (!emoji) { MESSAGE("no NotoColorEmoji.ttf (make fontdata); skipping"); return; }
    REQUIRE(emoji->descriptor().color);
    TextStyle st = fx.style(14.0f);
    st.font.family = {"serif-ja", "emoji"};
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(300.0f);
    inl::Paragraph p = inl::Paragraph::plain(u"あ😀い", st);
    const inl::ParagraphFragment frag = layouter.layout(p, WritingMode::HorizontalTb, shape);
    REQUIRE(frag.lines.size() == 1);
    dl::DisplayList out;
    inl::emitParagraph(out, frag, WritingMode::HorizontalTb, Point{0, 20});
    int images = 0;
    Rect box;
    for (const dl::Item& item : out.items) {
        if (const auto* im = std::get_if<dl::ImageItem>(&item)) {
            ++images;
            REQUIRE(static_cast<bool>(im->image));
            const Point a = im->xform.apply(Point{0, 0});
            const Point b = im->xform.apply(Point{static_cast<float>(im->image->width), static_cast<float>(im->image->height)});
            box = Rect{std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x), std::fabs(b.y - a.y)};
        }
    }
    CHECK(images == 1);
    // 大きさは文字サイズ程度（0.8〜1.6em）、ベースライン（y=20）をまたいで上側に大部分がある
    CHECK(box.w > 14.0f * 0.8f);
    CHECK(box.w < 14.0f * 1.6f);
    CHECK(box.y < 20.0f);
    CHECK(box.bottom() > 20.0f - 14.0f * 0.4f);
}

namespace {

std::vector<dl::RectItem> rectsOf(const dl::DisplayList& out) {
    std::vector<dl::RectItem> v;
    for (const dl::Item& item : out.items) {
        if (const auto* r = std::get_if<dl::RectItem>(&item)) v.push_back(*r);
    }
    return v;
}

std::vector<dl::GlyphRun> runsOf(const dl::DisplayList& out) {
    std::vector<dl::GlyphRun> v;
    for (const dl::Item& item : out.items) {
        if (const auto* r = std::get_if<dl::GlyphRun>(&item)) v.push_back(*r);
    }
    return v;
}

} // namespace

TEST_CASE("text decoration: underline below the baseline (horizontal) / right of the column (vertical), strikethrough through the middle") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::ParagraphLayouter layouter(fx.fonts);
    const Pt size = 10.0f;

    for (bool underline : {true, false}) {
        TextStyle st = fx.style(size);
        if (underline) st.underline = TextDecoration{};
        else st.strikethrough = TextDecoration{};
        inl::Paragraph para = inl::Paragraph::plain(u"吾輩は猫である。ABC", st);
        para.style.align = Align::Start;
        para.style.lineBreak.justify = false;
        const inl::ConstantLineShape shape(300.0f);

        // 横組み: 行の中心線 y=20。下線はベースライン（中心線＋約 0.38em）より下、em box の下端付近
        {
            const inl::ParagraphFragment frag = layouter.layout(para, WritingMode::HorizontalTb, shape);
            REQUIRE(frag.lines.size() == 1);
            REQUIRE(frag.styleMetrics.size() == frag.styles.size());
            dl::DisplayList out;
            inl::emitParagraph(out, frag, WritingMode::HorizontalTb, Point{0, 20});
            const auto rects = rectsOf(out);
            REQUIRE(rects.size() == 1);
            const Rect r = rects[0].rect;
            CHECK(r.x == doctest::Approx(0.0f).epsilon(0.01));
            CHECK(r.w > frag.lines[0].length * 0.9f);
            CHECK(r.w <= frag.lines[0].length + 1.0f);
            CHECK(r.h > 0.2f);
            CHECK(r.h < size * 0.15f);
            const Pt cy = r.y + r.h * 0.5f;
            const Pt baseline = 20.0f + frag.styleMetrics[0].baseline;
            if (underline) {
                CHECK(cy > baseline);
                CHECK(cy < 20.0f + size * 0.5f + size * 0.15f);
            } else {
                CHECK(cy < baseline);
                CHECK(cy > 20.0f - size * 0.2f);
            }
            // 塗りは文字色
            CHECK(rects[0].fill == st.fill);
            // 下線は文字の下（先）に、打消し線は文字の上（後）に出る
            size_t rectPos = 0, runPos = 0;
            for (size_t i = 0; i < out.items.size(); ++i) {
                if (std::holds_alternative<dl::RectItem>(out.items[i])) rectPos = i;
                if (std::holds_alternative<dl::GlyphRun>(out.items[i])) runPos = i;
            }
            if (underline) CHECK(rectPos < runPos); else CHECK(rectPos > runPos);
        }
        // 縦組み: 列の中心線 x=100。傍線は右側（x > 100 + 0.5em）、打消し線は中心線上
        {
            const inl::ParagraphFragment frag = layouter.layout(para, WritingMode::VerticalRl, shape);
            REQUIRE(frag.lines.size() == 1);
            dl::DisplayList out;
            inl::emitParagraph(out, frag, WritingMode::VerticalRl, Point{100, 0});
            const auto rects = rectsOf(out);
            REQUIRE(rects.size() == 1);
            const Rect r = rects[0].rect;
            CHECK(r.y == doctest::Approx(0.0f).epsilon(0.01));
            CHECK(r.h > frag.lines[0].length * 0.9f);
            CHECK(r.w < size * 0.15f);
            const Pt cx = r.x + r.w * 0.5f;
            if (underline) {
                CHECK(cx > 100.0f + size * 0.5f);
                CHECK(cx < 100.0f + size * 0.5f + size * 0.2f);
            } else {
                CHECK(cx == doctest::Approx(100.0f).epsilon(0.01));
            }
        }
    }

    // 色・太さ・位置の指定
    {
        TextStyle st = fx.style(size);
        TextDecoration d;
        d.color = Color::rgb(255, 0, 0);
        d.thickness = 1.5f;
        d.offset = 0.2f;
        st.underline = d;
        inl::Paragraph para = inl::Paragraph::plain(u"猫", st);
        const inl::ConstantLineShape shape(100.0f);
        const inl::ParagraphFragment frag = layouter.layout(para, WritingMode::HorizontalTb, shape);
        dl::DisplayList out;
        inl::emitParagraph(out, frag, WritingMode::HorizontalTb, Point{0, 20});
        const auto rects = rectsOf(out);
        REQUIRE(rects.size() == 1);
        CHECK(rects[0].fill == Color::rgb(255, 0, 0));
        CHECK(rects[0].rect.h == doctest::Approx(1.5f));
        const Pt cy = rects[0].rect.y + rects[0].rect.h * 0.5f;
        CHECK(cy == doctest::Approx(20.0f + frag.styleMetrics[0].baseline +
                                    frag.styleMetrics[0].decoration.underlineOffset + size * 0.2f).epsilon(0.01));
    }
    // スタイルが変わると線も分かれ、下線の無い run には出ない
    {
        TextStyle a = fx.style(size);
        a.underline = TextDecoration{};
        TextStyle b = fx.style(size);
        inl::Paragraph para;
        para.runs.push_back(inl::InlineRun{u"吾輩は", a});
        para.runs.push_back(inl::InlineRun{u"猫である。", b});
        para.runs.push_back(inl::InlineRun{u"名前は", a});
        const inl::ConstantLineShape shape(300.0f);
        const inl::ParagraphFragment frag = layouter.layout(para, WritingMode::HorizontalTb, shape);
        dl::DisplayList out;
        inl::emitParagraph(out, frag, WritingMode::HorizontalTb, Point{0, 20});
        const auto rects = rectsOf(out);
        REQUIRE(rects.size() == 2);
        CHECK(rects[0].rect.w == doctest::Approx(size * 3.0f).epsilon(0.02));
        CHECK(rects[1].rect.x > rects[0].rect.right() + size * 4.0f);
    }
}

TEST_CASE("text layers: shadow and outline layers are emitted bottom-up, aligned from the top layer") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(300.0f);

    // shadow + stroke: 影の GlyphRun（ずらし・ぼかし）→ 塗り＋縁取りの GlyphRun
    {
        TextStyle st = fx.style(10.0f);
        TextShadow sh;
        sh.color = Color::rgba(255, 0, 0, 200);
        sh.offset = Point{1.0f, 2.0f};
        sh.blur = 1.5f;
        st.shadow = sh;
        Stroke stroke;
        stroke.color = Color::rgb(0, 0, 255);
        stroke.width = 0.5f;
        st.stroke = stroke;
        REQUIRE(st.resolvedLayers().size() == 2);

        inl::Paragraph para = inl::Paragraph::plain(u"吾輩は猫である。", st);
        const inl::ParagraphFragment frag = layouter.layout(para, WritingMode::HorizontalTb, shape);
        dl::DisplayList out;
        inl::emitParagraph(out, frag, WritingMode::HorizontalTb, Point{0, 20});
        const auto runs = runsOf(out);
        REQUIRE(runs.size() == 2);
        CHECK(runs[0].fill == Color::rgba(255, 0, 0, 200));
        CHECK(!runs[0].stroke);
        CHECK(runs[0].blur == doctest::Approx(1.5f));
        CHECK(runs[1].fill == st.fill);
        REQUIRE(runs[1].stroke);
        CHECK(*runs[1].stroke == stroke);
        CHECK(runs[1].blur == 0.0f);
        REQUIRE(runs[0].glyphs.size() == runs[1].glyphs.size());
        for (size_t i = 0; i < runs[0].glyphs.size(); ++i) {
            CHECK(runs[0].glyphs[i].gid == runs[1].glyphs[i].gid);
            CHECK(runs[0].glyphs[i].pos.x == doctest::Approx(runs[1].glyphs[i].pos.x + 1.0f));
            CHECK(runs[0].glyphs[i].pos.y == doctest::Approx(runs[1].glyphs[i].pos.y + 2.0f));
        }
    }
    // 明示した層（二重縁取り）は指定順。fill / stroke は使われない
    {
        TextStyle st = fx.style(10.0f);
        st.fill = Color::rgb(1, 2, 3);
        Stroke outer;
        outer.color = Color::rgb(0, 0, 0);
        outer.width = 2.0f;
        Stroke inner;
        inner.color = Color::rgb(255, 255, 255);
        inner.width = 1.0f;
        st.layers = {TextLayer::outlined(outer), TextLayer::outlined(inner), TextLayer::filled(Color::rgb(200, 0, 0))};

        inl::Paragraph para = inl::Paragraph::plain(u"猫である", st);
        const inl::ParagraphFragment frag = layouter.layout(para, WritingMode::VerticalRl, shape);
        dl::DisplayList out;
        inl::emitParagraph(out, frag, WritingMode::VerticalRl, Point{50, 0});
        const auto runs = runsOf(out);
        REQUIRE(runs.size() == 3);
        CHECK(!runs[0].fill);
        REQUIRE(runs[0].stroke);
        CHECK(runs[0].stroke->width == doctest::Approx(2.0f));
        REQUIRE(runs[1].stroke);
        CHECK(runs[1].stroke->width == doctest::Approx(1.0f));
        CHECK(!runs[2].stroke);
        CHECK(runs[2].fill == Color::rgb(200, 0, 0));
    }
    // 層の数が違うスタイルが混ざる行: 影は行全体で先に、塗りは全部あとに出る（同じ外観なので 1 つの run にまとまる）
    {
        TextStyle a = fx.style(10.0f);
        a.shadow = TextShadow{};
        TextStyle b = fx.style(10.0f);
        inl::Paragraph para;
        para.runs.push_back(inl::InlineRun{u"吾輩は", a});
        para.runs.push_back(inl::InlineRun{u"猫である。", b});
        const inl::ParagraphFragment frag = layouter.layout(para, WritingMode::HorizontalTb, shape);
        dl::DisplayList out;
        inl::emitParagraph(out, frag, WritingMode::HorizontalTb, Point{0, 20});
        const auto runs = runsOf(out);
        REQUIRE(runs.size() == 2);
        CHECK(runs[0].fill == a.shadow->color);
        CHECK(runs[0].glyphs.size() == 3);
        CHECK(runs[1].fill == a.fill);
        CHECK(runs[1].glyphs.size() == 8);
    }
}

TEST_CASE("font set: nearest weight / italic face is selected per family, falling back to fake bold") {
    font::FontSet fonts;
    auto regular = fonts.loadFile("data/NotoSerif-Regular.ttf", "serif");
    auto bold = fonts.loadFile("data/NotoSerif-Bold.ttf", "serif-bold");
    if (!regular || !bold) { MESSAGE("fonts not found; skipping"); return; }
    // 別キーでも name テーブルの family 名（Noto Serif）で同じ family として引ける
    CHECK((fonts.find("Noto Serif") == regular));
    CHECK((fonts.select("Noto Serif", 700, false) == bold));
    CHECK((fonts.select("Noto Serif", 600, false) == bold));
    CHECK((fonts.select("Noto Serif", 500, false) == regular));   // 400〜500 は 500 まで上を見てから下
    CHECK((fonts.select("Noto Serif", 300, false) == regular));
    CHECK((fonts.select("Noto Serif", 900, false) == bold));
    // 斜体が無ければ通常の face（組版層でフェイク斜体）
    CHECK((fonts.select("Noto Serif", 400, true) == regular));
    auto italic = fonts.loadFile("data/NotoSerif-Italic.ttf", "serif-italic");
    if (italic) {
        CHECK((fonts.select("Noto Serif", 400, true) == italic));
        CHECK((fonts.select("Noto Serif", 700, true) == italic));   // 斜体の一致がウェイトより優先
        CHECK((fonts.select("Noto Serif", 400, false) == regular));
    }

    // FontSpec の weight で resolve / primary が太字 face を返し、シェイプ結果はフェイクボールド無し
    FontSpec spec;
    spec.family = {"Noto Serif"};
    spec.weight = 700;
    CHECK((fonts.resolve(spec, U'A') == bold));
    CHECK((fonts.primary(spec) == bold));
    TextStyle st;
    st.font = spec;
    st.size = 10.0f;
    const inl::ShapedText shaped = inl::shapeText(u"AB", st, fonts, WritingMode::HorizontalTb);
    REQUIRE(!shaped.glyphs.empty());
    CHECK((shaped.glyphs[0].face == bold));
    CHECK(shaped.glyphs[0].embolden == 0.0f);

    // 太字 face を持たない family は今までどおりフェイクボールド
    font::FontSet only;
    if (only.loadFile("data/NotoSerif-Regular.ttf", "serif")) {
        const inl::ShapedText fake = inl::shapeText(u"AB", st, only, WritingMode::HorizontalTb);
        REQUIRE(!fake.glyphs.empty());
        CHECK(fake.glyphs[0].embolden > 0.0f);
    }
}

TEST_CASE("font set: declared fonts open on first use and unknown files fail quietly") {
    font::FontSet fonts;
    font::FontDeclaration jp;
    jp.key = "serif-ja";
    jp.path = "data/NotoSerifJP-Regular.otf";
    jp.family = {"serif"};
    REQUIRE(fonts.declare(jp));
    font::FontDeclaration latin;
    latin.key = "serif-latin";
    latin.path = "data/NotoSerif-Regular.ttf";
    latin.family = {"serif"};
    latin.ranges = {{0x0000, 0x024F}};      // カバレッジを宣言: 和字では開かれない
    REQUIRE(fonts.declare(latin));
    font::FontDeclaration missing;
    missing.key = "nope";
    missing.path = "data/does-not-exist.ttf";
    missing.family = {"serif"};
    missing.weight = 700;
    REQUIRE(fonts.declare(missing));
    CHECK(fonts.size() == 3);
    CHECK(fonts.has("serif-ja"));
    CHECK(!fonts.isLoaded("serif-ja"));
    CHECK(!fonts.isLoaded("serif-latin"));

    FontSpec spec;
    spec.family = {"serif"};
    auto face = fonts.resolve(spec, U'猫');
    if (!face) { MESSAGE("fonts not found; skipping"); return; }
    CHECK(fonts.isLoaded("serif-ja"));
    CHECK(!fonts.isLoaded("serif-latin"));      // ranges で外れたので開いていない
    CHECK(face->descriptor().key == "serif-ja");

    // 開けない太字宣言は無視され、通常ウェイトに落ちる
    spec.weight = 700;
    auto b = fonts.resolve(spec, U'猫');
    REQUIRE(static_cast<bool>(b));
    CHECK(b->descriptor().key == "serif-ja");
    CHECK(!fonts.isLoaded("nope"));

    // 同点（同じ weight / italic）なら宣言順で先の serif-ja が 'A' も持つので返り、serif-latin はまだ開かない
    spec.weight = 400;
    auto l = fonts.resolve(spec, U'A');
    REQUIRE(static_cast<bool>(l));
    CHECK(l->descriptor().key == "serif-ja");
    CHECK(!fonts.isLoaded("serif-latin"));
    // キーで直接引けば開く
    spec.family = {"serif-latin"};
    auto l2 = fonts.resolve(spec, U'A');
    REQUIRE(static_cast<bool>(l2));
    CHECK(fonts.isLoaded("serif-latin"));
    CHECK(l2->descriptor().key == "serif-latin");
    CHECK(fonts.keys() == std::vector<std::string>{"serif-ja", "serif-latin", "nope"});
}

TEST_CASE("font set: language-linked fonts are tried before the style's families") {
    font::FontSet fonts;
    auto serif = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
    auto sans = fonts.loadFile("data/NotoSansJP-Regular.otf", "sans-ja");
    if (!serif || !sans) { MESSAGE("fonts not found; skipping"); return; }
    fonts.setLanguageFonts("zh", {"sans-ja"});

    FontSpec spec;
    spec.family = {"serif-ja"};
    CHECK((fonts.resolve(spec, U'中', "ja") == serif));
    CHECK((fonts.resolve(spec, U'中', "zh") == sans));
    CHECK((fonts.resolve(spec, U'中', "zh-Hans") == sans));   // 主言語で引く
    CHECK((fonts.resolve(spec, U'中', "") == serif));
    CHECK((fonts.primary(spec) == serif));                     // 行のメトリクス基準は変えない
    // 言語のフォントが文字を持たなければ本来の family へ
    fonts.setLanguageFonts("ko", {"missing-family"});
    CHECK((fonts.resolve(spec, U'中', "ko") == serif));
    fonts.setLanguageFonts("zh", {});
    CHECK((fonts.resolve(spec, U'中', "zh") == serif));

    // 宣言の languages でも同じ（開くのは使うとき）
    font::FontDeclaration d;
    d.key = "sans-zh";
    d.path = "data/NotoSansJP-Regular.otf";
    d.languages = {"zh"};
    REQUIRE(fonts.declare(d));
    CHECK(!fonts.isLoaded("sans-zh"));
    auto zh = fonts.resolve(spec, U'中', "zh-Hant");
    REQUIRE(static_cast<bool>(zh));
    CHECK(zh->descriptor().key == "sans-zh");
    CHECK((fonts.resolve(spec, U'中', "ja") == serif));

    // TextStyle::language が itemize に効く: 同じ段落の中で中国語の run だけ別 face
    TextStyle ja;
    ja.font = spec;
    ja.size = 10.0f;
    TextStyle zhStyle = ja;
    zhStyle.language = "zh-Hans";
    inl::Paragraph para;
    para.runs.push_back(inl::InlineRun{u"日本語", ja});
    para.runs.push_back(inl::InlineRun{u"中文", zhStyle});
    inl::ParagraphLayouter layouter(fonts);
    const inl::ConstantLineShape shape(200.0f);
    const inl::ParagraphFragment frag = layouter.layout(para, WritingMode::HorizontalTb, shape);
    REQUIRE(frag.lines.size() == 1);
    REQUIRE(frag.lines[0].glyphs.size() == 5);
    CHECK((frag.lines[0].glyphs[0].face == serif));
    CHECK(frag.lines[0].glyphs[3].face->descriptor().key == "sans-zh");
}

TEST_CASE("query: char boxes, rects for a range, hit test and caret agree with the emitted glyphs") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::ParagraphLayouter layouter(fx.fonts);
    const Pt size = 10.0f;
    inl::Paragraph para = inl::Paragraph::plain(
        u"吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。ABC def", fx.style(size));
    para.style.firstLineIndent = 1.0f;
    para.annotations.push_back(inl::Annotation::ruby(0, 2, u"わがはい"));
    const inl::ConstantLineShape shape(120.0f);

    for (WritingMode wm : {WritingMode::HorizontalTb, WritingMode::VerticalRl}) {
        const inl::ParagraphFragment frag = layouter.layout(para, wm, shape);
        REQUIRE(frag.lines.size() >= 3);
        const Point origin{100.0f, 50.0f};

        // 文字の箱: 注記（ルビ）を含まず、本文の文字と 1 対 1。送り方向に並び、隣と重ならない
        const auto boxes0 = inl::charBoxes(frag, wm, 0);
        REQUIRE(!boxes0.empty());
        size_t textChars = 0;
        for (size_t i = frag.lines[0].charStart; i < frag.lines[0].charEnd;) {
            size_t len = 1; text::codePointAt(*frag.text, i, len); i += len; ++textChars;
        }
        CHECK(boxes0.size() == textChars);
        CHECK(boxes0.front().charIndex == frag.lines[0].charStart);
        for (size_t i = 0; i + 1 < boxes0.size(); ++i) {
            CHECK(boxes0[i].inlineEnd <= boxes0[i + 1].inlineStart + 0.01f);
            CHECK(boxes0[i].charIndex < boxes0[i + 1].charIndex);
        }
        // 箱の block 範囲は em box
        CHECK(boxes0[0].blockMax - boxes0[0].blockMin == doctest::Approx(size));

        // 描いたグリフの位置は箱の中（ペン位置は箱の始端＋boxBefore）
        dl::DisplayList out;
        inl::emitParagraph(out, frag, wm, origin);
        const Point lo0 = inl::lineOriginOf(frag, wm, origin, 0);
        for (const dl::Item& item : out.items) {
            const auto* run = std::get_if<dl::GlyphRun>(&item);
            if (!run) continue;
            if (run->size != size) continue;      // ルビのグリフは親文字群にまたがるので本文だけ見る
            for (const dl::Glyph& g : run->glyphs) {
                if (g.charIndex >= frag.lines[0].charEnd) continue;
                bool found = false;
                for (const inl::CharBox& b : boxes0) {
                    if (b.charIndex != g.charIndex) continue;
                    const Rect r = b.rect(wm, lo0);
                    // ペン位置は箱の送り方向の範囲内
                    if (wm == WritingMode::HorizontalTb) found = g.pos.x >= r.x - 0.5f && g.pos.x <= r.right() + 0.5f;
                    else found = g.pos.y >= r.y - 0.5f && g.pos.y <= r.bottom() + 0.5f;
                    if (found) break;
                }
                CHECK_MESSAGE(found, "wm=" << static_cast<int>(wm) << " char=" << g.charIndex);
            }
        }

        // 範囲 → 矩形: 1 行に収まる範囲は 1 つ、行をまたぐ範囲は行数ぶん。矩形の長さは箱の和
        const auto r1 = inl::rectsFor(frag, wm, origin, 2, 5);        // 「は猫で」
        REQUIRE(r1.size() == 1);
        const Pt len1 = (wm == WritingMode::HorizontalTb) ? r1[0].w : r1[0].h;
        CHECK(len1 == doctest::Approx(size * 3.0f).epsilon(0.05));
        const size_t l1s = frag.lines[1].charStart;
        const auto r2 = inl::rectsFor(frag, wm, origin, l1s - 2, l1s + 2);
        REQUIRE(r2.size() == 2);
        CHECK(inl::rectsFor(frag, wm, origin, 5, 5).empty());

        // ヒットテスト: 各箱の中心を突くとその文字。行の外は nullopt
        for (size_t li = 0; li < frag.lines.size(); ++li) {
            const Point lo = inl::lineOriginOf(frag, wm, origin, li);
            for (const inl::CharBox& b : inl::charBoxes(frag, wm, li)) {
                const Rect r = b.rect(wm, lo);
                const Point c{r.x + r.w * 0.5f, r.y + r.h * 0.5f};
                const auto hit = inl::hitTest(frag, wm, origin, c);
                REQUIRE(hit.has_value());
                CHECK(hit->lineIndex == li);
                CHECK(hit->charIndex == b.charIndex);
                CHECK(hit->inside);
            }
        }
        // 行頭の一字下げの余白は先頭の文字に丸める（inside = false）
        {
            const Point lo = inl::lineOriginOf(frag, wm, origin, 0);
            const Point before = toPhysical(wm, LogicalPoint{-size * 0.5f, 0.0f}, lo);
            const auto hit = inl::hitTest(frag, wm, origin, before);
            REQUIRE(hit.has_value());
            CHECK(hit->charIndex == frag.lines[0].charStart);
            CHECK(!hit->inside);
            // 行の後ろの余白は行末
            const Point after = toPhysical(wm, LogicalPoint{frag.lines[0].length + size, 0.0f}, lo);
            const auto hit2 = inl::hitTest(frag, wm, origin, after);
            REQUIRE(hit2.has_value());
            CHECK(hit2->charIndex == frag.lines[0].charEnd);
            CHECK(hit2->after);
        }
        {
            const Point far = (wm == WritingMode::HorizontalTb) ? Point{origin.x, origin.y - 100.0f}
                                                                : Point{origin.x + 100.0f, origin.y};
            CHECK(!inl::hitTest(frag, wm, origin, far).has_value());
        }

        // キャレット: 文字の始端。行末の位置は前の行の終端側。範囲外は nullopt
        {
            const auto c0 = inl::caretRect(frag, wm, origin, 3);
            REQUIRE(c0.has_value());
            const Rect b3 = boxes0[3].rect(wm, lo0);
            if (wm == WritingMode::HorizontalTb) CHECK(c0->x + c0->w * 0.5f == doctest::Approx(b3.x).epsilon(0.01));
            else CHECK(c0->y + c0->h * 0.5f == doctest::Approx(b3.y).epsilon(0.01));
            const auto cEnd = inl::caretRect(frag, wm, origin, frag.lines[0].charEnd);
            REQUIRE(cEnd.has_value());
            const Rect last = boxes0.back().rect(wm, lo0);
            if (wm == WritingMode::HorizontalTb) CHECK(cEnd->x + cEnd->w * 0.5f == doctest::Approx(last.right()).epsilon(0.01));
            else CHECK(cEnd->y + cEnd->h * 0.5f == doctest::Approx(last.bottom()).epsilon(0.01));
            CHECK(!inl::caretRect(frag, wm, origin, frag.text->size() + 1).has_value());
        }
    }
}

TEST_CASE("placeholder: occupies its box, is not drawn, and its rect can be queried") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::ParagraphLayouter layouter(fx.fonts);
    const TextStyle st = fx.style(10.0f);
    inl::Paragraph para;
    para.runs.push_back(inl::InlineRun{u"吾輩は", st});
    para.addPlaceholder(Size{30.0f, 24.0f}, st, "widget");
    para.runs.push_back(inl::InlineRun{u"猫である", st});
    const inl::ConstantLineShape shape(200.0f);

    for (WritingMode wm : {WritingMode::HorizontalTb, WritingMode::VerticalRl}) {
        const inl::ParagraphFragment frag = layouter.layout(para, wm, shape);
        REQUIRE(frag.lines.size() == 1);
        const Point origin{10.0f, 40.0f};
        // 送りは箱の大きさ（横組み: 幅、縦組み: 高さ）
        const Pt adv = (wm == WritingMode::HorizontalTb) ? 30.0f : 24.0f;
        CHECK(frag.lines[0].naturalLength == doctest::Approx(10.0f * 7.0f + adv).epsilon(0.02));
        // 行送りの箱から出るぶんは行送りが広がる（画像と同じ）
        const Pt across = (wm == WritingMode::HorizontalTb) ? 24.0f : 30.0f;
        CHECK(frag.lines[0].extraBefore + frag.lines[0].extraAfter ==
              doctest::Approx(std::max(0.0f, across - frag.linePitch)));

        dl::DisplayList out;
        inl::emitParagraph(out, frag, wm, origin);
        size_t glyphs = 0;
        for (const dl::Item& item : out.items) {
            CHECK(!std::holds_alternative<dl::ImageItem>(item));
            if (const auto* run = std::get_if<dl::GlyphRun>(&item)) glyphs += run->glyphs.size();
        }
        CHECK(glyphs == 7);

        const auto prs = inl::placeholderRects(frag, wm, origin);
        REQUIRE(prs.size() == 1);
        CHECK(prs[0].id == "widget");
        CHECK(prs[0].charIndex == 3);
        CHECK(prs[0].rect.w == doctest::Approx(30.0f));
        CHECK(prs[0].rect.h == doctest::Approx(24.0f));
        // 中心が行の中心線に載る
        const Point lo = inl::lineOriginOf(frag, wm, origin, 0);
        if (wm == WritingMode::HorizontalTb) CHECK(prs[0].rect.y + prs[0].rect.h * 0.5f == doctest::Approx(lo.y));
        else CHECK(prs[0].rect.x + prs[0].rect.w * 0.5f == doctest::Approx(lo.x));
        // charBoxes にも placeholder として出る
        const auto boxes = inl::charBoxes(frag, wm, 0);
        REQUIRE(boxes.size() == 8);
        CHECK(boxes[3].placeholder);
        CHECK(!boxes[3].image);
    }
}

TEST_CASE("emitParagraph maxChars draws only the text before the position, keeping the full layout") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::ParagraphLayouter layouter(fx.fonts);
    TextStyle st = fx.style(10.0f);
    st.underline = TextDecoration{};
    inl::Paragraph para = inl::Paragraph::plain(u"吾輩は猫である。名前はまだ無い。どこで生れたか。", st);
    para.annotations.push_back(inl::Annotation::ruby(3, 4, u"ねこ"));
    const inl::ConstantLineShape shape(100.0f);
    const inl::ParagraphFragment frag = layouter.layout(para, WritingMode::HorizontalTb, shape);
    REQUIRE(frag.lines.size() >= 2);

    auto countGlyphs = [](const dl::DisplayList& out, uint32_t& maxChar) {
        size_t n = 0;
        maxChar = 0;
        for (const dl::Item& item : out.items) {
            if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
                n += run->glyphs.size();
                for (const dl::Glyph& g : run->glyphs) maxChar = std::max(maxChar, g.charIndex);
            }
        }
        return n;
    };
    dl::DisplayList full, part, none;
    inl::emitParagraph(full, frag, WritingMode::HorizontalTb, Point{0, 20});
    inl::emitParagraph(part, frag, WritingMode::HorizontalTb, Point{0, 20}, 0, 5);
    inl::emitParagraph(none, frag, WritingMode::HorizontalTb, Point{0, 20}, 0, 0);
    uint32_t mFull = 0, mPart = 0, mNone = 0;
    const size_t nFull = countGlyphs(full, mFull);
    const size_t nPart = countGlyphs(part, mPart);
    const size_t nNone = countGlyphs(none, mNone);
    CHECK(nFull > nPart);
    CHECK(nPart == 5 + 2);       // 本文 5 文字＋「猫」のルビ 2 文字（親文字に従う）
    CHECK(mPart == 4);
    CHECK(nNone == 0);
    // 下線も途中までの範囲だけ（1 本、5 文字ぶん）
    size_t rects = 0;
    Pt w = 0.0f;
    for (const dl::Item& item : part.items) {
        if (const auto* r = std::get_if<dl::RectItem>(&item)) { ++rects; w = r->rect.w; }
    }
    CHECK(rects == 1);
    CHECK(w == doctest::Approx(50.0f).epsilon(0.02));
}

TEST_CASE("measureText returns advance, extents and cluster count for one unbroken line") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    const TextStyle st = fx.style(10.0f);
    const inl::TextMetrics h = inl::measureText(fx.fonts, u"吾輩は猫", st, WritingMode::HorizontalTb);
    CHECK(h.advance == doctest::Approx(40.0f));
    CHECK(h.clusterCount == 4);
    CHECK(h.glyphCount == 4);
    CHECK(h.ascent > 0.0f);
    CHECK(h.descent > 0.0f);
    CHECK(h.ascent + h.descent == doctest::Approx(10.0f).epsilon(0.1));
    const inl::TextMetrics v = inl::measureText(fx.fonts, u"吾輩は猫", st, WritingMode::VerticalRl);
    CHECK(v.advance == doctest::Approx(40.0f));
    const inl::TextMetrics latin = inl::measureText(fx.fonts, u"Hello", st, WritingMode::HorizontalTb);
    CHECK(latin.advance > 15.0f);
    CHECK(latin.advance < 40.0f);
    CHECK(latin.clusterCount == 5);
    CHECK(inl::measureText(fx.fonts, u"", st).clusterCount == 0);
}
