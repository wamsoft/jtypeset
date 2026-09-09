/**
 * sample_intl — 多言語と欧文の行分割
 *
 * 双方向テキスト（アラビア文字・ヘブライ文字を UAX #9 で視覚順に並べる）、言語ごとのフォント、
 * 宣言だけして初回使用時に開くフォント、欧文のハイフネーション、折返しの方式、禁則の強弱を並べて見せる。
 *
 * リポジトリルートで実行する（./data/ のフォントとハイフネーションのパターンを読む）。
 */

#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "typeset/backend/pdf_writer.hpp"
#include "typeset/backend/raster.hpp"
#include "typeset/backend/svg_writer.hpp"
#include "typeset/font/font_set.hpp"
#include "typeset/inl/paragraph.hpp"
#include "typeset/text/hyphenation.hpp"

using namespace typeset;

namespace {

struct Layout {
    inl::ParagraphLayouter& layouter;
    dl::DisplayList& page;
    Pt x = 24.0f;
    Pt y = 26.0f;
    Pt width = 470.0f;

    Pt caption(const std::u16string& text, const TextStyle& base) {
        TextStyle s = base;
        s.size = 7.5f;
        s.fill = Color::rgb(120, 120, 130);
        inl::Paragraph p = inl::Paragraph::plain(text, s);
        p.style.firstLineIndent = 0.0f;
        p.style.lineBreak.justify = false;
        const inl::ConstantLineShape shape(width);
        const inl::ParagraphFragment f = layouter.layout(p, WritingMode::HorizontalTb, shape);
        inl::emitParagraph(page, f, WritingMode::HorizontalTb, Point{x, y});
        y += f.blockExtent() + 2.0f;
        return y;
    }

    /// 段落を 1 つ置いて y を進める
    const inl::ParagraphFragment place(inl::Paragraph p, Pt length, Pt gap = 10.0f) {
        const inl::ConstantLineShape shape(length);
        const inl::ParagraphFragment f = layouter.layout(p, WritingMode::HorizontalTb, shape);
        inl::emitParagraph(page, f, WritingMode::HorizontalTb, Point{x, y});
        y += f.blockExtent() + gap;
        return f;
    }
};

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::printf("=== typeset sample_intl ===\n");

    font::FontSet fonts;
    auto jp = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
    auto latin = fonts.loadFile("data/NotoSerif-Regular.ttf", "serif");
    if (!jp || !latin) {
        std::fprintf(stderr, "fonts not found under ./data (run `make fontdata`)\n");
        return 1;
    }
    // 宣言だけしておくと、その文字を実際に使うときに初めて開く（起動が軽い）
    {
        font::FontDeclaration arabic;
        arabic.key = "arabic";
        arabic.path = "data/NotoSansArabic-Regular.ttf";
        arabic.languages = {"ar"};
        arabic.ranges = {{0x0600, 0x06FF}, {0xFB50, 0xFEFF}};   // 開かずにカバレッジを判定できる
        fonts.declare(arabic);

        font::FontDeclaration hebrew;
        hebrew.key = "hebrew";
        hebrew.path = "data/NotoSansHebrew-Regular.ttf";
        hebrew.languages = {"he"};
        hebrew.ranges = {{0x0590, 0x05FF}};
        fonts.declare(hebrew);
    }
    // 言語ごとに先に試すフォント（同じ字形でも言語で使い分けたいとき）
    fonts.loadFile("data/NotoSansJP-Regular.otf", "sans-ja");
    fonts.setLanguageFonts("zh", {"sans-ja"});

    std::printf("declared but not opened yet: arabic=%d hebrew=%d\n",
                fonts.isLoaded("arabic") ? 1 : 0, fonts.isLoaded("hebrew") ? 1 : 0);

    // 欧文のハイフネーション（TeX のパターン。`make fontdata` が data/ に取る）
    text::HyphenationDictionary hyphenation;
    const size_t patterns = hyphenation.forLanguage("en").addPatternFile("data/hyph-en-us.tex");
    std::printf("hyphenation patterns: %zu\n", patterns);

    TextStyle body;
    body.font.family = {"serif-ja", "serif", "arabic", "hebrew"};
    body.size = 12.0f;
    body.fill = Color::rgb(25, 25, 30);

    dl::DisplayList page;
    page.page = Size{520.0f, 700.0f};
    page.addRect(Rect{0, 0, page.page.w, page.page.h}, Color::rgb(255, 255, 253));

    inl::ParagraphLayouter layouter(fonts);
    Layout out{layouter, page};

    // --- 双方向テキスト ---
    out.caption(u"双方向: 和文・欧文の中のヘブライ文字とアラビア文字（UAX #9 で視覚順に並べる）", body);
    {
        TextStyle st = body;
        st.language = "en";
        inl::Paragraph p = inl::Paragraph::plain(
            u"日本語の中に English と עברית（ヘブライ語）と العربية جميلة（アラビア語）が混ざる。", st);
        p.style.firstLineIndent = 0.0f;
        out.place(std::move(p), out.width);
    }
    out.caption(u"基底方向が RTL の段落: 行頭揃えは右揃えになり、欧文だけ左から右へ", body);
    {
        TextStyle st = body;
        st.language = "he";
        inl::Paragraph p = inl::Paragraph::plain(u"שלום עולם! זהו משפט בעברית עם English בתוכו.", st);
        p.style.firstLineIndent = 0.0f;
        p.style.align = Align::Start;
        p.style.lineBreak.justify = false;
        p.style.direction = Direction::Auto;      // 最初の強い文字（ヘブライ文字）で RTL になる
        out.place(std::move(p), 300.0f);
    }
    std::printf("opened on first use: arabic=%d hebrew=%d\n",
                fonts.isLoaded("arabic") ? 1 : 0, fonts.isLoaded("hebrew") ? 1 : 0);

    // --- 欧文のハイフネーション ---
    const std::u16string english =
        u"Typesetting is the composition of text by means of arranging physical type or digital "
        u"equivalents in order to display printed matter.";
    out.caption(u"欧文: ハイフネーション無し（右が揃わない／長い語がはみ出す）", body);
    {
        TextStyle st = body;
        st.language = "en";
        inl::Paragraph p = inl::Paragraph::plain(english, st);
        p.style.firstLineIndent = 0.0f;
        p.style.lineBreak.strategy = LineBreakStrategy::KnuthPlass;
        out.place(std::move(p), 150.0f);
    }
    out.caption(u"欧文: ハイフネーション有り（語の途中で割ってハイフンを出す）", body);
    {
        TextStyle st = body;
        st.language = "en";
        inl::Paragraph p = inl::Paragraph::plain(english, st);
        p.style.firstLineIndent = 0.0f;
        p.style.lineBreak.strategy = LineBreakStrategy::KnuthPlass;
        p.style.lineBreak.hyphenation = &hyphenation;     // 所有しないので組版の間は生かしておく
        const inl::ParagraphFragment f = out.place(std::move(p), 150.0f);
        size_t split = 0;
        for (const inl::LineBox& l : f.lines) if (l.hyphenated) ++split;
        std::printf("hyphenated lines: %zu / %zu\n", split, f.lines.size());
    }

    // --- 折返しの方式と禁則の強さ ---
    out.caption(u"折返し: 既定（語で切る）と Char（語の途中でも切る）", body);
    {
        const std::u16string word = u"internationalization を含む行";
        TextStyle st = body;
        st.language = "en";
        for (WrapMode w : {WrapMode::Mixed, WrapMode::Char}) {
            inl::Paragraph p = inl::Paragraph::plain(word, st);
            p.style.firstLineIndent = 0.0f;
            p.style.align = Align::Start;
            p.style.lineBreak.justify = false;
            p.style.lineBreak.wrap = w;
            out.place(std::move(p), 100.0f, 4.0f);
        }
        out.y += 6.0f;
    }
    out.caption(u"禁則: Strict（長音を行頭に置かない）と Normal（弱い禁則にして詰める）", body);
    {
        const std::u16string kana = u"アイウーエオカキクケコサシスセソ";
        for (KinsokuLevel k : {KinsokuLevel::Strict, KinsokuLevel::Normal}) {
            inl::Paragraph p = inl::Paragraph::plain(kana, body);
            p.style.firstLineIndent = 0.0f;
            p.style.align = Align::Start;
            p.style.lineBreak.justify = false;
            p.style.spacing.kinsoku = k;
            p.style.spacing.kanjiSkipStretch = 0.0f;
            out.place(std::move(p), 48.0f, 4.0f);
        }
    }

    backend::RasterRenderer raster;
    backend::RasterOptions ro;
    ro.dpi = 192.0f;
    const backend::Bitmap bmp = raster.render(page, ro);
    if (backend::savePng(bmp, "output_intl.png")) {
        std::printf("Saved: output_intl.png (%dx%d)\n", bmp.width, bmp.height);
    }
    backend::PdfWriter pdf;
    pdf.setTitle("typeset sample_intl");
    pdf.addPage(page);
    if (pdf.save("output_intl.pdf")) std::printf("Saved: output_intl.pdf\n");
    for (const auto& w : pdf.warnings()) std::printf("  pdf warning: %s\n", w.c_str());
    if (backend::saveSvg(page, "output_intl.svg")) std::printf("Saved: output_intl.svg\n");
    return 0;
}
