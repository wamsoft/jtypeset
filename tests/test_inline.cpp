#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <variant>

#include "typeset/backend/pdf_writer.hpp"
#include "typeset/font/font_set.hpp"
#include "typeset/inl/item_builder.hpp"
#include "typeset/inl/paragraph.hpp"
#include "typeset/inl/shaper.hpp"
#include "typeset/inl/tag_parser.hpp"
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
                if (pi->fill && (pi->fill->solid().r != pi->fill->solid().g || pi->fill->solid().g != pi->fill->solid().b)) ++colored;
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

TEST_CASE("OpenType features: palt tightens punctuation and disables the JLReq body compression for that run") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    const TextStyle plain = fx.style(10.0f);
    TextStyle palt = plain;
    palt.features = {"palt"};
    CHECK(palt.hasProportionalFeature());
    TextStyle off = plain;
    off.features = {"-palt", "liga=0"};
    CHECK(!off.hasProportionalFeature());

    // シェイピング: palt で読点・句点・括弧の送りが 1em より短くなる
    const inl::TextMetrics a = inl::measureText(fx.fonts, u"「猫、犬。」", plain);
    const inl::TextMetrics b = inl::measureText(fx.fonts, u"「猫、犬。」", palt);
    CHECK(a.advance == doctest::Approx(60.0f));
    CHECK(b.advance < 55.0f);
    CHECK(b.clusterCount == 6);

    // 組版: palt の run は JLReq の半角化・約物のアキを使わず、シェイパーの送りの和がそのまま行長になる
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(300.0f);
    inl::Paragraph pp = inl::Paragraph::plain(u"「猫、犬。」", palt);
    pp.style.align = Align::Start;
    pp.style.lineBreak.justify = false;
    const inl::ParagraphFragment fp = layouter.layout(pp, WritingMode::HorizontalTb, shape);
    REQUIRE(fp.lines.size() == 1);
    CHECK(fp.lines[0].naturalLength == doctest::Approx(b.advance).epsilon(0.01));
    // 通常の run は JLReq どおり（始め括弧の半角化で 60 より短く、palt より長い）
    inl::Paragraph pn = inl::Paragraph::plain(u"「猫、犬。」", plain);
    pn.style.align = Align::Start;
    pn.style.lineBreak.justify = false;
    const inl::ParagraphFragment fn = layouter.layout(pn, WritingMode::HorizontalTb, shape);
    CHECK(fn.lines[0].naturalLength < 60.0f);
    CHECK(fn.lines[0].naturalLength > fp.lines[0].naturalLength);
}

TEST_CASE("variable fonts: weight and axis values select an instance, no fake bold, PDF pins the axes") {
    font::FontSet fonts;
    auto vf = fonts.loadFile("data/NotoSans-Variable.ttf", "sans-var");
    if (!vf) { MESSAGE("variable font not found; skipping"); return; }
    REQUIRE(!vf->descriptor().axes.empty());

    FontSpec regular;
    regular.family = {"sans-var"};
    FontSpec bold = regular;
    bold.weight = 700;
    FontSpec narrow = regular;
    narrow.variations["wdth"] = 62.5f;

    auto fr = fonts.resolve(regular, U'H');
    auto fb = fonts.resolve(bold, U'H');
    auto fn = fonts.resolve(narrow, U'H');
    REQUIRE(static_cast<bool>(fr));
    REQUIRE(static_cast<bool>(fb));
    REQUIRE(static_cast<bool>(fn));
    CHECK((fr == vf));                    // 既定値ならそのまま
    CHECK((fb != fr));
    CHECK((fn != fr));
    CHECK((fn != fb));
    CHECK((fonts.resolve(bold, U'H') == fb));   // 同じ座標は同じインスタンス
    CHECK(font::effectiveWeight(*fb) == 700);
    CHECK(font::effectiveWeight(*fr) == 400);
    // variations() は全軸の現在値（wdth は既定のまま、wght が 700）
    bool sawWght = false;
    for (const glyphware::VarCoord& c : fb->variations()) {
        if (c.tag == font::makeTag("wght")) { sawWght = true; CHECK(c.value == doctest::Approx(700.0f)); }
    }
    CHECK(sawWght);

    // 太いほど、細いほど送りが変わる。フェイクボールドは掛からない
    TextStyle sr; sr.font = regular; sr.size = 20.0f;
    TextStyle sb; sb.font = bold; sb.size = 20.0f;
    TextStyle sn; sn.font = narrow; sn.size = 20.0f;
    const inl::TextMetrics mr = inl::measureText(fonts, u"Hamburg", sr);
    const inl::TextMetrics mb = inl::measureText(fonts, u"Hamburg", sb);
    const inl::TextMetrics mn = inl::measureText(fonts, u"Hamburg", sn);
    CHECK(mb.advance > mr.advance * 1.02f);
    CHECK(mn.advance < mr.advance * 0.9f);
    const inl::ShapedText shapedBold = inl::shapeText(u"H", sb, fonts, WritingMode::HorizontalTb);
    REQUIRE(!shapedBold.glyphs.empty());
    CHECK(shapedBold.glyphs[0].embolden == 0.0f);
    CHECK((shapedBold.glyphs[0].face == fb));

    // PDF: 2 つのインスタンスが別フォントとして、軸を固定して埋め込まれる
    inl::ParagraphLayouter layouter(fonts);
    inl::Paragraph para;
    para.runs.push_back(inl::InlineRun{u"Regular ", sr});
    para.runs.push_back(inl::InlineRun{u"Bold ", sb});
    para.runs.push_back(inl::InlineRun{u"Narrow", sn});
    const inl::ConstantLineShape shape(400.0f);
    const inl::ParagraphFragment frag = layouter.layout(para, WritingMode::HorizontalTb, shape);
    dl::DisplayList list;
    list.page = Size{400, 100};
    inl::emitParagraph(list, frag, WritingMode::HorizontalTb, Point{10, 50});
    backend::PdfWriter pdf;
    pdf.addPage(list);
    const std::string path = "build/test_variable.pdf";
    REQUIRE(pdf.save(path));
    CHECK(pdf.warnings().empty());
    std::ifstream in(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("wght700") != std::string::npos);
    CHECK(bytes.find("wdth6") != std::string::npos);   // 62.5 → 四捨五入で 63
    size_t fontFiles = 0;
    for (size_t p = bytes.find("/FontFile"); p != std::string::npos; p = bytes.find("/FontFile", p + 1)) ++fontFiles;
    CHECK(fontFiles == 3);
}

TEST_CASE("bidi: RTL runs are reordered visually within the line, in horizontal and vertical text") {
    font::FontSet fonts;
    auto latin = fonts.loadFile("data/NotoSans-Regular.ttf", "sans");
    auto hebrew = fonts.loadFile("data/NotoSansHebrew-Regular.ttf", "hebrew");
    auto arabic = fonts.loadFile("data/NotoSansArabic-Regular.ttf", "arabic");
    if (!latin || !hebrew || !arabic) { MESSAGE("fonts not found; skipping"); return; }
    TextStyle st;
    st.font.family = {"sans", "hebrew", "arabic"};
    st.size = 10.0f;
    st.language = "en";
    inl::ParagraphLayouter layouter(fonts);
    const inl::ConstantLineShape shape(400.0f);

    // 欧文の中のヘブライ語: "abc " + שלום + " def"
    const std::u16string text = u"abc שלום def";
    inl::Paragraph para = inl::Paragraph::plain(text, st);
    para.style.align = Align::Start;
    para.style.lineBreak.justify = false;
    for (WritingMode wm : {WritingMode::HorizontalTb, WritingMode::VerticalRl}) {
        const inl::ParagraphFragment frag = layouter.layout(para, wm, shape);
        REQUIRE(frag.lines.size() == 1);
        const auto boxes = inl::charBoxes(frag, wm, 0);   // 送り方向の順（視覚順）。空白はグルーなので箱に無い
        REQUIRE(boxes.size() == 10);
        // 視覚順: a b c ␠ ם ו ל ש ␠ d e f
        const std::vector<uint32_t> expected{0, 1, 2, 7, 6, 5, 4, 9, 10, 11};
        for (size_t i = 0; i < expected.size(); ++i) CHECK(boxes[i].charIndex == expected[i]);
        // 箱は重ならずに並ぶ（空白のぶんだけ隙間）
        for (size_t i = 0; i + 1 < boxes.size(); ++i) CHECK(boxes[i].inlineEnd <= boxes[i + 1].inlineStart + 0.01f);
        // 行長は変わらない
        CHECK(boxes.back().inlineEnd == doctest::Approx(frag.lines[0].naturalLength).epsilon(0.01));
        // 描いたグリフも同じ順（ヘブライ文字のペン位置は後ろの文字ほど手前）
        dl::DisplayList out;
        inl::emitParagraph(out, frag, wm, Point{0, 0});
        Pt posOf4 = 0, posOf7 = 0;
        for (const dl::Item& item : out.items) {
            if (const auto* run = std::get_if<dl::GlyphRun>(&item)) {
                for (const dl::Glyph& g : run->glyphs) {
                    const Pt p = (wm == WritingMode::HorizontalTb) ? g.pos.x : g.pos.y;
                    if (g.charIndex == 4) posOf4 = p;
                    if (g.charIndex == 7) posOf7 = p;
                }
            }
        }
        CHECK(posOf4 > posOf7);
    }

    // 空白（グルー）は自分のレベル（両側の run の間では段落レベル）で並ぶ: 各 run の前後に 1 つずつ
    {
        inl::Paragraph mixed = inl::Paragraph::plain(u"ab גד ef", st);
        mixed.style.align = Align::Start;
        mixed.style.lineBreak.justify = false;
        mixed.style.direction = Direction::Ltr;
        const inl::ParagraphFragment frag = layouter.layout(mixed, WritingMode::HorizontalTb, shape);
        const auto boxes = inl::charBoxes(frag, WritingMode::HorizontalTb, 0);
        REQUIRE(boxes.size() == 6);
        const std::vector<uint32_t> expected{0, 1, 4, 3, 6, 7};
        for (size_t i = 0; i < expected.size(); ++i) CHECK(boxes[i].charIndex == expected[i]);
        const Pt space = inl::measureText(fonts, u" ", st).advance;
        CHECK(boxes[2].inlineStart - boxes[1].inlineEnd == doctest::Approx(space).epsilon(0.02));
        CHECK(boxes[4].inlineStart - boxes[3].inlineEnd == doctest::Approx(space).epsilon(0.02));
        CHECK(boxes[3].inlineStart == doctest::Approx(boxes[2].inlineEnd).epsilon(0.01));
    }

    // RTL の段落（Auto で最初の強い文字がヘブライ文字）: 行頭揃えは右揃えになり、最初の文字が終端側
    {
        inl::Paragraph rtl = inl::Paragraph::plain(u"שלום abc", st);
        rtl.style.align = Align::Start;
        rtl.style.lineBreak.justify = false;
        rtl.style.firstLineIndent = 1.0f;
        const inl::ParagraphFragment frag = layouter.layout(rtl, WritingMode::HorizontalTb, shape);
        REQUIRE(frag.lines.size() == 1);
        const auto boxes = inl::charBoxes(frag, WritingMode::HorizontalTb, 0);
        REQUIRE(boxes.size() == 7);
        // 視覚順: a b c ␠ ם ו ל ש
        CHECK(boxes[0].charIndex == 5);
        CHECK(boxes.back().charIndex == 0);
        // 右揃え: 行頭のずれ（indent）は 400 - 一字下げ 10 - 自然長
        CHECK(frag.lines[0].indent == doctest::Approx(400.0f - 10.0f - frag.lines[0].naturalLength).epsilon(0.01));
        // 明示の LTR なら左揃えのまま（先頭の一字下げ）
        rtl.style.direction = Direction::Ltr;
        const inl::ParagraphFragment fl = layouter.layout(rtl, WritingMode::HorizontalTb, shape);
        CHECK(fl.lines[0].indent == doctest::Approx(10.0f));
        const auto bl = inl::charBoxes(fl, WritingMode::HorizontalTb, 0);
        CHECK(bl[0].charIndex == 3);     // ヘブライ語の run は反転したまま、run 全体は左に
        CHECK(bl.back().charIndex == 7);
    }

    // アラビア語: 文脈字形が付き（単独形と別のグリフ）、語間だけで折り返す
    {
        const std::u16string ar = u"العربية لغة جميلة "
                                  u"ومفيدة للقراءة";
        const inl::TextMetrics joined = inl::measureText(fonts, u"لع", st);   // ل + ع（結合）
        const inl::TextMetrics l = inl::measureText(fonts, u"ل", st);
        const inl::TextMetrics a = inl::measureText(fonts, u"ع", st);
        CHECK(joined.advance < l.advance + a.advance - 0.5f);   // 頭字形・尾字形は単独形より詰まる
        inl::Paragraph pa = inl::Paragraph::plain(ar, st);
        pa.style.align = Align::Start;
        pa.style.lineBreak.justify = false;
        const inl::ConstantLineShape narrow(60.0f);
        const inl::ParagraphFragment frag = layouter.layout(pa, WritingMode::HorizontalTb, narrow);
        REQUIRE(frag.lines.size() >= 2);
        for (size_t i = 0; i + 1 < frag.lines.size(); ++i) {
            CHECK(ar[frag.lines[i].charEnd] == u' ');                  // 語の切れ目で折り返す
            CHECK(frag.lines[i + 1].charStart > frag.lines[i].charEnd);
            CHECK(frag.lines[i].length <= 60.0f + 0.01f);
        }
        CHECK(frag.complete);
        // RTL の段落: 各行の右端から始まる（最初の文字の箱が一番右）
        const auto boxes = inl::charBoxes(frag, WritingMode::HorizontalTb, 0);
        REQUIRE(!boxes.empty());
        CHECK(boxes.back().charIndex == frag.lines[0].charStart);
    }
}

TEST_CASE("wrap modes: char breaks inside a word, word keeps kana together, none overflows") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(60.0f);
    const std::u16string text = u"あいうえお internationalization かきくけこ";

    auto run = [&](WrapMode w) {
        inl::Paragraph p = inl::Paragraph::plain(text, fx.style(10.0f));
        p.style.align = Align::Start;
        p.style.lineBreak.justify = false;
        p.style.lineBreak.wrap = w;
        return layouter.layout(p, WritingMode::HorizontalTb, shape);
    };

    // Mixed（既定）: 和文は字ごと、欧文は語ごと。長い欧単語は行長を超える
    const inl::ParagraphFragment mixed = run(WrapMode::Mixed);
    REQUIRE(mixed.lines.size() >= 3);
    Pt longestMixed = 0.0f;
    for (const inl::LineBox& l : mixed.lines) longestMixed = std::max(longestMixed, l.naturalLength);
    CHECK(longestMixed > 60.0f);

    // Char: 欧単語の途中でも切るので、どの行も行長に収まる
    const inl::ParagraphFragment ch = run(WrapMode::Char);
    for (const inl::LineBox& l : ch.lines) CHECK(l.naturalLength <= 60.0f + 0.01f);
    CHECK(ch.lines.size() >= mixed.lines.size());

    // Word: 和文も語（UAX #14）でしか切らないので、仮名の連続は切れない
    const inl::ParagraphFragment wd = run(WrapMode::Word);
    for (const inl::LineBox& l : wd.lines) {
        if (l.charEnd < text.size() && l.charEnd > 0) {
            // 行末が仮名同士の境目になっていない
            const char32_t a = text::codePointAt(text, l.charEnd - 1);
            const char32_t b = text::codePointAt(text, l.charEnd);
            const bool bothKana = text::getCharClass(a) == text::CharClass::Hiragana &&
                                  text::getCharClass(b) == text::CharClass::Hiragana;
            CHECK(!bothKana);
        }
    }

    // None: 改行以外で切らない（1 行のまま）
    const inl::ParagraphFragment none = run(WrapMode::None);
    CHECK(none.lines.size() == 1);
    CHECK(none.lines[0].charEnd == text.size());
    CHECK(none.lines[0].naturalLength > 60.0f);
}

TEST_CASE("ellipsis: the last line is truncated with the ellipsis when maxLines cuts the text") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(60.0f);
    inl::Paragraph p = inl::Paragraph::plain(u"吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。", fx.style(10.0f));
    p.style.align = Align::Start;
    p.style.lineBreak.justify = false;

    const inl::ParagraphFragment cut = layouter.layout(p, WritingMode::HorizontalTb, shape, 0, 2);
    REQUIRE(cut.lines.size() == 2);
    CHECK(!cut.complete);

    p.style.ellipsis = u"…";
    const inl::ParagraphFragment ell = layouter.layout(p, WritingMode::HorizontalTb, shape, 0, 2);
    REQUIRE(ell.lines.size() == 2);
    CHECK(!ell.complete);
    // 最後の行は行長に収まり、末尾に「…」のグリフが増えている
    CHECK(ell.lines[1].naturalLength <= 60.0f + 0.01f);
    CHECK(ell.lines[1].glyphs.size() >= 2);
    const inl::PlacedGlyph& last = ell.lines[1].glyphs.back();
    CHECK(last.charIndex == std::numeric_limits<uint32_t>::max());
    // 本文は「…」のぶん短くなる
    CHECK(ell.lines[1].charEnd <= cut.lines[1].charEnd);
    // 1 行目は変わらない
    CHECK(ell.lines[0].charEnd == cut.lines[0].charEnd);
    // 全部組めるときは足さない
    const inl::ParagraphFragment full = layouter.layout(p, WritingMode::HorizontalTb, shape);
    CHECK(full.complete);
    for (const inl::PlacedGlyph& g : full.lines.back().glyphs) {
        CHECK(g.charIndex != std::numeric_limits<uint32_t>::max());
    }
}

TEST_CASE("kinsoku levels and custom characters change where a line may break") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::ParagraphLayouter layouter(fx.fonts);
    // 行長 30pt（3 字）。4 字目が長音「ー」= 行頭禁則（弱い）
    const inl::ConstantLineShape shape(30.0f);
    auto lines = [&](KinsokuLevel level, const std::u16string& text) {
        inl::Paragraph p = inl::Paragraph::plain(text, fx.style(10.0f));
        p.style.align = Align::Start;
        p.style.lineBreak.justify = false;
        p.style.spacing.kinsoku = level;
        p.style.spacing.kanjiSkipStretch = 0.0f;
        return layouter.layout(p, WritingMode::HorizontalTb, shape);
    };
    // Strict: 「ー」を行頭に置けないので 3 字目から追い出す
    const inl::ParagraphFragment strict = lines(KinsokuLevel::Strict, u"アイウーエオカキ");
    CHECK(strict.lines[0].charEnd == 2);
    // Normal: 弱い禁則なので、詰まっていれば行頭に来てよい
    const inl::ParagraphFragment normal = lines(KinsokuLevel::Normal, u"アイウーエオカキ");
    CHECK(normal.lines[0].charEnd == 3);

    // Loose: 句点も弱い禁則
    const inl::ParagraphFragment strictStop = lines(KinsokuLevel::Strict, u"アイウ。エオカキ");
    CHECK(strictStop.lines[0].charEnd == 2);
    const inl::ParagraphFragment loose = lines(KinsokuLevel::Loose, u"アイウ。エオカキ");
    CHECK(loose.lines[0].charEnd == 3);
    // Normal では句点は強いまま
    const inl::ParagraphFragment normalStop = lines(KinsokuLevel::Normal, u"アイウ。エオカキ");
    CHECK(normalStop.lines[0].charEnd == 2);

    // 追加・除外リスト: クラスより優先する
    {
        inl::Paragraph p = inl::Paragraph::plain(u"アイウエオカキ", fx.style(10.0f));
        p.style.align = Align::Start;
        p.style.lineBreak.justify = false;
        p.style.spacing.kanjiSkipStretch = 0.0f;
        p.style.spacing.lineStartProhibited = u"エ";     // 「エ」を行頭に置かない
        const inl::ParagraphFragment f = layouter.layout(p, WritingMode::HorizontalTb, shape);
        CHECK(f.lines[0].charEnd == 2);
    }
    {
        inl::Paragraph p = inl::Paragraph::plain(u"アイウ。エオカキ", fx.style(10.0f));
        p.style.align = Align::Start;
        p.style.lineBreak.justify = false;
        p.style.spacing.kanjiSkipStretch = 0.0f;
        p.style.spacing.lineStartAllowed = u"。";        // 句点を行頭禁則から外す
        const inl::ParagraphFragment f = layouter.layout(p, WritingMode::HorizontalTb, shape);
        CHECK(f.lines[0].charEnd == 3);
    }
    {
        inl::Paragraph p = inl::Paragraph::plain(u"アイウエオカキ", fx.style(10.0f));
        p.style.align = Align::Start;
        p.style.lineBreak.justify = false;
        p.style.spacing.kanjiSkipStretch = 0.0f;
        p.style.spacing.lineEndProhibited = u"ウ";       // 「ウ」を行末に置かない
        const inl::ParagraphFragment f = layouter.layout(p, WritingMode::HorizontalTb, shape);
        CHECK(f.lines[0].charEnd == 2);
    }
}

TEST_CASE("hanging indent and mid-paragraph indent shift the line heads") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(100.0f);
    const std::u16string text = u"吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。";

    // ぶら下げインデント: 1 行目は一字下げ、2 行目以降は 2em
    inl::Paragraph p = inl::Paragraph::plain(text, fx.style(10.0f));
    p.style.firstLineIndent = 1.0f;
    p.style.hangingIndent = 2.0f;
    const inl::ParagraphFragment frag = layouter.layout(p, WritingMode::HorizontalTb, shape);
    REQUIRE(frag.lines.size() >= 3);
    CHECK(frag.lines[0].indent == doctest::Approx(10.0f));
    CHECK(frag.lines[1].indent == doctest::Approx(20.0f));
    CHECK(frag.lines[2].indent == doctest::Approx(20.0f));
    for (size_t i = 0; i + 1 < frag.lines.size(); ++i) {
        CHECK(frag.lines[i].length == doctest::Approx(100.0f - frag.lines[i].indent).epsilon(0.01));
    }

    // 途中からの字下げ（Indent 注記）: 指定範囲に行頭がある行だけ下がる
    inl::Paragraph q = inl::Paragraph::plain(text, fx.style(10.0f));
    q.style.firstLineIndent = 0.0f;
    const inl::ParagraphFragment base = layouter.layout(q, WritingMode::HorizontalTb, shape);
    REQUIRE(base.lines.size() >= 3);
    q.annotations.push_back(inl::Annotation::indent(base.lines[1].charStart, text.size(), 3.0f));
    const inl::ParagraphFragment ind = layouter.layout(q, WritingMode::HorizontalTb, shape);
    REQUIRE(ind.lines.size() >= 3);
    CHECK(ind.lines[0].indent == doctest::Approx(0.0f));
    CHECK(ind.lines[1].indent == doctest::Approx(30.0f));
    CHECK(ind.lines[1].length == doctest::Approx(70.0f).epsilon(0.02));
}

TEST_CASE("tab stops and moveTo place text at absolute inline positions") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(400.0f);
    const TextStyle st = fx.style(10.0f);

    // 既定のタブ（tabWidth × em ごとの左揃え）
    {
        inl::Paragraph p = inl::Paragraph::plain(u"あ\tい\tう", st);
        p.style.align = Align::Start;
        p.style.lineBreak.justify = false;
        p.style.tabWidth = 4;    // 40pt ごと
        const inl::ParagraphFragment f = layouter.layout(p, WritingMode::HorizontalTb, shape);
        REQUIRE(f.lines.size() == 1);
        const auto boxes = inl::charBoxes(f, WritingMode::HorizontalTb, 0);
        REQUIRE(boxes.size() >= 3);
        CHECK(boxes[0].inlineStart == doctest::Approx(0.0f));
        // 「い」は 40pt、「う」は 80pt から
        Pt posI = -1.0f, posU = -1.0f;
        for (const inl::CharBox& b : boxes) {
            if (b.charIndex == 2) posI = b.inlineStart;
            if (b.charIndex == 4) posU = b.inlineStart;
        }
        CHECK(posI == doctest::Approx(40.0f));
        CHECK(posU == doctest::Approx(80.0f));
    }
    // タブストップの指定（左・右・中央揃え）
    {
        inl::Paragraph p = inl::Paragraph::plain(u"あ\tいろは\tにほ", st);
        p.style.align = Align::Start;
        p.style.lineBreak.justify = false;
        p.style.tabStops = {TabStop{50.0f, TabAlign::Left}, TabStop{200.0f, TabAlign::Right}};
        const inl::ParagraphFragment f = layouter.layout(p, WritingMode::HorizontalTb, shape);
        REQUIRE(f.lines.size() == 1);
        const auto boxes = inl::charBoxes(f, WritingMode::HorizontalTb, 0);
        Pt posI = -1.0f;
        Pt endLast = 0.0f;
        for (const inl::CharBox& b : boxes) {
            if (b.charIndex == 2) posI = b.inlineStart;
            endLast = std::max(endLast, b.inlineEnd);
        }
        CHECK(posI == doctest::Approx(50.0f));
        CHECK(endLast == doctest::Approx(200.0f).epsilon(0.02));   // 右揃えタブ: 末尾が 200pt
    }
    // MoveTo: 指定位置から始める。既に超えていれば何もしない
    {
        inl::Paragraph p = inl::Paragraph::plain(u"あいうえお", st);
        p.style.align = Align::Start;
        p.style.lineBreak.justify = false;
        p.annotations.push_back(inl::Annotation::moveTo(2, 100.0f));
        const inl::ParagraphFragment f = layouter.layout(p, WritingMode::HorizontalTb, shape);
        const auto boxes = inl::charBoxes(f, WritingMode::HorizontalTb, 0);
        Pt posU = -1.0f;
        for (const inl::CharBox& b : boxes) if (b.charIndex == 2) posU = b.inlineStart;
        CHECK(posU == doctest::Approx(100.0f));
    }
    {
        inl::Paragraph p = inl::Paragraph::plain(u"あいうえお", st);
        p.style.align = Align::Start;
        p.style.lineBreak.justify = false;
        p.annotations.push_back(inl::Annotation::moveTo(4, 5.0f));   // 既に 40pt なので無視
        const inl::ParagraphFragment f = layouter.layout(p, WritingMode::HorizontalTb, shape);
        const auto boxes = inl::charBoxes(f, WritingMode::HorizontalTb, 0);
        Pt posO = -1.0f;
        for (const inl::CharBox& b : boxes) if (b.charIndex == 4) posO = b.inlineStart;
        CHECK(posO == doctest::Approx(40.0f));
    }
}

TEST_CASE("fitParagraph shrinks the text until it fits the line count, originInBox aligns it in a box") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(100.0f);
    inl::Paragraph p = inl::Paragraph::plain(
        u"吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。", fx.style(10.0f));

    // そのままなら 4 行。3 行に収める
    const inl::ParagraphFragment plain = layouter.layout(p, WritingMode::HorizontalTb, shape);
    REQUIRE(plain.lines.size() >= 4);
    const inl::FitResult fit = inl::fitParagraph(layouter, p, WritingMode::HorizontalTb, shape, 3);
    CHECK(fit.fits);
    CHECK(fit.scale < 1.0f);
    CHECK(fit.fragment.complete);
    CHECK(fit.fragment.lines.size() <= 3);
    // 収まるなら縮めない
    const inl::FitResult noShrink = inl::fitParagraph(layouter, p, WritingMode::HorizontalTb, shape, 10);
    CHECK(noShrink.scale == 1.0f);
    CHECK(noShrink.fits);
    // 縮めても無理なら fits = false
    const inl::FitResult impossible = inl::fitParagraph(layouter, p, WritingMode::HorizontalTb, shape, 1);
    CHECK(!impossible.fits);

    // 箱の中での天地・左右揃え
    const Rect box{10.0f, 20.0f, 100.0f, 200.0f};
    const inl::ParagraphFragment frag = layouter.layout(p, WritingMode::HorizontalTb, shape);
    const Pt extent = frag.blockExtent();
    const Point top = inl::originInBox(frag, WritingMode::HorizontalTb, box, BlockAlign::Start);
    const Point mid = inl::originInBox(frag, WritingMode::HorizontalTb, box, BlockAlign::Center);
    const Point bot = inl::originInBox(frag, WritingMode::HorizontalTb, box, BlockAlign::End);
    CHECK(top.x == doctest::Approx(10.0f));
    CHECK(top.y == doctest::Approx(20.0f + frag.linePitch * 0.5f));
    CHECK(mid.y == doctest::Approx(20.0f + (200.0f - extent) * 0.5f + frag.linePitch * 0.5f));
    CHECK(bot.y == doctest::Approx(20.0f + 200.0f - extent + frag.linePitch * 0.5f));
    // 縦組みは列が右から
    const inl::ParagraphFragment vf = layouter.layout(p, WritingMode::VerticalRl, shape);
    const Point v = inl::originInBox(vf, WritingMode::VerticalRl, box, BlockAlign::Start);
    CHECK(v.x == doctest::Approx(box.right() - vf.linePitch * 0.5f));
    CHECK(v.y == doctest::Approx(20.0f));
}

TEST_CASE("ruby and emphasis offset move the annotation away from the parent characters") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(200.0f);
    const TextStyle st = fx.style(10.0f);

    auto rubyBlock = [&](float offset, WritingMode wm) {
        inl::Paragraph p = inl::Paragraph::plain(u"吾輩は猫", st);
        inl::Annotation a = inl::Annotation::ruby(0, 2, u"わがはい");
        a.offset = offset;
        p.annotations.push_back(a);
        const inl::ParagraphFragment f = layouter.layout(p, wm, shape);
        Pt extreme = 0.0f;
        for (const inl::PlacedGlyph& g : f.lines[0].glyphs) {
            if (!g.annotation) continue;
            extreme = isVertical(wm) ? std::max(extreme, g.block) : std::min(extreme, g.block);
        }
        return std::make_pair(extreme, f.lines[0].blockMax - f.lines[0].blockMin);
    };
    // 横組み: ルビは上（block 負）。offset で更に上へ、行の張り出しも増える
    const auto h0 = rubyBlock(0.0f, WritingMode::HorizontalTb);
    const auto h1 = rubyBlock(0.3f, WritingMode::HorizontalTb);
    CHECK(h1.first == doctest::Approx(h0.first - 3.0f));
    CHECK(h1.second == doctest::Approx(h0.second + 3.0f));
    // 縦組み: ルビは右（block 正）
    const auto v0 = rubyBlock(0.0f, WritingMode::VerticalRl);
    const auto v1 = rubyBlock(0.3f, WritingMode::VerticalRl);
    CHECK(v1.first == doctest::Approx(v0.first + 3.0f));

    // 圏点も同じ
    auto emphasisBlock = [&](float offset) {
        inl::Paragraph p = inl::Paragraph::plain(u"吾輩は猫", st);
        inl::Annotation a = inl::Annotation::emphasis(0, 2);
        a.offset = offset;
        p.annotations.push_back(a);
        const inl::ParagraphFragment f = layouter.layout(p, WritingMode::HorizontalTb, shape);
        Pt top = 0.0f;
        for (const inl::PlacedGlyph& g : f.lines[0].glyphs) if (g.annotation) top = std::min(top, g.block);
        return top;
    };
    CHECK(emphasisBlock(0.2f) == doctest::Approx(emphasisBlock(0.0f) - 2.0f));
}

TEST_CASE("emoji presentation: VS15 / Text draws an outline, VS16 / Emoji picks the color font") {
    font::FontSet fonts;
    auto latin = fonts.loadFile("data/NotoSerif-Regular.ttf", "serif");
    auto emoji = fonts.loadFile("data/Noto-COLRv1.ttf", "emoji");
    if (!latin || !emoji) { MESSAGE("fonts not found; skipping"); return; }
    // ☎ U+260E は欧文フォントにも絵文字フォントにもある（既定は字形、VS16 で絵文字）
    const char32_t cp = 0x260E;
    if (!latin->covers(cp) || !emoji->covers(cp)) { MESSAGE("phone glyph not in both fonts; skipping"); return; }

    TextStyle st;
    st.font.family = {"serif", "emoji"};
    st.size = 20.0f;
    inl::ParagraphLayouter layouter(fonts);
    const inl::ConstantLineShape shape(200.0f);

    auto faceOf = [&](const std::u16string& text, EmojiPresentation pres) {
        TextStyle s = st;
        s.emojiPresentation = pres;
        const inl::ShapedText sh = inl::shapeText(text, s, fonts, WritingMode::HorizontalTb);
        REQUIRE(!sh.glyphs.empty());
        return sh.glyphs[0].face;
    };
    // 既定は family の順（serif が先）
    CHECK((faceOf(u"\u260E", EmojiPresentation::Auto) == latin));
    // VS16 でカラー、VS15 で字形
    CHECK((faceOf(u"\u260E\uFE0F", EmojiPresentation::Auto) == emoji));
    CHECK((faceOf(u"\u260E\uFE0E", EmojiPresentation::Auto) == latin));
    // スタイルでの指定
    CHECK((faceOf(u"\u260E", EmojiPresentation::Emoji) == emoji));
    CHECK((faceOf(u"\u260E", EmojiPresentation::Text) == latin));

    // カラーフォントしか無い絵文字を Text で指定すると、カラーの層ではなくアウトラインで描く
    TextStyle only = st;
    only.font.family = {"emoji"};
    only.emojiPresentation = EmojiPresentation::Text;
    inl::Paragraph p = inl::Paragraph::plain(u"\U0001F600", only);
    const inl::ParagraphFragment f = layouter.layout(p, WritingMode::HorizontalTb, shape);
    REQUIRE(!f.lines.empty());
    REQUIRE(!f.lines[0].glyphs.empty());
    CHECK(f.lines[0].glyphs[0].monochrome);
    dl::DisplayList out;
    inl::emitParagraph(out, f, WritingMode::HorizontalTb, Point{0, 20});
    size_t runs = 0, paths = 0;
    for (const dl::Item& item : out.items) {
        if (std::holds_alternative<dl::GlyphRun>(item)) ++runs;
        if (std::holds_alternative<dl::PathItem>(item)) ++paths;
        if (const auto* g = std::get_if<dl::Group>(&item)) paths += g->children.size();
    }
    CHECK(runs == 1);       // 普通のグリフとして 1 run
    CHECK(paths == 0);      // カラーの層は出ない
    // Emoji 指定ならカラーの層（Group / Path）になる
    only.emojiPresentation = EmojiPresentation::Emoji;
    inl::Paragraph q = inl::Paragraph::plain(u"\U0001F600", only);
    const inl::ParagraphFragment fq = layouter.layout(q, WritingMode::HorizontalTb, shape);
    dl::DisplayList outq;
    inl::emitParagraph(outq, fq, WritingMode::HorizontalTb, Point{0, 20});
    size_t colorItems = 0;
    for (const dl::Item& item : outq.items) {
        if (!std::holds_alternative<dl::GlyphRun>(item)) ++colorItems;
    }
    CHECK(colorItems > 0);
}

TEST_CASE("paragraph rotation wraps the output in a rotated group") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(200.0f);
    inl::Paragraph p = inl::Paragraph::plain(u"回転する段落", fx.style(10.0f));
    p.style.rotation = 90.0f;
    const inl::ParagraphFragment f = layouter.layout(p, WritingMode::HorizontalTb, shape);
    CHECK(f.rotation == doctest::Approx(90.0f));

    dl::DisplayList out;
    const Point origin{100.0f, 50.0f};
    inl::emitParagraph(out, f, WritingMode::HorizontalTb, origin);
    REQUIRE(out.items.size() == 1);
    const auto* grp = std::get_if<dl::Group>(&out.items[0]);
    REQUIRE(grp != nullptr);
    CHECK(!grp->children.empty());
    // origin を中心に時計回り 90 度: 行頭から送り方向へ進むと y が増える
    const Point a = grp->xform.apply(Point{0.0f, 0.0f});
    const Point b = grp->xform.apply(Point{10.0f, 0.0f});
    CHECK(a.x == doctest::Approx(origin.x));
    CHECK(a.y == doctest::Approx(origin.y));
    CHECK(b.x == doctest::Approx(origin.x).epsilon(0.01));
    CHECK(b.y == doctest::Approx(origin.y + 10.0f));
    // 回転が無ければ Group で包まない
    p.style.rotation = 0.0f;
    const inl::ParagraphFragment f0 = layouter.layout(p, WritingMode::HorizontalTb, shape);
    dl::DisplayList out0;
    inl::emitParagraph(out0, f0, WritingMode::HorizontalTb, origin);
    CHECK(!std::holds_alternative<dl::Group>(out0.items[0]));
}

TEST_CASE("tag parser: richtext tags become runs, styles and annotations") {
    inl::TagParseOptions opts;
    opts.baseStyle.font.family = {"serif"};
    opts.baseStyle.size = 10.0f;
    opts.namedFamilies["gothic"] = {"sans-ja", "sans"};
    TextStyle titleStyle = opts.baseStyle;
    titleStyle.size = 20.0f;
    titleStyle.fill = Color::rgb(10, 20, 30);
    opts.namedStyles["title"] = titleStyle;

    // 本文とタグの対応、スタイルの入れ子
    {
        const inl::TagParseResult r = inl::parseTaggedText(
            u"ふつう<b>ふとじ<color value=\"#ff0000\">あか</color>もどる</b>おわり", opts);
        CHECK(r.errors.empty());
        CHECK(r.paragraph.text() == u"ふつうふとじあかもどるおわり");
        REQUIRE(r.paragraph.runs.size() == 5);
        CHECK(r.paragraph.runs[0].style.font.weight == 400);
        CHECK(r.paragraph.runs[1].style.font.weight == 700);
        CHECK(r.paragraph.runs[2].style.font.weight == 700);
        CHECK(r.paragraph.runs[2].style.fill.solid() == Color::rgb(255, 0, 0));
        CHECK(r.paragraph.runs[3].style.font.weight == 700);
        CHECK(r.paragraph.runs[3].style.fill.solid() == Color::rgb(0, 0, 0));
        CHECK(r.paragraph.runs[4].style.font.weight == 400);
    }
    // font の属性
    {
        const inl::TagParseResult r = inl::parseTaggedText(
            u"<font size=\"14\" weight=\"600\" face=\"gothic\" spacing=\"0.1\" width=\"0.8\">あ</font>", opts);
        REQUIRE(r.paragraph.runs.size() == 1);
        const TextStyle& st = r.paragraph.runs[0].style;
        CHECK(st.size == doctest::Approx(14.0f));
        CHECK(st.font.weight == 600);
        CHECK(st.font.family == std::vector<std::string>{"sans-ja", "sans"});
        CHECK(st.letterSpacing == doctest::Approx(0.1f));
        CHECK(st.scaleX == doctest::Approx(0.8f));
    }
    // 下線・打消し線・上付き・名前付きスタイル
    {
        const inl::TagParseResult r = inl::parseTaggedText(
            u"<u>した</u><s>けし</s><sup>うえ</sup><style name=\"title\">大</style>", opts);
        REQUIRE(r.paragraph.runs.size() == 4);
        CHECK(r.paragraph.runs[0].style.underline.has_value());
        CHECK(r.paragraph.runs[1].style.strikethrough.has_value());
        CHECK(r.paragraph.runs[2].style.baselineShift == doctest::Approx(opts.supOffset));
        CHECK(r.paragraph.runs[2].style.size == doctest::Approx(10.0f * opts.supScale));
        CHECK(r.paragraph.runs[3].style.size == doctest::Approx(20.0f));
    }
    // 縁取り・影・二重縁取り（add）
    {
        const inl::TagParseResult r = inl::parseTaggedText(
            u"<outline color=\"#0000ff\" width=\"2\">ふち</outline>"
            u"<shadow color=\"#808080\" x=\"1\" y=\"2\" blur=\"3\">かげ</shadow>"
            u"<outline color=\"#000000\" width=\"3\"><outline add color=\"#ffffff\" width=\"1\">二重</outline></outline>",
            opts);
        REQUIRE(r.paragraph.runs.size() == 3);
        REQUIRE(r.paragraph.runs[0].style.stroke.has_value());
        CHECK(r.paragraph.runs[0].style.stroke->color.solid() == Color::rgb(0, 0, 255));
        CHECK(r.paragraph.runs[0].style.stroke->width == doctest::Approx(2.0f));
        REQUIRE(r.paragraph.runs[1].style.shadow.has_value());
        CHECK(r.paragraph.runs[1].style.shadow->color == Color::rgb(128, 128, 128));
        CHECK(r.paragraph.runs[1].style.shadow->offset.x == doctest::Approx(1.0f));
        CHECK(r.paragraph.runs[1].style.shadow->blur == doctest::Approx(3.0f));
        // add: 層が 2 枚（外側の白 1pt が下、元の黒 3pt ＋ 塗り）
        const TextStyle& dbl = r.paragraph.runs[2].style;
        REQUIRE(dbl.layers.size() == 2);
        REQUIRE(dbl.layers[0].stroke.has_value());
        CHECK(dbl.layers[0].stroke->color.solid() == Color::rgb(255, 255, 255));
        REQUIRE(dbl.layers[1].stroke.has_value());
        CHECK(dbl.layers[1].stroke->color.solid() == Color::rgb(0, 0, 0));
    }
    // 注記
    {
        const inl::TagParseResult r = inl::parseTaggedText(
            u"<ruby text=\"かんじ\" mode=\"mono\">漢字</ruby>と<emphasis mark=\"dot\">圏点</emphasis>と"
            u"<tcy>12</tcy>と<jidori em=\"4\">字取</jidori>", opts);
        CHECK(r.paragraph.text() == u"漢字と圏点と12と字取");
        REQUIRE(r.paragraph.annotations.size() == 4);
        const auto& ruby = r.paragraph.annotations[0];
        CHECK(ruby.type == inl::AnnotationType::Ruby);
        CHECK(ruby.start == 0);
        CHECK(ruby.end == 2);
        CHECK(ruby.text == u"かんじ");
        CHECK(ruby.rubyMode == inl::RubyMode::Mono);
        CHECK(r.paragraph.annotations[1].type == inl::AnnotationType::Emphasis);
        CHECK(r.paragraph.annotations[1].mark == inl::EmphasisMark::Dot);
        CHECK(r.paragraph.annotations[1].start == 3);
        CHECK(r.paragraph.annotations[1].end == 5);
        CHECK(r.paragraph.annotations[2].type == inl::AnnotationType::TateChuYoko);
        CHECK(r.paragraph.annotations[3].type == inl::AnnotationType::Jidori);
        CHECK(r.paragraph.annotations[3].jidoriEm == doctest::Approx(4.0f));
    }
    // リンク・マーカー・プレースホルダ・改行・空白・実体参照
    {
        const inl::TagParseResult r = inl::parseTaggedText(
            u"<link name=\"a\">押す</link>ここで<keywait>待つ<br><sp width=\"3\">後"
            u"<graph name=\"icon\" width=\"12\" height=\"8\">。&lt;タグ&gt;&amp;", opts);
        REQUIRE(r.links.size() == 1);
        CHECK(r.links[0].name == "a");
        CHECK(r.links[0].start == 0);
        CHECK(r.links[0].end == 2);
        REQUIRE(r.markers.size() == 1);
        CHECK(r.markers[0].kind == "keywait");
        CHECK(r.markers[0].charIndex == 5);
        REQUIRE(r.placeholders.size() == 1);
        CHECK(r.placeholders[0].name == "icon");
        CHECK(r.placeholders[0].size.w == doctest::Approx(12.0f));
        CHECK(r.placeholders[0].size.h == doctest::Approx(8.0f));
        CHECK(r.paragraph.text() == u"押すここで待つ\n   後￼。<タグ>&");
    }
    // 未知のタグ・閉じ忘れ・閉じすぎは errors に出して読み飛ばす
    {
        const inl::TagParseResult r = inl::parseTaggedText(u"<nope>あ</nope><b>い</i>う", opts);
        CHECK(r.paragraph.text() == u"あいう");
        CHECK(r.errors.size() >= 2);
        // タグに見えないものは本文のまま
        const inl::TagParseResult r2 = inl::parseTaggedText(u"1 < 2 & 3 > 0", opts);
        CHECK(r2.paragraph.text() == u"1 < 2 & 3 > 0");
    }
    CHECK(inl::stripTags(u"<b>あ</b><ruby text=\"い\">う</ruby>") == u"あう");
}

TEST_CASE("tag parser: the parsed paragraph lays out with its annotations") {
    Fixture fx;
    if (!fx.ok()) { MESSAGE("fonts not found; skipping"); return; }
    inl::TagParseOptions opts;
    opts.baseStyle = fx.style(10.0f);
    const inl::TagParseResult r = inl::parseTaggedText(
        u"<ruby text=\"わがはい\">吾輩</ruby>は<b>猫</b>である"
        u"<graph name=\"g\" width=\"20\" height=\"10\">。", opts);
    CHECK(r.errors.empty());

    inl::ParagraphLayouter layouter(fx.fonts);
    const inl::ConstantLineShape shape(200.0f);
    const inl::ParagraphFragment frag = layouter.layout(r.paragraph, WritingMode::HorizontalTb, shape);
    REQUIRE(frag.lines.size() == 1);
    // ルビのグリフが付く
    size_t annotated = 0;
    for (const inl::PlacedGlyph& g : frag.lines[0].glyphs) if (g.annotation) ++annotated;
    CHECK(annotated == 4);
    // プレースホルダの位置が取れる
    const auto phs = inl::placeholderRects(frag, WritingMode::HorizontalTb, Point{0, 20});
    REQUIRE(phs.size() == 1);
    CHECK(phs[0].id == "g");
    CHECK(phs[0].rect.w == doctest::Approx(20.0f));
}
