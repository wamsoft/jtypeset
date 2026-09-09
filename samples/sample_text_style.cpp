/**
 * sample_text_style — 文字の装飾とフォントの選び方
 *
 * 下線・打消し線・影・二重縁取り・グラデーション、フォントのウェイト選択とバリアブルフォントの軸、
 * OpenType feature（palt）、絵文字の表示形式（VS15 / VS16）、段落の回転、アンチエイリアス無しの描画を
 * 1 枚に並べて、ラスタ / PDF / SVG が同じ絵になることを確かめる。
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

using namespace typeset;

namespace {

/// 1 行を組んで置く。次の行の y を返す
Pt line(dl::DisplayList& page, inl::ParagraphLayouter& layouter, const std::u16string& text,
        const TextStyle& style, Pt x, Pt y, Pt length = 470.0f, float rotation = 0.0f) {
    inl::Paragraph p = inl::Paragraph::plain(text, style);
    p.style.align = Align::Start;
    p.style.lineBreak.justify = false;
    p.style.firstLineIndent = 0.0f;
    p.style.rotation = rotation;
    const inl::ConstantLineShape shape(length);
    const inl::ParagraphFragment frag = layouter.layout(p, WritingMode::HorizontalTb, shape);
    inl::emitParagraph(page, frag, WritingMode::HorizontalTb, Point{x, y});
    return y + frag.blockExtent();
}

/// 見出しの小さなラベル
Pt label(dl::DisplayList& page, inl::ParagraphLayouter& layouter, const std::u16string& text,
         const TextStyle& base, Pt x, Pt y) {
    TextStyle s = base;
    s.size = 7.5f;
    s.fill = Color::rgb(120, 120, 130);
    return line(page, layouter, text, s, x, y);
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::printf("=== typeset sample_text_style ===\n");

    font::FontSet fonts;
    auto jp = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
    auto latin = fonts.loadFile("data/NotoSerif-Regular.ttf", "serif");
    if (!jp || !latin) {
        std::fprintf(stderr, "fonts not found under ./data (run `make fontdata`)\n");
        return 1;
    }
    // 同じ family 名の太字・斜体を足すと、weight / italic で本物の face が選ばれる
    const bool hasBold = fonts.loadFile("data/NotoSerifJP-Bold.otf", "serif-ja-bold") != nullptr;
    const bool hasLatinBold = fonts.loadFile("data/NotoSerif-Bold.ttf", "serif-bold") != nullptr;
    const bool hasItalic = fonts.loadFile("data/NotoSerif-Italic.ttf", "serif-italic") != nullptr;
    const bool hasVariable = fonts.loadFile("data/NotoSans-Variable.ttf", "sans-var") != nullptr;
    const bool hasEmoji = fonts.loadFile("data/Noto-COLRv1.ttf", "emoji") != nullptr;

    TextStyle body;
    body.font.family = {"serif-ja", "serif"};
    if (hasEmoji) body.font.family.push_back("emoji");
    body.size = 13.0f;
    body.fill = Color::rgb(25, 25, 30);

    dl::DisplayList page;
    page.page = Size{520.0f, 400.0f};
    page.addRect(Rect{0, 0, page.page.w, page.page.h}, Color::rgb(255, 255, 253));

    inl::ParagraphLayouter layouter(fonts);
    const Pt x = 24.0f;
    Pt y = 26.0f;

    // --- 装飾 ---
    y = label(page, layouter, u"装飾: 下線・打消し線・影・二重縁取り", body, x, y) + 3.0f;
    {
        TextStyle underlined = body;
        underlined.underline = TextDecoration{};
        TextStyle struck = body;
        struck.strikethrough = TextDecoration{Color::rgb(200, 40, 40)};
        TextStyle shadowed = body;
        shadowed.shadow = TextShadow{Color::rgba(0, 0, 0, 120), Point{1.0f, 1.2f}, 1.6f};
        TextStyle doubled = body;
        {
            Stroke outer;
            outer.color = Color::rgb(30, 60, 160);
            outer.width = 2.0f;
            outer.join = StrokeJoin::Round;
            Stroke inner;
            inner.color = Color::rgb(255, 255, 255);
            inner.width = 0.9f;
            inner.join = StrokeJoin::Round;
            doubled.layers = {TextLayer::outlined(outer), TextLayer::outlined(inner),
                              TextLayer::filled(Color::rgb(30, 60, 160))};
        }
        inl::Paragraph p;
        p.runs.push_back(inl::InlineRun{u"下線つき", underlined});
        p.runs.push_back(inl::InlineRun{u"／", body});
        p.runs.push_back(inl::InlineRun{u"打消し線", struck});
        p.runs.push_back(inl::InlineRun{u"／", body});
        p.runs.push_back(inl::InlineRun{u"ぼかした影", shadowed});
        p.runs.push_back(inl::InlineRun{u"／", body});
        p.runs.push_back(inl::InlineRun{u"二重縁取り", doubled});
        p.style.align = Align::Start;
        p.style.lineBreak.justify = false;
        const inl::ConstantLineShape shape(470.0f);
        const inl::ParagraphFragment frag = layouter.layout(p, WritingMode::HorizontalTb, shape);
        inl::emitParagraph(page, frag, WritingMode::HorizontalTb, Point{x, y});
        y += frag.blockExtent() + 12.0f;
    }

    // --- グラデーション ---
    y = label(page, layouter, u"塗り: 線形グラデーション（外接矩形の 0〜1）と放射グラデーションの背景", body, x, y) + 3.0f;
    {
        page.addRect(Rect{x, y - 4.0f, 200.0f, 26.0f},
                     Paint::radial(Point{0.5f, 0.5f}, 0.55f,
                                   {{0.0f, Color::rgb(250, 246, 230)}, {1.0f, Color::rgb(226, 214, 180)}}));
        TextStyle grad = body;
        grad.size = 18.0f;
        grad.fill = Paint::linear(Point{0, 0}, Point{1, 0},
                                  {{0.0f, Color::rgb(200, 30, 60)},
                                   {0.5f, Color::rgb(230, 160, 30)},
                                   {1.0f, Color::rgb(30, 90, 200)}});
        y = line(page, layouter, u"Gradient グラデーション", grad, x + 6.0f, y + 14.0f) + 12.0f;
    }

    // --- フォントのウェイト・斜体・バリアブル ---
    y = label(page, layouter, u"フォント: ウェイト／斜体の face 選択、バリアブルフォントの軸", body, x, y) + 3.0f;
    {
        inl::Paragraph p;
        p.runs.push_back(inl::InlineRun{u"標準", body});
        TextStyle bold = body;
        bold.font.weight = 700;
        p.runs.push_back(inl::InlineRun{hasBold && hasLatinBold ? u"／太字（実 Bold）" : u"／太字（合成）", bold});
        TextStyle italic = body;
        italic.font.italic = true;
        p.runs.push_back(inl::InlineRun{hasItalic ? u"／Italic face" : u"／fake italic", italic});
        if (hasVariable) {
            p.runs.push_back(inl::InlineRun{u"／", body});
            TextStyle vf = body;
            vf.font.family = {"sans-var"};      // 可変フォント 1 本（和文は持たないので欧文だけ入れる）
            vf.font.variations = {{"wght", 300.0f}};
            p.runs.push_back(inl::InlineRun{u"wght300", vf});
            TextStyle vf2 = vf;
            vf2.font.variations = {{"wght", 800.0f}, {"wdth", 75.0f}};
            p.runs.push_back(inl::InlineRun{u" wght800+wdth75", vf2});
        }
        p.style.align = Align::Start;
        p.style.lineBreak.justify = false;
        const inl::ConstantLineShape shape(470.0f);
        const inl::ParagraphFragment frag = layouter.layout(p, WritingMode::HorizontalTb, shape);
        inl::emitParagraph(page, frag, WritingMode::HorizontalTb, Point{x, y});
        y += frag.blockExtent() + 12.0f;
    }

    // --- OpenType feature ---
    y = label(page, layouter, u"OpenType feature: 既定（JLReq の詰め）と palt（フォントの詰め）", body, x, y) + 3.0f;
    {
        const std::u16string sample = u"「猫」、犬。（鳥）";
        y = line(page, layouter, sample, body, x, y);
        TextStyle palt = body;
        palt.features = {"palt"};
        y = line(page, layouter, sample, palt, x, y) + 12.0f;
    }

    // --- 絵文字の表示形式 ---
    if (hasEmoji) {
        y = label(page, layouter, u"絵文字: 既定 / VS15（字形）/ VS16（カラー）", body, x, y) + 3.0f;
        inl::Paragraph p;
        p.runs.push_back(inl::InlineRun{u"☎ ☀ ", body});
        TextStyle text = body;
        text.emojiPresentation = EmojiPresentation::Text;
        p.runs.push_back(inl::InlineRun{u"☎ ☀ ", text});
        TextStyle emoji = body;
        emoji.emojiPresentation = EmojiPresentation::Emoji;
        p.runs.push_back(inl::InlineRun{u"☎ ☀", emoji});
        p.style.align = Align::Start;
        p.style.lineBreak.justify = false;
        const inl::ConstantLineShape shape(470.0f);
        const inl::ParagraphFragment frag = layouter.layout(p, WritingMode::HorizontalTb, shape);
        inl::emitParagraph(page, frag, WritingMode::HorizontalTb, Point{x, y});
        y += frag.blockExtent() + 12.0f;
    }

    // --- 段落の回転 ---
    y = label(page, layouter, u"段落の回転（行頭を中心に）", body, x, y) + 3.0f;
    {
        TextStyle rot = body;
        rot.size = 10.0f;
        rot.fill = Color::rgb(150, 150, 160);
        line(page, layouter, u"回転 15 度", rot, x + 4.0f, y + 10.0f, 200.0f, 15.0f);
        line(page, layouter, u"回転 -90 度", rot, x + 150.0f, y + 40.0f, 200.0f, -90.0f);
        y += 52.0f;
    }

    // --- 出力（アンチエイリアスの有無で 2 枚） ---
    backend::RasterRenderer raster;
    backend::RasterOptions ro;
    ro.dpi = 192.0f;
    const backend::Bitmap bmp = raster.render(page, ro);
    if (backend::savePng(bmp, "output_text_style.png")) {
        std::printf("Saved: output_text_style.png (%dx%d)\n", bmp.width, bmp.height);
    }
    backend::RasterOptions binary = ro;
    binary.dpi = 96.0f;
    binary.antialias = false;      // 小サイズのゲーム用途: にじみの無い 2 値
    if (backend::savePng(raster.render(page, binary), "output_text_style_binary.png")) {
        std::printf("Saved: output_text_style_binary.png (antialias=false)\n");
    }

    backend::PdfWriter pdf;
    pdf.setTitle("typeset sample_text_style");
    pdf.addPage(page);
    if (pdf.save("output_text_style.pdf")) std::printf("Saved: output_text_style.pdf\n");
    for (const auto& w : pdf.warnings()) std::printf("  pdf warning: %s\n", w.c_str());
    if (backend::saveSvg(page, "output_text_style.svg")) std::printf("Saved: output_text_style.svg\n");
    return 0;
}
