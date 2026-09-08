/**
 * sample_dl.cpp — Phase 0 の確認サンプル
 *
 * 表示リストを 1 本組んで、ラスタ（PNG）/ PDF / SVG の 3 backend へ出す。
 * 3 つの座標が一致することを見るのが目的で、組版はまだしない
 * （グリフはアドバンスを足して手で並べる）。
 *
 * ※ リポジトリルートから実行すること（フォントを ./data/ から読む）
 */

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <glyphware/Face.h>

#include "typeset/backend/pdf_writer.hpp"
#include "typeset/backend/raster.hpp"
#include "typeset/backend/svg_writer.hpp"
#include "typeset/dl/display_list.hpp"
#include "typeset/dl/glyph_transform.hpp"
#include "typeset/font/font_set.hpp"
#include "typeset/writing_mode.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace typeset;

namespace {

/// 横組み 1 行をベタで並べる（ペン位置 = ベースライン左端）
dl::GlyphRun horizontalRun(const std::shared_ptr<glyphware::Face>& face, Pt size,
                           const std::u16string& text, Point pen, Color color) {
    dl::GlyphRun run;
    run.face = face;
    run.size = size;
    run.fill = color;
    run.text = std::make_shared<const std::u16string>(text);
    const float upem = font::unitsPerEm(*face);
    Pt x = pen.x;
    for (size_t i = 0; i < text.size(); ++i) {
        dl::Glyph g;
        g.gid = face->glyphIndex(text[i]);
        g.pos = Point{x, pen.y};
        g.charIndex = static_cast<uint32_t>(i);
        run.glyphs.push_back(g);
        glyphware::GlyphMetrics m;
        if (face->glyphMetricsUnscaled(g.gid, m)) x += m.advanceX * size / upem;
        else x += size;
    }
    return run;
}

/// 縦組み 1 列をベタで並べる。和文は正立（1em 送り、ペンは em box の中心上）、
/// 欧文は横倒し（-90°、ベースラインは列の中心線から ascent/descent の中点分ずらす）
dl::GlyphRun verticalRun(const std::shared_ptr<glyphware::Face>& jp,
                         const std::shared_ptr<glyphware::Face>& latin, Pt size,
                         const std::u16string& text, Point top, Color color,
                         std::vector<dl::GlyphRun>& extra) {
    dl::GlyphRun runJp;
    runJp.face = jp;
    runJp.size = size;
    runJp.fill = color;
    runJp.text = std::make_shared<const std::u16string>(text);

    dl::GlyphRun runLatin;
    runLatin.face = latin;
    runLatin.size = size;
    runLatin.fill = color;
    runLatin.text = runJp.text;

    const float upemL = font::unitsPerEm(*latin);
    const glyphware::LineMetrics lm = latin->lineMetrics();
    const float ascent = lm.ascenderUnits * size / upemL;      // 正
    const float descent = -lm.descenderUnits * size / upemL;   // 正
    const float upemJ = font::unitsPerEm(*jp);
    const glyphware::LineMetrics lmJ = jp->lineMetrics();

    Pt y = top.y;
    for (size_t i = 0; i < text.size(); ++i) {
        const char16_t c = text[i];
        const bool latinChar = c < 0x3000;
        dl::Glyph g;
        g.charIndex = static_cast<uint32_t>(i);
        if (!latinChar) {
            // 正立: 横組み用グリフを 1em 角の中央に置く（縦字形置換は Phase 1 のシェイパーの仕事）
            g.gid = jp->glyphIndex(c);
            glyphware::GlyphMetrics m;
            Pt adv = size;
            if (jp->glyphMetricsUnscaled(g.gid, m)) adv = m.advanceX * size / upemJ;
            // ベースラインは em box の上端から ascender 分下。em box の高さを 1em とみなす
            const float ascJ = lmJ.ascenderUnits * size / upemJ;
            const float descJ = -lmJ.descenderUnits * size / upemJ;
            const float boxTop = y + (size - (ascJ + descJ)) * 0.5f;
            g.pos = Point{top.x - adv * 0.5f, boxTop + ascJ};
            runJp.glyphs.push_back(g);
        } else {
            g.gid = latin->glyphIndex(c);
            glyphware::GlyphMetrics m;
            Pt adv = size * 0.5f;
            if (latin->glyphMetricsUnscaled(g.gid, m)) adv = m.advanceX * size / upemL;
            // 横倒し: 天が右を向く。ベースラインは中心線から (ascent - descent)/2 だけ左
            g.xform = dl::glyphMatrix(kSidewaysRotation);
            g.pos = Point{top.x - (ascent - descent) * 0.5f, y};
            runLatin.glyphs.push_back(g);
            y += adv;
            continue;
        }
        y += size;
    }
    if (!runLatin.glyphs.empty()) extra.push_back(std::move(runLatin));
    return runJp;
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::printf("=== typeset sample_dl (Phase 0) ===\n");

    font::FontSet fonts;
    auto serifJp = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
    auto sans = fonts.loadFile("data/NotoSans-Regular.ttf", "sans");
    if (!serifJp || !sans) {
        std::fprintf(stderr, "fonts not found under ./data (run `make fontdata`)\n");
        return 1;
    }
    std::printf("fonts: %s / %s\n", serifJp->descriptor().family.c_str(),
                sans->descriptor().family.c_str());

    dl::DisplayList page;
    page.page = Size{240.0f, 200.0f};

    // 背景と版面枠
    page.addRect(Rect{0, 0, page.page.w, page.page.h}, Color::rgb(255, 255, 250));
    {
        Path frame;
        frame.addRect(Rect{20, 20, 200, 160});
        Stroke s;
        s.color = Color::rgb(120, 120, 120);
        s.width = 0.75f;
        page.addPath(frame, std::nullopt, s);
    }

    // 横組み 1 行 + 下線
    const std::u16string hText = u"横組み Aあ typeset";
    page.add(horizontalRun(serifJp, 14.0f, u"横組み ", Point{30, 50}, Color::rgb(20, 20, 20)));
    page.add(horizontalRun(sans, 14.0f, u"Aa typeset", Point{92, 50}, Color::rgb(20, 20, 20)));
    {
        Stroke s;
        s.color = Color::rgb(200, 40, 40);
        s.width = 1.0f;
        page.addLine(Point{30, 54}, Point{170, 54}, s);
    }

    // 縦組み 1 列（和文正立 + 欧文横倒し）
    {
        std::vector<dl::GlyphRun> extra;
        dl::GlyphRun col = verticalRun(serifJp, sans, 16.0f, u"縦組みAb字", Point{195, 40},
                                       Color::rgb(20, 20, 120), extra);
        page.add(std::move(col));
        for (auto& r : extra) page.add(std::move(r));
        // 列の中心線
        Stroke s;
        s.color = Color::rgba(0, 160, 0, 128);
        s.width = 0.5f;
        page.addLine(Point{195, 36}, Point{195, 150}, s);
    }

    // 変形付きグループ: 楕円（塗り＋線）と回転・拡大したテキスト、フェイクボールド／斜体
    {
        dl::Group grp;
        grp.xform = Matrix::translation(40.0f, 90.0f);
        grp.opacity = 0.9f;
        Path ell;
        ell.addEllipse(Rect{0, 0, 90, 50});
        dl::PathItem pi;
        pi.path = ell;
        pi.fill = Color::rgb(230, 240, 255);
        Stroke s;
        s.color = Color::rgb(60, 90, 160);
        s.width = 1.5f;
        pi.stroke = s;
        grp.children.push_back(std::move(pi));

        dl::GlyphRun bold = horizontalRun(serifJp, 12.0f, u"太字", Point{12, 30}, Color::rgb(0, 0, 0));
        bold.embolden = dl::fakeBoldWidth(12.0f);
        grp.children.push_back(std::move(bold));

        dl::GlyphRun italic = horizontalRun(sans, 12.0f, u"Italic", Point{44, 30}, Color::rgb(0, 0, 0));
        for (auto& g : italic.glyphs) g.xform = dl::glyphMatrix(0.0f, 1.0f, 1.0f, dl::kFakeItalicSkew);
        grp.children.push_back(std::move(italic));

        page.add(std::move(grp));
    }

    // 縁取り文字（塗り無し）と平体
    {
        dl::GlyphRun outline = horizontalRun(serifJp, 22.0f, u"縁取", Point{30, 170}, Color::rgb(0, 0, 0));
        outline.fill = std::nullopt;
        Stroke s;
        s.color = Color::rgb(180, 60, 20);
        s.width = 0.8f;
        outline.stroke = s;
        page.add(std::move(outline));

        dl::GlyphRun flat = horizontalRun(serifJp, 22.0f, u"平体", Point{90, 170}, Color::rgb(40, 40, 40));
        for (auto& g : flat.glyphs) g.xform = dl::glyphMatrix(0.0f, 1.0f, 0.7f);
        page.add(std::move(flat));
    }

    const Rect b = dl::bounds(page);
    std::printf("bounds: %.1f %.1f %.1f %.1f\n", b.x, b.y, b.w, b.h);

    // --- ラスタ ---
    backend::RasterRenderer raster;
    backend::RasterOptions ro;
    ro.dpi = 216.0f;
    backend::Bitmap bmp = raster.render(page, ro);
    if (backend::savePng(bmp, "output_dl.png")) {
        std::printf("Saved: output_dl.png (%dx%d @ %.0f dpi)\n", bmp.width, bmp.height, ro.dpi);
    }

    // --- PDF ---
    backend::PdfWriter pdf;
    pdf.setTitle("typeset sample_dl");
    pdf.addPage(page);
    if (pdf.save("output_dl.pdf")) std::printf("Saved: output_dl.pdf\n");
    for (const auto& w : pdf.warnings()) std::printf("  pdf warning: %s\n", w.c_str());

    // --- SVG ---
    if (backend::saveSvg(page, "output_dl.svg")) std::printf("Saved: output_dl.svg\n");

    return 0;
}
