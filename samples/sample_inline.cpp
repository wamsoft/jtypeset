/**
 * sample_inline.cpp — Phase 1 の確認サンプル
 *
 * 同じ文（約物・和欧混在・ルビ・縦中横・圏点・割注・字取り）を
 * **縦組みと横組み**で組み、PNG / PDF / SVG に出す。禁則違反と両端揃えを検算する。
 *
 * ※ リポジトリルートから実行すること（フォントを ./data/ から読む）
 */

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "typeset/backend/pdf_writer.hpp"
#include "typeset/backend/raster.hpp"
#include "typeset/backend/svg_writer.hpp"
#include "typeset/font/font_set.hpp"
#include "typeset/inl/paragraph.hpp"
#include "typeset/text/char_class.hpp"
#include "typeset/text/utf.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace typeset;

namespace {

size_t findU(const std::u16string& hay, const char16_t* needle) {
    const size_t p = hay.find(needle);
    if (p == std::u16string::npos) {
        std::fprintf(stderr, "annotation target not found\n");
        return 0;
    }
    return p;
}

/// 行頭・行末禁則の違反数と、両端揃えの外れ数を数える
void verify(const char* label, const inl::ParagraphFragment& frag, Pt lineLength) {
    const std::u16string& text = *frag.text;
    int kinsoku = 0, unjustified = 0;
    for (const inl::LineBox& line : frag.lines) {
        if (line.charEnd <= line.charStart) continue;
        const text::CharClass first = text::getCharClass(text::codePointAt(text, line.charStart));
        size_t lastPos = line.charEnd - 1;
        if (lastPos > line.charStart && text[lastPos] >= 0xDC00 && text[lastPos] <= 0xDFFF) --lastPos;
        const text::CharClass last = text::getCharClass(text::codePointAt(text, lastPos));
        if (text::isLineStartProhibited(first)) ++kinsoku;
        if (text::isLineEndProhibited(last)) ++kinsoku;
        if (!line.paragraphEnd && !line.hanging &&
            std::fabs(line.length - (lineLength - line.indent)) > 0.5f) {
            ++unjustified;
        }
    }
    std::printf("%s: %zu lines, kinsoku violations = %d, unjustified lines = %d\n", label,
                frag.lines.size(), kinsoku, unjustified);
    for (size_t i = 0; i < frag.lines.size(); ++i) {
        const inl::LineBox& l = frag.lines[i];
        std::printf("  [%zu] len=%.2f natural=%.2f indent=%.1f%s%s : %s\n", i, l.length,
                    l.naturalLength, l.indent, l.hanging ? " hang" : "",
                    l.paragraphEnd ? " end" : "",
                    text::utf16ToUtf8(text.substr(l.charStart, l.charEnd - l.charStart)).c_str());
    }
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::printf("=== typeset sample_inline (Phase 1) ===\n");

    font::FontSet fonts;
    auto serifJp = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
    auto serif = fonts.loadFile("data/NotoSerif-Regular.ttf", "serif");
    if (!serifJp || !serif) {
        std::fprintf(stderr, "fonts not found under ./data (run `make fontdata`)\n");
        return 1;
    }
    // カラー絵文字（あれば）。COLR v1 のレイヤが塗り付きのパスになり、縦組みでは正立する
    const bool hasEmoji = fonts.loadFile("data/Noto-COLRv1.ttf", "emoji") != nullptr;

    // --- 本文 ---
    TextStyle body;
    body.font.family = {"serif-ja", "serif"};
    if (hasEmoji) body.font.family.push_back("emoji");
    body.size = 11.0f;
    body.fill = Color::rgb(20, 20, 20);

    const std::u16string text =
        u"吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。"
        u"「何でも薄暗いじめじめした所でニャーニャー泣いていた事だけは記憶している」と、"
        u"Wagahai は 24 年に語った……。夏目漱石作の冒頭は、well-known な一文である！"
        u"　あとは、（括弧の）詰めと、中点・読点、の並びを見る。\n"
        u"二つ目の段落。空行の後にも一字下げが付く。絵文字 \U0001F600\U0001F44D\U0001F3FD\U0001F1EF\U0001F1F5 もカラーで組める。";

    inl::Paragraph para;
    para.runs.push_back(inl::InlineRun{text, body});
    para.style.firstLineIndent = 1.0f;
    para.style.lineHeight = 1.9f;
    para.style.spacing.hangingPunctuation = true;

    // 注記（範囲は UTF-16 位置）
    {
        const size_t p1 = findU(text, u"吾輩");
        para.annotations.push_back(inl::Annotation::ruby(p1, p1 + 2, u"わがはい"));
        const size_t p2 = findU(text, u"見当");
        para.annotations.push_back(inl::Annotation::ruby(p2, p2 + 2, u"けん|とう", inl::RubyMode::Mono));
        const size_t p3 = findU(text, u"記憶");
        para.annotations.push_back(inl::Annotation::emphasis(p3, p3 + 2));
        const size_t p4 = findU(text, u"24");
        para.annotations.push_back(inl::Annotation::tateChuYoko(p4, p4 + 2));
        const size_t p5 = findU(text, u"夏目漱石作");
        para.annotations.push_back(inl::Annotation::warichu(p5, p5 + 5, u"明治三十八年発表"));
        const size_t p6 = findU(text, u"冒頭");
        para.annotations.push_back(inl::Annotation::jidori(p6, p6 + 2, 4.0f));
        const size_t p7 = findU(text, u"猫");
        para.annotations.push_back(inl::Annotation::ruby(p7, p7 + 1, u"ねこ"));
    }

    dl::DisplayList page;
    page.page = Size{560.0f, 400.0f};
    page.addRect(Rect{0, 0, page.page.w, page.page.h}, Color::rgb(255, 255, 252));

    inl::ParagraphLayouter layouter(fonts);

    // --- 縦組み（右側。列は右から左へ） ---
    const Pt vLen = 330.0f;
    const Point vOrigin{page.page.w - 30.0f - body.size * 0.5f, 35.0f};
    {
        const inl::ConstantLineShape shape(vLen);
        inl::ParagraphFragment frag = layouter.layout(para, WritingMode::VerticalRl, shape);
        verify("vertical-rl", frag, vLen);
        inl::emitParagraph(page, frag, WritingMode::VerticalRl, vOrigin);
        // 版面の目印
        Stroke s;
        s.color = Color::rgba(0, 150, 0, 90);
        s.width = 0.4f;
        const Pt right = vOrigin.x + body.size * 0.5f;
        const Pt left = vOrigin.x - frag.linePitch * static_cast<float>(frag.lines.size() - 1) - body.size * 0.5f;
        Path frame;
        frame.addRect(Rect{left, vOrigin.y, right - left, vLen});
        page.addPath(frame, std::nullopt, s);
    }

    // --- 横組み（左側） ---
    const Pt hLen = 250.0f;
    const Point hOrigin{30.0f, 35.0f + body.size * 0.5f};
    {
        const inl::ConstantLineShape shape(hLen);
        inl::ParagraphFragment frag = layouter.layout(para, WritingMode::HorizontalTb, shape);
        verify("horizontal-tb", frag, hLen);
        inl::emitParagraph(page, frag, WritingMode::HorizontalTb, hOrigin);
        Stroke s;
        s.color = Color::rgba(0, 150, 0, 90);
        s.width = 0.4f;
        Path frame;
        frame.addRect(Rect{hOrigin.x, hOrigin.y - body.size * 0.5f, hLen,
                           frag.linePitch * static_cast<float>(frag.lines.size() - 1) + body.size});
        page.addPath(frame, std::nullopt, s);
    }

    // --- Knuth–Plass と Greedy の比較（横組み、短い行長） ---
    {
        inl::Paragraph kp = para;
        kp.style.lineBreak.strategy = LineBreakStrategy::KnuthPlass;
        kp.annotations.clear();
        kp.runs[0].text = text.substr(0, text.find(u'\n'));
        kp.runs[0].style.size = 8.0f;
        const inl::ConstantLineShape shape(120.0f);
        inl::ParagraphFragment frag = layouter.layout(kp, WritingMode::HorizontalTb, shape);
        verify("horizontal-tb knuth-plass (8pt/120pt)", frag, 120.0f);
        inl::emitParagraph(page, frag, WritingMode::HorizontalTb, Point{30.0f, 300.0f});
    }

    backend::RasterRenderer raster;
    backend::RasterOptions ro;
    ro.dpi = 216.0f;
    backend::Bitmap bmp = raster.render(page, ro);
    if (backend::savePng(bmp, "output_inline.png")) {
        std::printf("Saved: output_inline.png (%dx%d)\n", bmp.width, bmp.height);
    }
    backend::PdfWriter pdf;
    pdf.setTitle("typeset sample_inline");
    pdf.addPage(page);
    if (pdf.save("output_inline.pdf")) std::printf("Saved: output_inline.pdf\n");
    for (const auto& w : pdf.warnings()) std::printf("  pdf warning: %s\n", w.c_str());
    if (backend::saveSvg(page, "output_inline.svg")) std::printf("Saved: output_inline.svg\n");
    return 0;
}
