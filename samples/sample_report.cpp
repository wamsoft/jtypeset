/**
 * sample_report.cpp — レポート（見出しの自動採番・目次・図表番号と相互参照・箇条書き・
 * コードブロック・行内画像・セルの縦位置・行の途中でのページまたぎ・PDF のしおり・最終ページの段揃え）
 *
 * ※ リポジトリルートで実行すること（フォントを ./data/ から読む）
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

std::shared_ptr<dl::Image> makeImage(int w, int h, uint8_t r0, uint8_t g0, uint8_t b0) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* p = px.data() + (static_cast<size_t>(y) * w + x) * 4;
            const bool grid = (x % 16 == 0) || (y % 16 == 0);
            p[0] = static_cast<uint8_t>(grid ? 60 : r0 + 120 * x / w);
            p[1] = static_cast<uint8_t>(grid ? 60 : g0 + 100 * y / h);
            p[2] = static_cast<uint8_t>(grid ? 60 : b0);
            p[3] = 255;
        }
    }
    return image::fromRgba(w, h, px.data());
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::printf("=== typeset sample_report ===\n");

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
    h1.size = 16.0f;
    TextStyle h2 = h1;
    h2.size = 12.5f;
    TextStyle title = h1;
    title.size = 24.0f;
    TextStyle smallText = body;
    smallText.font.family = {"sans-ja", "sans"};
    smallText.size = 8.5f;
    smallText.fill = Color::rgb(70, 70, 70);
    TextStyle code = body;
    code.font.family = {"sans", "sans-ja"};
    code.size = 8.5f;
    code.fill = Color::rgb(30, 30, 90);
    TextStyle cellText = body;
    cellText.size = 9.0f;
    TextStyle cellHead = cellText;
    cellHead.font.family = {"sans-ja", "sans"};

    ParagraphStyle bodyStyle;
    bodyStyle.lineHeight = 1.7f;
    bodyStyle.firstLineIndent = 1.0f;
    ParagraphStyle headStyle;
    headStyle.lineHeight = 1.5f;
    headStyle.align = Align::Start;
    ParagraphStyle centerStyle;
    centerStyle.align = Align::Center;
    centerStyle.lineHeight = 1.6f;
    ParagraphStyle codeStyle;
    codeStyle.align = Align::Start;
    codeStyle.lineHeight = 1.45f;
    codeStyle.preserveSpaces = true;
    codeStyle.spacing.punctuationSpacing = false;
    codeStyle.spacing.latinGap = false;
    ParagraphStyle cellStyle;
    cellStyle.lineHeight = 1.4f;
    cellStyle.align = Align::Start;
    cellStyle.spacing.kanjiSkipStretch = 0.0f;

    page::PageSequence seq;
    seq.master.size = page::paper::A4;
    seq.master.writingMode = WritingMode::HorizontalTb;
    seq.master.margin = page::Margins{25.0f * kMm, 25.0f * kMm, 24.0f * kMm, 20.0f * kMm};
    {
        page::RunningText h;
        h.para = inl::Paragraph::plain(u"{title}", smallText);
        h.para.style.align = Align::Start;
        h.offset = 8.0f * kMm;
        seq.master.header = h;
        page::RunningText f;
        f.para = inl::Paragraph::plain(u"{page} / {pages}", smallText);
        f.para.style.align = Align::Center;
        f.offset = 10.0f * kMm;
        seq.master.footer = f;
    }

    block::Flow flow;
    auto heading = [&](const char16_t* t, int level, const std::string& label = std::string()) {
        inl::Paragraph p = inl::Paragraph::plain(t, level == 1 ? h1 : h2, headStyle);
        block::BlockStyle bs;
        bs.keepWithNext = true;
        bs.spaceBefore = level == 1 ? 14.0f : 8.0f;
        bs.spaceAfter = level == 1 ? 6.0f : 3.0f;
        bs.label = label;
        flow.addHeading(std::move(p), level, bs, true);
    };
    auto para = [&](const char16_t* t) {
        block::BlockStyle bs;
        bs.orphans = 2;
        bs.widows = 2;
        bs.spaceAfter = 3.0f;
        flow.addParagraph(inl::Paragraph::plain(t, body, bodyStyle), bs);
    };

    // --- 表題 ---
    {
        block::BlockStyle bs;
        bs.spaceBefore = 40.0f;
        flow.addParagraph(inl::Paragraph::plain(u"typeset 組版エンジン 評価レポート", title, centerStyle), bs);
        flow.addParagraph(inl::Paragraph::plain(u"— 縦横共通の日本語組版と三つの出力先 —", h2, centerStyle));
        block::BlockStyle bs2;
        bs2.spaceBefore = 12.0f;
        bs2.spaceAfter = 24.0f;
        flow.addParagraph(inl::Paragraph::plain(u"2026 年 9 月 8 日　組版基盤チーム", smallText, centerStyle), bs2);
    }

    // --- 目次 ---
    {
        flow.addParagraph(inl::Paragraph::plain(u"目次", h2, headStyle));
        block::TocBlock toc;
        toc.style = body;
        TextStyle sub = body;
        sub.size = 9.5f;
        toc.subStyle = sub;
        toc.maxLevel = 2;
        toc.block.spaceAfter = 10.0f;
        flow.addToc(toc);
        flow.addPageBreak();
    }

    // --- 本文 ---
    heading(u"はじめに", 1, "sec-intro");
    para(u"本レポートは、typeset 組版エンジンの評価結果をまとめたものである。第 {ref:sec-arch} 章で構成を述べ、"
         u"第 {ref:sec-eval} 章（{page:sec-eval} ページ）で評価結果を示す。図 {ref:fig-arch} に全体の構成を、"
         u"表 {ref:tab-bench} に出力先ごとの比較を示す。");
    {
        // 脚注: 本文中の記号は上付き、注はこの段の末尾に置かれる（番号は文書を通して連番）
        TextStyle noteStyle = body;
        noteStyle.size = 8.0f;
        const TextStyle marker = inl::superscriptStyle(body);
        inl::Paragraph p;
        p.style = bodyStyle;
        p.runs.push_back(inl::InlineRun{u"レポートに必要な要素として、見出しの自動採番、目次、図表番号と相互参照", body});
        p.addFootnote(inl::Paragraph::plain(u"相互参照は {ref:label} と {page:label} で、番号とページを別々に引ける。", noteStyle), marker);
        p.runs.push_back(inl::InlineRun{u"、箇条書き、コードブロック、行内の小さな画像、表のセルの縦位置、長いセルのページまたぎ、"
                                        u"PDF のしおり、脚注", body});
        p.addFootnote(inl::Paragraph::plain(u"脚注は記号のある行が載った段の末尾に集まり、本文との間に短い罫が入る。"
                                            u"段に入らなければ本文ごと次の段へ送られる。", noteStyle), marker);
        p.runs.push_back(inl::InlineRun{u"を確認する。", body});
        block::BlockStyle bs;
        bs.orphans = 2;
        bs.widows = 2;
        bs.spaceAfter = 3.0f;
        flow.addParagraph(p, bs);
    }

    heading(u"構成", 1, "sec-arch");
    heading(u"レイヤー", 2, "sec-layers");
    para(u"組版は次の層に分かれる。");
    {
        block::ListBlock list;
        list.marker = block::ListBlock::Marker::Bullet;
        list.items.push_back(inl::Paragraph::plain(u"text — JLReq の文字クラスとアキ量表、UAX #14 / #50", body, headStyle));
        list.items.push_back(inl::Paragraph::plain(u"inl — Itemizer、HarfBuzz、Box/Glue/Penalty、行分割（Greedy / Knuth–Plass）。行長は行ごとに変えられる", body, headStyle));
        list.items.push_back(inl::Paragraph::plain(u"block / page — 段落・見出し・表・画像・目次、ページマスタ、段組、回り込み、柱・ノンブル", body, headStyle));
        list.items.push_back(inl::Paragraph::plain(u"backend — ラスタ、PDF（Identity-H、hb-subset、しおり）、SVG", body, headStyle));
        list.itemGap = 2.0f;
        list.block.spaceAfter = 6.0f;
        flow.addList(list);
    }
    {
        block::ImageBlock img;
        img.image = makeImage(320, 140, 40, 80, 160);
        img.size = Size{120.0f * kMm, 0.0f};
        img.placement = block::ImagePlacement::Block;
        img.caption = inl::Paragraph::plain(u"{fig}　全体構成（図番号は自動）", smallText, centerStyle);
        img.block.label = "fig-arch";
        img.block.spaceBefore = 4.0f;
        img.block.spaceAfter = 8.0f;
        flow.addImage(std::move(img));
    }

    heading(u"使い方", 2, "sec-usage");
    para(u"Python から使う場合の最小の例を示す。コードブロックは空白を保持し、背景を付ける。");
    {
        inl::Paragraph p = inl::Paragraph::plain(
            u"import typeset as ts\n"
            u"fonts = ts.FontSet()\n"
            u"fonts.load_file(\"data/NotoSerifJP-Regular.otf\", \"serif-ja\")\n"
            u"body = ts.TextStyle([\"serif-ja\"], 10.0)\n"
            u"flow = ts.Flow()\n"
            u"for i in range(3):\n"
            u"    p = ts.Paragraph(f\"段落 {i}\", body)   # 一字下げは ParagraphStyle で\n"
            u"    flow.add_paragraph(p)\n"
            u"pages = ts.FlowLayouter(fonts).layout(flow, ts.PageSequence())\n"
            u"ts.save_pdf(pages, \"out.pdf\")",
            code, codeStyle);
        block::BlockStyle bs;
        bs.background = Color::rgb(243, 243, 238);
        bs.padding = 6.0f;
        bs.keepTogether = true;
        bs.spaceAfter = 8.0f;
        flow.addParagraph(std::move(p), bs);
    }
    {
        // 行内画像
        inl::Paragraph p;
        p.style = bodyStyle;
        p.runs.push_back(inl::InlineRun{u"行の途中に小さな画像 ", body});
        p.addImage(makeImage(64, 64, 200, 120, 40), Size{11.0f, 11.0f}, body);
        p.runs.push_back(inl::InlineRun{u" を置くこともできる。行内画像は 1 文字ぶんの箱として組版され、中心が行の中心線に揃う。"
                                         u"アイコンや記号の差し込みに使う。", body});
        block::BlockStyle bs;
        bs.spaceAfter = 3.0f;
        flow.addParagraph(std::move(p), bs);
    }

    heading(u"評価", 1, "sec-eval");
    heading(u"出力先の比較", 2, "sec-compare");
    para(u"表 {ref:tab-bench} に、同じ組版結果を三つの出力先へ書き出したときの比較を示す。セル内の縦位置は列ごとに"
         u"上・中央・下を指定している。");
    {
        block::TableBlock t;
        t.caption = inl::Paragraph::plain(u"{table}　出力先の比較", smallText, headStyle);
        t.block.label = "tab-bench";
        t.columns = {block::TableColumn{0.0f, Align::Start}, block::TableColumn{0.0f, Align::Center},
                     block::TableColumn{0.0f, Align::Start}, block::TableColumn{0.0f, Align::Center}};
        auto cell = [&](const char16_t* s, const TextStyle& st, block::VAlign v = block::VAlign::Top) {
            block::TableCell c;
            c.paras.push_back(inl::Paragraph::plain(s, st, cellStyle));
            c.valign = v;
            return c;
        };
        block::TableRow head;
        head.header = true;
        head.cells = {cell(u"出力先", cellHead), cell(u"サイズ", cellHead), cell(u"備考", cellHead), cell(u"位置", cellHead)};
        t.rows.push_back(head);
        struct R { const char16_t* a; const char16_t* b; const char16_t* c; const char16_t* d; block::VAlign v; };
        const std::vector<R> rows = {
            {u"ラスタ", u"—", u"glyphware のカバレッジマスクを自前で合成する。パスは自前のスキャンライン AA。dpi を掛けてピクセルへ", u"上", block::VAlign::Top},
            {u"PDF", u"274 KB", u"Identity-H でグリフ ID を直書きし、hb-subset で使用グリフだけを埋め込む。Flate 圧縮、画像 XObject、しおり", u"中央", block::VAlign::Middle},
            {u"SVG", u"436 KB", u"グリフは <defs> に 1 回置いて <use> で参照する。画像は data URI", u"下", block::VAlign::Bottom},
        };
        for (const R& r : rows) {
            block::TableRow row;
            row.cells = {cell(r.a, cellText, r.v), cell(r.b, cellText, r.v), cell(r.c, cellText), cell(r.d, cellText, r.v)};
            t.rows.push_back(row);
        }
        t.block.spaceAfter = 8.0f;
        flow.addTable(std::move(t));
    }

    heading(u"長い行のページまたぎ", 2, "sec-longrow");
    para(u"1 行がページより高くなる表は、行を分けて次のページへ続ける。ヘッダ行は繰り返される。");
    {
        block::TableBlock t;
        t.caption = inl::Paragraph::plain(u"{table}　長いセルを持つ表", smallText, headStyle);
        t.block.label = "tab-long";
        t.columns = {block::TableColumn{70.0f, Align::Start}, block::TableColumn{0.0f, Align::Start}};
        auto cell = [&](std::u16string s, const TextStyle& st) {
            block::TableCell c;
            c.paras.push_back(inl::Paragraph::plain(std::move(s), st, cellStyle));
            return c;
        };
        block::TableRow head;
        head.header = true;
        head.cells = {cell(u"項目", cellHead), cell(u"説明", cellHead)};
        t.rows.push_back(head);
        std::u16string longText;
        for (int i = 0; i < 90; ++i) {
            longText += u"この段落は長いセルの例で、1 ページに収まらない高さになるまで繰り返している。";
        }
        block::TableRow r1;
        r1.cells = {cell(u"長い説明", cellText), cell(longText, cellText)};
        t.rows.push_back(r1);
        block::TableRow r2;
        r2.cells = {cell(u"短い説明", cellText), cell(u"続きの行。", cellText)};
        t.rows.push_back(r2);
        t.block.spaceAfter = 8.0f;
        flow.addTable(std::move(t));
    }

    heading(u"まとめ", 1, "sec-summary");
    flow.add(block::SectionBlock{2, 8.0f * kMm});
    for (int i = 0; i < 5; ++i) {
        para(u"最終ページは段の高さが揃うように組み直される。2 段組のまとめの文章をここに置いて、左右の段の行数が"
             u"ほぼ同じになることを確かめる。段抜きの見出しと同じ機構（ページ先頭の再開点からの組み直し）で実現している。");
    }

    page::FlowLayouter layouter(fonts);
    page::FlowLayoutOptions opts;
    opts.fields[u"title"] = u"typeset 評価レポート";
    opts.drawGuides = true;
    const std::vector<page::Page> pages = layouter.layout(flow, seq, opts);
    std::printf("pages: %zu\n", pages.size());

    sample::savePages(pages, "output_report", 120.0f, 6);
    return 0;
}
