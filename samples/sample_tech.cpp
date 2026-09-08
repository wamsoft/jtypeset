/**
 * sample_tech.cpp — 技術文書（Phase 3 の確認サンプル）
 *
 * A4 横組み。見出し、一字下げの本文、回り込みの図（キャプション付き）、
 * 中央配置の図、表（自動列幅・ヘッダ行・colspan・ページまたぎ）、罫線、途中から 2 段組。
 *
 * ※ リポジトリルートから実行すること（フォントを ./data/ から読む）
 */

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sample_common.hpp"
#include "typeset/block/block.hpp"
#include "typeset/font/font_set.hpp"
#include "typeset/image/image.hpp"
#include "typeset/page/flow_layouter.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace typeset;

namespace {

/// 合成画像（グラデーション＋格子。半透明の角を持つ）
std::shared_ptr<dl::Image> makeImage(int w, int h, bool alphaCorner) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* p = px.data() + (static_cast<size_t>(y) * w + x) * 4;
            const bool grid = (x % 20 == 0) || (y % 20 == 0);
            p[0] = static_cast<uint8_t>(grid ? 40 : 60 + 180 * x / w);
            p[1] = static_cast<uint8_t>(grid ? 40 : 90 + 120 * y / h);
            p[2] = static_cast<uint8_t>(grid ? 40 : 200 - 100 * x / w);
            p[3] = 255;
            if (alphaCorner) {
                const float dx = static_cast<float>(x) / w, dy = static_cast<float>(y) / h;
                if (dx + dy > 1.6f) p[3] = static_cast<uint8_t>(255 * std::max(0.0f, (2.0f - dx - dy) / 0.4f));
            }
        }
    }
    return image::fromRgba(w, h, px.data());
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::printf("=== typeset sample_tech (Phase 3) ===\n");

    font::FontSet fonts;
    auto sansJp = fonts.loadFile("data/NotoSansJP-Regular.otf", "sans-ja");
    auto serifJp = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
    auto sans = fonts.loadFile("data/NotoSans-Regular.ttf", "sans");
    auto serif = fonts.loadFile("data/NotoSerif-Regular.ttf", "serif");
    if (!sansJp || !serifJp || !sans || !serif) {
        std::fprintf(stderr, "fonts not found under ./data (run `make fontdata`)\n");
        return 1;
    }

    TextStyle body;
    body.font.family = {"serif-ja", "serif"};
    body.size = 10.0f;

    TextStyle h1 = body;
    h1.font.family = {"sans-ja", "sans"};
    h1.size = 18.0f;
    TextStyle h2 = h1;
    h2.size = 13.0f;

    TextStyle caption = body;
    caption.font.family = {"sans-ja", "sans"};
    caption.size = 8.5f;
    caption.fill = Color::rgb(60, 60, 60);

    TextStyle cellText = body;
    cellText.size = 9.0f;
    TextStyle cellHead = cellText;
    cellHead.font.family = {"sans-ja", "sans"};

    TextStyle runningStyle = caption;
    runningStyle.size = 8.0f;

    ParagraphStyle bodyStyle;
    bodyStyle.lineHeight = 1.7f;
    bodyStyle.firstLineIndent = 1.0f;
    ParagraphStyle headStyle;
    headStyle.lineHeight = 1.5f;
    headStyle.align = Align::Start;
    ParagraphStyle cellStyle;
    cellStyle.lineHeight = 1.4f;
    cellStyle.align = Align::Start;
    cellStyle.spacing.kanjiSkipStretch = 0.0f;

    page::PageSequence seq;
    seq.master.size = page::paper::A4;
    seq.master.writingMode = WritingMode::HorizontalTb;
    seq.master.margin = page::Margins{25.0f * kMm, 25.0f * kMm, 22.0f * kMm, 22.0f * kMm};
    {
        page::RunningText h;
        h.para = inl::Paragraph::plain(u"{title}", runningStyle);
        h.para.style.align = Align::Start;
        h.offset = 8.0f * kMm;
        seq.master.header = h;
        page::RunningText f;
        f.para = inl::Paragraph::plain(u"{page}", runningStyle);
        f.para.style.align = Align::Center;
        f.offset = 10.0f * kMm;
        seq.master.footer = f;
    }

    block::Flow flow;
    auto heading = [&](const char16_t* t, const TextStyle& st, int level, Pt before, Pt after) {
        inl::Paragraph p = inl::Paragraph::plain(t, st, headStyle);
        block::BlockStyle bs;
        bs.keepWithNext = true;
        bs.spaceBefore = before;
        bs.spaceAfter = after;
        flow.addHeading(std::move(p), level, bs);
    };
    auto para = [&](const char16_t* t) {
        inl::Paragraph p = inl::Paragraph::plain(t, body, bodyStyle);
        block::BlockStyle bs;
        bs.orphans = 2;
        bs.widows = 2;
        bs.spaceAfter = 3.0f;
        flow.addParagraph(std::move(p), bs);
    };

    heading(u"typeset ライブラリの構成", h1, 1, 0.0f, 10.0f);
    para(u"本書は typeset ライブラリの構成を説明する技術文書のサンプルである。縦書きと横書きの日本語組版を同じエンジンで扱い、"
         u"ラスタ・PDF・SVG の三つの出力先へ同一の組版結果を書き出す。ここでは横組みの技術文書に必要な、見出し、図の回り込み、"
         u"表、罫線、段組を確認する。");

    heading(u"1. 図の回り込み", h2, 2, 8.0f, 4.0f);
    {
        block::ImageBlock img;
        img.image = makeImage(240, 180, true);
        img.size = Size{60.0f * kMm, 0.0f};
        img.placement = block::ImagePlacement::FloatStart;
        img.gap = 8.0f;
        img.caption = inl::Paragraph::plain(u"図 1　回り込みの例（左寄せ、半透明の角）", caption);
        img.caption->style.lineHeight = 1.4f;
        flow.addImage(std::move(img));
    }
    para(u"図を行頭側に寄せると、本文はその右側を回り込む。行ごとの行長は Region の排除領域から求め、"
         u"行分割器には LineShapeProvider 経由で渡す。TeX の \\parshape と同じ考え方で、回り込みも、ラベル付き段落も、"
         u"将来の吹き出しも、すべて「行番号から行の形を返す関数」として表現する。");
    para(u"排除領域は段（Region）が変わると消える。図より本文が短ければ、次のブロックも同じ排除領域を避けて流れる。"
         u"図が段の残りに入らないときは次の段へ送られ、段より大きい図は段に収まるように縮められる。"
         u"キャプションは図の下に置き、その高さも排除領域に含める。");
    para(u"この段落は図の下端を過ぎたところで全幅に戻る。行送りは固定ピッチなので、図の高さが行送りの整数倍でなくても"
         u"本文の行位置は揃ったままである。JLReq の「行の中心線」モデルをそのまま使っている。");

    heading(u"2. 図の中央配置", h2, 2, 8.0f, 4.0f);
    {
        block::ImageBlock img;
        img.image = makeImage(320, 120, false);
        img.size = Size{110.0f * kMm, 0.0f};
        img.placement = block::ImagePlacement::Block;
        img.align = Align::Center;
        img.caption = inl::Paragraph::plain(u"図 2　中央配置（Block）。PDF では Flate、SVG では data URI で埋め込む", caption);
        img.caption->style.lineHeight = 1.4f;
        img.block.spaceBefore = 4.0f;
        img.block.spaceAfter = 8.0f;
        flow.addImage(std::move(img));
    }

    heading(u"3. 表", h2, 2, 8.0f, 4.0f);
    para(u"表は TeX の tabular 水準を目標にしている。列幅は固定か自動（内容の自然幅から配分）、罫線は外枠・内側・ヘッダ下で"
         u"太さを変えられ、colspan とヘッダ行の繰り返しに対応する。");
    {
        block::TableBlock t;
        t.columns = {block::TableColumn{0.0f, Align::Start}, block::TableColumn{0.0f, Align::Center},
                     block::TableColumn{0.0f, Align::Start}};
        auto cell = [&](const char16_t* s, const TextStyle& st) {
            block::TableCell c;
            c.paras.push_back(inl::Paragraph::plain(s, st, cellStyle));
            return c;
        };
        block::TableRow head;
        head.header = true;
        head.cells = {cell(u"層", cellHead), cell(u"名前空間", cellHead), cell(u"役割", cellHead)};
        t.rows.push_back(head);
        struct R { const char16_t* a; const char16_t* b; const char16_t* c; };
        const std::vector<R> rows = {
            {u"文字", u"text", u"JLReq の文字クラス・アキ量表・UAX #50・UAX #14（libunibreak）"},
            {u"フォント", u"font", u"glyphware の包み。フォールバック解決とシェイピング用 hb_font"},
            {u"行内", u"inl", u"Itemizer → HarfBuzz → Box/Glue/Penalty → 行分割（Greedy / Knuth–Plass）→ LineBox"},
            {u"ブロック", u"block", u"段落・見出し・罫線・ラベル付き段落・画像・表・セクション"},
            {u"ページ", u"page", u"ページマスタ・段・流し込み・柱・ノンブル・回り込み"},
            {u"表示リスト", u"dl", u"GlyphRun / Path / Rect / Image / Group。backend の入力"},
            {u"出力", u"backend", u"ラスタ（glyphware マスク合成）・PDF（Identity-H）・SVG（defs + use）"},
        };
        for (const R& r : rows) {
            block::TableRow row;
            row.cells = {cell(r.a, cellText), cell(r.b, cellText), cell(r.c, cellText)};
            t.rows.push_back(row);
        }
        block::TableRow span;
        block::TableCell sc = cell(u"備考: 依存ライブラリは FreeType / HarfBuzz / SheenBidi / libunibreak / zlib / stb。ICU と minikin は使わない。", cellText);
        sc.colspan = 3;
        span.cells = {sc};
        t.rows.push_back(span);
        t.block.spaceAfter = 8.0f;
        flow.addTable(std::move(t));
    }

    heading(u"4. 罫線と 2 段組", h2, 2, 8.0f, 4.0f);
    para(u"次の罫線の後で段数を 2 に切り替える。段数の変更はページ単位なので、ここで改ページになる。");
    flow.addRule(0.75f, Color::rgb(0, 0, 0));
    flow.add(block::SectionBlock{2, 8.0f * kMm});
    for (int i = 0; i < 6; ++i) {
        para(u"二段組の本文。段の幅が狭くなるので行分割の候補が減り、追い込み・追い出しの働きが見やすい。約物の詰め、和欧間の四分アキ、"
             u"英単語 typeset や HarfBuzz の内部では切らない、といった規則は段の幅に関係なく同じである。");
    }
    {
        block::ImageBlock img;
        img.image = makeImage(160, 160, false);
        img.size = Size{28.0f * kMm, 0.0f};
        img.placement = block::ImagePlacement::FloatEnd;
        img.caption = inl::Paragraph::plain(u"図 3　右寄せ", caption);
        img.caption->style.lineHeight = 1.4f;
        flow.addImage(std::move(img));
    }
    for (int i = 0; i < 4; ++i) {
        para(u"右寄せの図を回り込む段落。段の途中に置かれた図は、その段の排除領域として残りの段落に効く。");
    }

    page::FlowLayouter layouter(fonts);
    page::FlowLayoutOptions opts;
    opts.fields[u"title"] = u"typeset ライブラリの構成";
    opts.drawGuides = true;
    const std::vector<page::Page> pages = layouter.layout(flow, seq, opts);
    std::printf("pages: %zu\n", pages.size());

    sample::savePages(pages, "output_tech", 120.0f, 3);
    return 0;
}
