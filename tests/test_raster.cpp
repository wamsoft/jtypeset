#include <doctest/doctest.h>

#include <numeric>

#include "backend/path_raster.hpp"
#include "typeset/backend/raster.hpp"
#include "typeset/backend/svg_writer.hpp"
#include "typeset/backend/pdf_writer.hpp"
#include "typeset/font/font_set.hpp"
#include "typeset/inl/paragraph.hpp"

using namespace typeset;
using namespace typeset::backend;

namespace {

double coverageSum(const detail::Coverage& c) {
    return std::accumulate(c.a.begin(), c.a.end(), 0.0) / 255.0;
}

} // namespace

TEST_CASE("rasterizePolygons: axis-aligned rectangle area") {
    std::vector<std::vector<Point>> polys = {{{2, 2}, {12, 2}, {12, 7}, {2, 7}}};
    detail::Coverage cov;
    REQUIRE(detail::rasterizePolygons(polys, false, 100, 100, cov));
    CHECK(coverageSum(cov) == doctest::Approx(50.0).epsilon(0.01));
}

TEST_CASE("rasterizePolygons: half-pixel offset rectangle keeps area") {
    std::vector<std::vector<Point>> polys = {{{2.5f, 2.25f}, {12.5f, 2.25f}, {12.5f, 7.25f}, {2.5f, 7.25f}}};
    detail::Coverage cov;
    REQUIRE(detail::rasterizePolygons(polys, false, 100, 100, cov));
    CHECK(coverageSum(cov) == doctest::Approx(50.0).epsilon(0.02));
}

TEST_CASE("rasterizePolygons: nonzero vs evenodd on nested squares") {
    // 同じ向きの入れ子: nonzero は全部塗る、evenodd は穴が開く
    std::vector<std::vector<Point>> polys = {
        {{0, 0}, {10, 0}, {10, 10}, {0, 10}},
        {{2, 2}, {8, 2}, {8, 8}, {2, 8}},
    };
    detail::Coverage nz, eo;
    REQUIRE(detail::rasterizePolygons(polys, false, 100, 100, nz));
    REQUIRE(detail::rasterizePolygons(polys, true, 100, 100, eo));
    CHECK(coverageSum(nz) == doctest::Approx(100.0).epsilon(0.01));
    CHECK(coverageSum(eo) == doctest::Approx(64.0).epsilon(0.01));
}

TEST_CASE("strokeToPolygons: horizontal rule has width*length area") {
    std::vector<std::vector<Point>> lines = {{{10, 10}, {60, 10}}};
    const auto polys = detail::strokeToPolygons(lines, {false}, 4.0f, StrokeJoin::Miter, StrokeCap::Butt);
    detail::Coverage cov;
    REQUIRE(detail::rasterizePolygons(polys, false, 100, 100, cov));
    CHECK(coverageSum(cov) == doctest::Approx(200.0).epsilon(0.02));
}

TEST_CASE("RasterRenderer fills a rect with the expected color") {
    dl::DisplayList list;
    list.page = Size{10, 10};
    list.addRect(Rect{2, 2, 4, 4}, Color::rgb(255, 0, 0));
    RasterRenderer r;
    RasterOptions o;
    o.dpi = 72;
    o.background = Color::rgb(255, 255, 255);
    const Bitmap bmp = r.render(list, o);
    REQUIRE(bmp.width == 10);
    REQUIRE(bmp.height == 10);
    CHECK(bmp.row(3)[3] == 0xFFFF0000u);
    CHECK(bmp.row(0)[0] == 0xFFFFFFFFu);
    CHECK(bmp.row(6)[6] == 0xFFFFFFFFu);
}

TEST_CASE("RasterRenderer: a blurred glyph run spreads its coverage and keeps the total ink") {
    font::FontSet fonts;
    if (!fonts.loadFile("data/NotoSerif-Regular.ttf", "serif")) { MESSAGE("fonts not found; skipping"); return; }
    TextStyle st;
    st.font.family = {"serif"};
    st.size = 40.0f;
    st.fill = Color::rgb(0, 0, 0);
    inl::Paragraph para = inl::Paragraph::plain(u"H", st);
    inl::ParagraphLayouter layouter(fonts);
    const inl::ConstantLineShape shape(200.0f);
    const inl::ParagraphFragment frag = layouter.layout(para, WritingMode::HorizontalTb, shape);

    dl::DisplayList sharp;
    sharp.page = Size{120, 120};
    inl::emitParagraph(sharp, frag, WritingMode::HorizontalTb, Point{40, 60});
    dl::DisplayList blurred = sharp;
    for (dl::Item& item : blurred.items) {
        if (auto* run = std::get_if<dl::GlyphRun>(&item)) run->blur = 4.0f;
    }

    RasterRenderer r;
    RasterOptions o;
    o.dpi = 72;
    o.background = Color::rgb(255, 255, 255);
    const Bitmap a = r.render(sharp, o);
    const Bitmap b = r.render(blurred, o);
    REQUIRE(a.width == b.width);

    auto ink = [](const Bitmap& bmp, int& minX, int& maxX) {
        double sum = 0.0;
        minX = bmp.width; maxX = -1;
        for (int y = 0; y < bmp.height; ++y) {
            for (int x = 0; x < bmp.width; ++x) {
                const int v = 255 - static_cast<int>(bmp.row(y)[x] & 0xFF);
                if (v > 0) { minX = std::min(minX, x); maxX = std::max(maxX, x); }
                sum += v;
            }
        }
        return sum;
    };
    int aMin, aMax, bMin, bMax;
    const double ia = ink(a, aMin, aMax);
    const double ib = ink(b, bMin, bMax);
    REQUIRE(ia > 0.0);
    // ぼかしで広がる（半径 4pt ≒ 4px、σ=2 なので数 px 外へ）が、総量はほぼ変わらない
    CHECK(bMin < aMin - 2);
    CHECK(bMax > aMax + 2);
    CHECK(ib > ia * 0.9);
    CHECK(ib < ia * 1.1);
    // 文字の内部（一番濃い画素）はぼかしで薄くなる
    int aDark = 255, bDark = 255;
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            aDark = std::min(aDark, static_cast<int>(a.row(y)[x] & 0xFF));
            bDark = std::min(bDark, static_cast<int>(b.row(y)[x] & 0xFF));
        }
    }
    CHECK(aDark == 0);
    CHECK(bDark > 0);
}

#include <fstream>
#include <iterator>
#include <vector>

#include "backend/sfnt_info.hpp"

TEST_CASE("sfnt_info: fsType embedding permissions and TTC face directories") {
    using typeset::backend::SfntInfo;
    SfntInfo s;
    CHECK_FALSE(s.embeddingRestricted());
    CHECK_FALSE(s.noSubsetting());
    CHECK_FALSE(s.bitmapOnly());
    s.fsType = 0x0002;   // Restricted License
    CHECK(s.embeddingRestricted());
    s.fsType = 0x0008;   // Editable（游明朝など）
    CHECK_FALSE(s.embeddingRestricted());
    s.fsType = 0x0004 | 0x0002;   // Preview & Print が立っていれば Restricted ではない
    CHECK_FALSE(s.embeddingRestricted());
    s.fsType = 0x0100;
    CHECK(s.noSubsetting());
    s.fsType = 0x0200;
    CHECK(s.bitmapOnly());

    // 実フォント: Noto Serif JP は Installable（fsType 0）で CFF
    std::ifstream f("data/NotoSerifJP-Regular.otf", std::ios::binary);
    if (!f) { MESSAGE("font not found; skipping"); return; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    SfntInfo info;
    REQUIRE(typeset::backend::parseSfnt(bytes.data(), bytes.size(), info));
    CHECK(info.isCFF);
    CHECK_FALSE(info.isCollection);
    CHECK(info.fsType == 0);
    CHECK(info.unitsPerEm == 1000);

    // 疑似 TTC: 同じフォントを 2 面持つコレクションを作り、face 1 のディレクトリが読めること
    std::vector<uint8_t> ttc;
    auto put32 = [&](uint32_t v) { for (int i = 3; i >= 0; --i) ttc.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF)); };
    ttc.insert(ttc.end(), {'t', 't', 'c', 'f'});
    put32(0x00010000);
    put32(2);
    const uint32_t headerEnd = 12 + 8;
    put32(headerEnd);                       // face 0 のディレクトリ位置
    put32(headerEnd + 12 + 16 * 0);         // 仮（下で直す）
    // 元フォントのディレクトリを 2 つ並べ、表本体は元のオフセット + shift になるよう並べる
    const size_t shift = headerEnd;          // 表のオフセットは shift だけずれる
    const uint16_t numTables = static_cast<uint16_t>((bytes[4] << 8) | bytes[5]);
    const size_t dirSize = 12 + 16 * static_cast<size_t>(numTables);
    // face 0 dir
    std::vector<uint8_t> dir(bytes.begin(), bytes.begin() + static_cast<ptrdiff_t>(dirSize));
    for (uint16_t i = 0; i < numTables; ++i) {
        uint8_t* rec = dir.data() + 12 + 16 * i;
        uint32_t off = (rec[8] << 24) | (rec[9] << 16) | (rec[10] << 8) | rec[11];
        off += static_cast<uint32_t>(shift + dirSize);   // 2 つ目のディレクトリのぶんもずらす
        rec[8] = static_cast<uint8_t>(off >> 24); rec[9] = static_cast<uint8_t>(off >> 16);
        rec[10] = static_cast<uint8_t>(off >> 8); rec[11] = static_cast<uint8_t>(off);
    }
    ttc.insert(ttc.end(), dir.begin(), dir.end());
    // face 1 のディレクトリ位置を書き直す
    const uint32_t dir1 = static_cast<uint32_t>(ttc.size());
    ttc[16] = static_cast<uint8_t>(dir1 >> 24); ttc[17] = static_cast<uint8_t>(dir1 >> 16);
    ttc[18] = static_cast<uint8_t>(dir1 >> 8); ttc[19] = static_cast<uint8_t>(dir1);
    ttc.insert(ttc.end(), dir.begin(), dir.end());
    ttc.insert(ttc.end(), bytes.begin() + static_cast<ptrdiff_t>(dirSize), bytes.end());
    SfntInfo t0, t1, bad;
    REQUIRE(typeset::backend::parseSfnt(ttc.data(), ttc.size(), t0, 0));
    REQUIRE(typeset::backend::parseSfnt(ttc.data(), ttc.size(), t1, 1));
    CHECK(t0.isCollection);
    CHECK(t1.isCollection);
    CHECK(t1.isCFF);
    CHECK(t1.unitsPerEm == 1000);
    CHECK(t1.dirOffset == dir1);
    CHECK_FALSE(typeset::backend::parseSfnt(ttc.data(), ttc.size(), bad, 2));
}

TEST_CASE("gradients: linear and radial paint on rects, paths and glyphs in all three backends") {
    font::FontSet fonts;
    auto latin = fonts.loadFile("data/NotoSerif-Regular.ttf", "serif");
    if (!latin) { MESSAGE("fonts not found; skipping"); return; }
    fonts.loadFile("data/NotoSerifJP-Regular.otf", "jp");

    const std::vector<GradientStop> stops{{0.0f, Color::rgb(220, 30, 30)},
                                          {0.5f, Color::rgb(240, 200, 40)},
                                          {1.0f, Color::rgb(20, 80, 220)}};
    TextStyle st;
    st.font.family = {"serif", "jp"};
    st.size = 34.0f;
    st.fill = Paint::linear(Point{0, 0}, Point{1, 1}, stops);
    inl::Paragraph para = inl::Paragraph::plain(u"Gradient 見出し", st);
    para.style.align = Align::Start;
    para.style.lineBreak.justify = false;
    inl::ParagraphLayouter layouter(fonts);
    const inl::ConstantLineShape shape(360.0f);
    const inl::ParagraphFragment frag = layouter.layout(para, WritingMode::HorizontalTb, shape);

    dl::DisplayList page;
    page.page = Size{400, 200};
    page.addRect(Rect{0, 0, 400, 200}, Color::rgb(255, 255, 255));
    // 左右に赤 → 青の線形グラデーション（BoundingBox 単位）
    page.addRect(Rect{20, 110, 160, 70},
                 Paint::linear(Point{0, 0}, Point{1, 0},
                               {{0.0f, Color::rgb(255, 0, 0)}, {1.0f, Color::rgb(0, 0, 255)}}));
    // ページ座標のグラデーションで縁取ったパス
    Path pp;
    pp.addRect(Rect{210, 110, 160, 70});
    Stroke stroke;
    stroke.width = 6.0f;
    stroke.color = Paint::linear(Point{210, 110}, Point{370, 180}, stops, PaintUnits::UserSpace);
    page.addPath(pp, std::nullopt, stroke);
    inl::emitParagraph(page, frag, WritingMode::HorizontalTb, Point{20, 50});

    // --- ラスタ: 矩形の色が位置に応じて補間される ---
    RasterRenderer r;
    RasterOptions o;
    o.dpi = 144.0f;
    const Bitmap bmp = r.render(page, o);
    REQUIRE(savePng(bmp, "build/grad_raster.png"));
    const float s = 144.0f / 72.0f;
    auto pixel = [&](float px, float py) {
        return bmp.row(static_cast<int>(py * s))[static_cast<int>(px * s)];
    };
    const uint32_t left = pixel(25.0f, 145.0f);
    const uint32_t mid = pixel(100.0f, 145.0f);
    const uint32_t right = pixel(175.0f, 145.0f);
    auto red = [](uint32_t p) { return static_cast<int>((p >> 16) & 0xFF); };
    auto blue = [](uint32_t p) { return static_cast<int>(p & 0xFF); };
    CHECK(red(left) > 200);
    CHECK(blue(left) < 60);
    CHECK(blue(right) > 200);
    CHECK(red(right) < 60);
    // 中央はおよそ半分ずつ
    CHECK(red(mid) > 100);
    CHECK(red(mid) < 160);
    CHECK(blue(mid) > 100);
    CHECK(blue(mid) < 160);
    // 単調に変化する
    CHECK(red(left) > red(mid));
    CHECK(red(mid) > red(right));

    // グリフもグラデーションで塗られる（見出しの左端は赤寄り、右端は青寄り）
    int leftRed = 0, rightBlue = 0;
    for (int y = static_cast<int>(20 * s); y < static_cast<int>(60 * s); ++y) {
        for (int x = static_cast<int>(20 * s); x < static_cast<int>(70 * s); ++x) {
            const uint32_t p = bmp.row(y)[x];
            if (red(p) > 150 && blue(p) < 100) ++leftRed;
        }
        for (int x = static_cast<int>(250 * s); x < static_cast<int>(340 * s); ++x) {
            const uint32_t p = bmp.row(y)[x];
            if (blue(p) > 120 && red(p) < 120) ++rightBlue;
        }
    }
    CHECK(leftRed > 20);
    CHECK(rightBlue > 20);

    // --- PDF: シェーディングパターンが入る ---
    PdfWriter pdf;
    pdf.setCompressStreams(false);      // 中身を検査するため
    pdf.addPage(page);
    REQUIRE(pdf.save("build/grad.pdf"));
    CHECK(pdf.warnings().empty());
    {
        std::ifstream in("build/grad.pdf", std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(bytes.find("/PatternType 2") != std::string::npos);
        CHECK(bytes.find("/ShadingType 2") != std::string::npos);
        CHECK(bytes.find("/Pattern cs") != std::string::npos);   // 塗り
        CHECK(bytes.find("/Pattern CS") != std::string::npos);   // 縁取り
        CHECK(bytes.find("/FunctionType 3") != std::string::npos);
    }

    // --- SVG: linearGradient と、グリフをクリップした矩形 ---
    REQUIRE(saveSvg(page, "build/grad.svg"));
    {
        const std::string svg = writeSvg(page);
        CHECK(svg.find("<linearGradient") != std::string::npos);
        CHECK(svg.find("gradientUnits=\"userSpaceOnUse\"") != std::string::npos);
        CHECK(svg.find("clip-path=\"url(#gc") != std::string::npos);
        size_t grads = 0;
        for (size_t p = svg.find("<linearGradient"); p != std::string::npos;
             p = svg.find("<linearGradient", p + 1)) ++grads;
        // 矩形の塗り・パスの縁取り・グリフ（欧文と和文で face が変わるので run は 2 つ）
        CHECK(grads == 4);
    }
}

TEST_CASE("antialias off renders binary coverage (no intermediate greys)") {
    font::FontSet fonts;
    if (!fonts.loadFile("data/NotoSerif-Regular.ttf", "serif")) { MESSAGE("fonts not found; skipping"); return; }
    TextStyle st;
    st.font.family = {"serif"};
    st.size = 18.0f;
    inl::Paragraph para = inl::Paragraph::plain(u"Ag", st);
    inl::ParagraphLayouter layouter(fonts);
    const inl::ConstantLineShape shape(200.0f);
    const inl::ParagraphFragment frag = layouter.layout(para, WritingMode::HorizontalTb, shape);
    dl::DisplayList list;
    list.page = Size{80, 40};
    // 斜めの辺を持つ図形も入れる（矩形の塗りと 2 値化の両方を見る）
    Path tri;
    tri.moveTo(5, 35);
    tri.lineTo(30, 5);
    tri.lineTo(55, 35);
    tri.close();
    list.addPath(tri, Color::rgb(0, 0, 0));
    inl::emitParagraph(list, frag, WritingMode::HorizontalTb, Point{5, 20});

    RasterOptions aa;
    aa.dpi = 72.0f;
    RasterOptions bin = aa;
    bin.antialias = false;
    const Bitmap a = RasterRenderer().render(list, aa);
    const Bitmap b = RasterRenderer().render(list, bin);

    auto greys = [](const Bitmap& bmp) {
        size_t n = 0;
        for (int y = 0; y < bmp.height; ++y) {
            for (int x = 0; x < bmp.width; ++x) {
                const int v = static_cast<int>(bmp.row(y)[x] & 0xFF);
                if (v != 0 && v != 255) ++n;
            }
        }
        return n;
    };
    CHECK(greys(a) > 50);      // AA あり: 中間調が出る
    CHECK(greys(b) == 0);      // AA なし: 白と黒だけ
    // インクの総量は大きく変わらない（形は保たれる）
    auto ink = [](const Bitmap& bmp) {
        size_t n = 0;
        for (int y = 0; y < bmp.height; ++y) {
            for (int x = 0; x < bmp.width; ++x) if ((bmp.row(y)[x] & 0xFF) < 128) ++n;
        }
        return n;
    };
    const double ia = static_cast<double>(ink(a));
    const double ib = static_cast<double>(ink(b));
    CHECK(ib > ia * 0.85);
    CHECK(ib < ia * 1.15);
}
