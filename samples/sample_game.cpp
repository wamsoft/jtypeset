/**
 * sample_game — ゲーム向け: タグ記法と取り出し口
 *
 * richtext 互換のタグ付きテキストを段落にして、
 *  - リンクの範囲を矩形で（クリック判定用）
 *  - 点 → 文字のヒットテストとキャレット
 *  - 行内プレースホルダ（描かない箱。ホストがボタン等を重ねる）
 *  - 1 文字ずつの表示（組み直さずに maxChars で切る）
 *  - 吹き出しに収める自動縮小と天地中央
 * を見せる。判定に使う矩形は薄い色で重ねて描いてある。
 *
 * リポジトリルートで実行する（./data/ のフォントを読む）。
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
#include "typeset/inl/tag_parser.hpp"

using namespace typeset;

namespace {

void strokeRect(dl::DisplayList& page, const Rect& r, Color color, Pt width = 0.6f) {
    Path p;
    p.addRect(r);
    Stroke s;
    s.color = color;
    s.width = width;
    page.addPath(std::move(p), std::nullopt, s);
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::printf("=== typeset sample_game ===\n");

    font::FontSet fonts;
    auto jp = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
    auto latin = fonts.loadFile("data/NotoSerif-Regular.ttf", "serif");
    if (!jp || !latin) {
        std::fprintf(stderr, "fonts not found under ./data (run `make fontdata`)\n");
        return 1;
    }
    fonts.loadFile("data/NotoSansJP-Regular.otf", "sans-ja");
    fonts.loadFile("data/NotoSerifJP-Bold.otf", "serif-ja-bold");

    dl::DisplayList page;
    page.page = Size{520.0f, 350.0f};
    page.addRect(Rect{0, 0, page.page.w, page.page.h}, Color::rgb(252, 250, 246));

    inl::ParagraphLayouter layouter(fonts);
    const WritingMode wm = WritingMode::HorizontalTb;

    // --- タグ記法 ---
    inl::TagParseOptions opts;
    opts.baseStyle.font.family = {"serif-ja", "serif"};
    opts.baseStyle.size = 15.0f;
    opts.baseStyle.fill = Color::rgb(25, 25, 30);
    opts.namedFamilies["gothic"] = {"sans-ja", "serif"};
    opts.evaluate = [](const std::string& name) -> std::u16string {
        return name == "hero" ? u"アリス" : std::u16string();
    };

    const std::u16string tagged =
        u"<ruby text=\"わがはい\">吾輩</ruby>は<b>猫</b>である。名前は<eval name=\"hero\" alt=\"？\"/>。<br>"
        u"<font face=\"gothic\" size=\"13\"><color value=\"#c02020\">赤いゴシック</color></font>と"
        u"<shadow color=\"#60000000\" x=\"1\" y=\"1\" blur=\"1.5\"><outline color=\"#204090\" width=\"1.2\">"
        u"<outline add color=\"#ffffff\" width=\"0.5\">縁取り</outline></outline></shadow>。"
        u"<graph name=\"icon\" width=\"16\" height=\"16\">を置いて、<keywait>"
        u"<link name=\"next\">つづきを読む</link>。";

    const inl::TagParseResult parsed = inl::parseTaggedText(tagged, opts);
    for (const std::string& e : parsed.errors) std::printf("  tag error: %s\n", e.c_str());

    inl::Paragraph para = parsed.paragraph;
    para.style.align = Align::Start;
    para.style.lineBreak.justify = false;
    para.style.firstLineIndent = 0.0f;
    para.style.lineHeight = 1.9f;

    const inl::ConstantLineShape shape(470.0f);
    const inl::ParagraphFragment frag = layouter.layout(para, wm, shape);
    const Point origin{24.0f, 40.0f};
    inl::emitParagraph(page, frag, wm, origin);

    // リンクの範囲 → 矩形（クリック判定に使う）
    for (const inl::TagLink& link : parsed.links) {
        for (const Rect& r : inl::rectsFor(frag, wm, origin, link.start, link.end)) {
            page.addRect(Rect{r.x, r.bottom() - 0.8f, r.w, 0.8f}, Color::rgb(40, 90, 200));   // 下線
            strokeRect(page, r, Color::rgba(40, 90, 200, 90));
        }
        std::printf("link \"%s\": chars %zu..%zu\n", link.name.c_str(), link.start, link.end);
    }
    // プレースホルダ（描かない箱）の位置にホストがウィジェットを置く
    for (const inl::PlaceholderRect& ph : inl::placeholderRects(frag, wm, origin)) {
        page.addRect(ph.rect, Color::rgb(230, 235, 245));
        strokeRect(page, ph.rect, Color::rgb(120, 140, 190));
        std::printf("placeholder \"%s\": (%.1f, %.1f) %.1fx%.1f\n", ph.id.c_str(), ph.rect.x, ph.rect.y,
                    ph.rect.w, ph.rect.h);
    }
    // マーカー（タイミング）は位置だけ返る。キャレットの矩形で画面上の位置に直せる
    for (const inl::TagMarker& m : parsed.markers) {
        if (const auto caret = inl::caretRect(frag, wm, origin, m.charIndex, 0, 1.2f)) {
            page.addRect(*caret, Color::rgb(220, 120, 40));
        }
        std::printf("marker <%s> at char %zu\n", m.kind.c_str(), m.charIndex);
    }
    // 点 → 文字（ヒットテスト）。当たった文字の箱を囲う
    const Point probe{120.0f, 42.0f};
    if (const auto hit = inl::hitTest(frag, wm, origin, probe)) {
        const Point lo = inl::lineOriginOf(frag, wm, origin, hit->lineIndex);
        for (const inl::CharBox& b : inl::charBoxes(frag, wm, hit->lineIndex)) {
            if (b.charIndex != hit->charIndex) continue;
            strokeRect(page, b.rect(wm, lo), Color::rgb(210, 60, 60), 0.8f);
        }
        std::printf("hitTest(%.0f, %.0f) -> line %zu char %u (inside=%d)\n", probe.x, probe.y,
                    hit->lineIndex, hit->charIndex, hit->inside ? 1 : 0);
    }

    // --- 1 文字ずつの表示（組み直さない） ---
    Pt y = origin.y + frag.blockExtent() + 26.0f;
    {
        TextStyle capStyle;
        capStyle.font.family = {"serif-ja", "serif"};
        capStyle.size = 7.5f;
        capStyle.fill = Color::rgb(120, 120, 130);
        inl::Paragraph cap = inl::Paragraph::plain(u"段階表示: 同じ組版結果を maxChars で切って描く", capStyle);
        cap.style.firstLineIndent = 0.0f;
        const inl::ParagraphFragment cf = layouter.layout(cap, wm, shape);
        inl::emitParagraph(page, cf, wm, Point{24.0f, y});
        y += cf.blockExtent() + 6.0f;
    }
    {
        TextStyle st;
        st.font.family = {"serif-ja", "serif"};
        st.size = 13.0f;
        inl::Paragraph p = inl::Paragraph::plain(u"一文字ずつ表示していく。", st);
        p.style.firstLineIndent = 0.0f;
        p.style.lineBreak.justify = false;
        const inl::ParagraphFragment f = layouter.layout(p, wm, shape);
        for (size_t n : {4u, 8u, 12u}) {
            inl::emitParagraph(page, f, wm, Point{24.0f, y}, 0, n);
            y += f.blockExtent();
        }
        y += 18.0f;
    }

    // --- 吹き出し: 箱に収まるまで縮めて天地中央 ---
    {
        TextStyle capStyle;
        capStyle.font.family = {"serif-ja", "serif"};
        capStyle.size = 7.5f;
        capStyle.fill = Color::rgb(120, 120, 130);
        inl::Paragraph cap = inl::Paragraph::plain(
            u"吹き出し: 3 行に収まるまで文字を縮め（fitParagraph）、箱の天地中央に置く（originInBox）", capStyle);
        cap.style.firstLineIndent = 0.0f;
        const inl::ParagraphFragment cf = layouter.layout(cap, wm, shape);
        inl::emitParagraph(page, cf, wm, Point{24.0f, y});
        y += cf.blockExtent() + 6.0f;

        const Rect balloon{24.0f, y, 220.0f, 74.0f};
        page.addRect(balloon, Color::rgb(240, 244, 252));
        strokeRect(page, balloon, Color::rgb(150, 170, 210));

        TextStyle st;
        st.font.family = {"serif-ja", "serif"};
        st.size = 14.0f;
        inl::Paragraph p = inl::Paragraph::plain(
            u"長い台詞でも、箱に収まるまで文字サイズを段階的に縮めて収める。", st);
        p.style.firstLineIndent = 0.0f;
        p.style.align = Align::Start;
        p.style.lineBreak.justify = false;
        const inl::ConstantLineShape inner(balloon.w - 16.0f);
        const inl::FitResult fit = inl::fitParagraph(layouter, p, wm, inner, 3);
        const Rect content{balloon.x + 8.0f, balloon.y + 4.0f, balloon.w - 16.0f, balloon.h - 8.0f};
        const Point o = inl::originInBox(fit.fragment, wm, content, BlockAlign::Center);
        inl::emitParagraph(page, fit.fragment, wm, o);
        std::printf("balloon: scale=%.2f fits=%d lines=%zu\n", fit.scale, fit.fits ? 1 : 0,
                    fit.fragment.lines.size());
    }

    backend::RasterRenderer raster;
    backend::RasterOptions ro;
    ro.dpi = 192.0f;
    const backend::Bitmap bmp = raster.render(page, ro);
    if (backend::savePng(bmp, "output_game.png")) {
        std::printf("Saved: output_game.png (%dx%d)\n", bmp.width, bmp.height);
    }
    backend::PdfWriter pdf;
    pdf.setTitle("typeset sample_game");
    pdf.addPage(page);
    if (pdf.save("output_game.pdf")) std::printf("Saved: output_game.pdf\n");
    for (const auto& w : pdf.warnings()) std::printf("  pdf warning: %s\n", w.c_str());
    if (backend::saveSvg(page, "output_game.svg")) std::printf("Saved: output_game.svg\n");
    return 0;
}
