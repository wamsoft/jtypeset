/**
 * sample_script.cpp — 台本（Phase 2 の確認サンプル）
 *
 * A5 縦組み。名前欄＋本文のラベル付き段落、場の見出し（keepWithNext）、柱・ノンブル、
 * 複数ページへの流し込み。
 *
 * ※ リポジトリルートから実行すること（フォントを ./data/ から読む）
 */

#include <cstdio>
#include <string>
#include <vector>

#include "sample_common.hpp"
#include "typeset/block/block.hpp"
#include "typeset/font/font_set.hpp"
#include "typeset/page/flow_layouter.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace typeset;

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::printf("=== typeset sample_script (Phase 2) ===\n");

    font::FontSet fonts;
    auto serifJp = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
    auto sansJp = fonts.loadFile("data/NotoSansJP-Regular.otf", "sans-ja");
    auto serif = fonts.loadFile("data/NotoSerif-Regular.ttf", "serif");
    auto sans = fonts.loadFile("data/NotoSans-Regular.ttf", "sans");
    if (!serifJp || !sansJp || !serif || !sans) {
        std::fprintf(stderr, "fonts not found under ./data (run `make fontdata`)\n");
        return 1;
    }

    // --- スタイル ---
    TextStyle body;
    body.font.family = {"serif-ja", "serif"};
    body.size = 11.0f;

    TextStyle name = body;
    name.font.family = {"sans-ja", "sans"};
    name.font.weight = 700;   // Bold が無いのでフェイクボールドになる

    TextStyle heading = name;
    heading.size = 14.0f;

    TextStyle runningStyle;
    runningStyle.font.family = {"sans-ja", "sans"};
    runningStyle.size = 8.0f;
    runningStyle.fill = Color::rgb(90, 90, 90);

    ParagraphStyle bodyStyle;
    bodyStyle.lineHeight = 1.8f;
    bodyStyle.firstLineIndent = 0.0f;
    bodyStyle.spacing.hangingPunctuation = true;

    ParagraphStyle nameStyle = bodyStyle;
    nameStyle.align = Align::Start;

    // --- ページマスタ ---
    page::PageSequence seq;
    seq.master.size = page::paper::A5;
    seq.master.writingMode = WritingMode::VerticalRl;
    seq.master.margin = page::Margins{22.0f * kMm, 18.0f * kMm, 18.0f * kMm, 14.0f * kMm};
    seq.master.duplex = true;
    {
        page::RunningText h;
        h.para = inl::Paragraph::plain(u"{title}　　第一稿", runningStyle);
        h.para.style.align = Align::End;
        h.offset = 7.0f * kMm;
        seq.master.header = h;
        page::RunningText f;
        f.para = inl::Paragraph::plain(u"— {page} / {pages} —", runningStyle);
        f.para.style.align = Align::Center;
        f.offset = 8.0f * kMm;
        seq.master.footer = f;
    }

    // --- 本文（台本）---
    block::Flow flow;
    const Pt labelWidth = body.size * 5.0f;   // 名前欄 5 文字
    const Pt labelGap = body.size * 1.0f;

    struct Cue { const char16_t* who; const char16_t* what; };
    const std::vector<Cue> scene1 = {
        {u"ト書き", u"放課後の教室。窓から夕日が差し込んでいる。太郎が机に座り、ノートを広げている。"},
        {u"太郎", u"なあ、花子。この問題、どう解くんだっけ。"},
        {u"花子", u"また？　昨日教えたでしょう。三角形の内角の和は百八十度。それを使うの。"},
        {u"太郎", u"……ああ、そうか。じゃあ、この角は六十度で……。"},
        {u"花子", u"そう。で、こっちが四十五度だから、残りは？"},
        {u"太郎", u"七十五度！"},
        {u"花子", u"正解。ほら、ちゃんと考えれば分かるじゃない。"},
        {u"ト書き", u"太郎、照れたように頭を掻く。花子は窓の外を見る。"},
        {u"花子", u"（小さく）……来年も、こうしていられるかな。"},
        {u"太郎", u"え？　何か言った？"},
        {u"花子", u"何でもない。ほら、次の問題。「Wagahai は 24 年に語った」——じゃなくて、二番の応用問題。"},
        {u"ナレーション", u"二人の影が、長く教室の床に伸びていた。チャイムが鳴る。遠くで部活の掛け声が聞こえる。"},
    };
    const std::vector<Cue> scene2 = {
        {u"ト書き", u"翌朝。通学路。桜並木の下を花子が歩いている。後ろから太郎が走ってくる。"},
        {u"太郎", u"花子ーっ！　待ってくれよ！"},
        {u"花子", u"遅いよ。また寝坊？"},
        {u"太郎", u"違うって。昨日の問題、家で全部解き直したんだ。ほら、見て。"},
        {u"花子", u"……へえ。全部、合ってる。"},
        {u"太郎", u"だろ？　だから今日は、俺が花子に教えてやるよ。"},
        {u"花子", u"（笑って）それは楽しみ。"},
        {u"ナレーション", u"花びらが一枚、二人の間をすり抜けて落ちた。"},
    };

    auto addScene = [&](const char16_t* title, const std::vector<Cue>& cues, bool pageBreak) {
        inl::Paragraph h = inl::Paragraph::plain(title, heading);
        h.style = bodyStyle;
        h.style.lineHeight = 2.2f;
        h.style.firstLineIndent = 0.0f;
        block::BlockStyle hs;
        hs.keepWithNext = true;
        hs.spaceAfter = body.size * 0.5f;
        if (pageBreak) hs.breakBefore = block::BreakKind::Page;
        flow.addHeading(std::move(h), 1, hs);

        for (const Cue& c : cues) {
            inl::Paragraph label = inl::Paragraph::plain(c.who, name, nameStyle);
            inl::Paragraph text = inl::Paragraph::plain(c.what, body, bodyStyle);
            block::BlockStyle bs;
            bs.orphans = 2;
            bs.widows = 2;
            bs.spaceAfter = body.size * 0.3f;
            flow.addLabeled(std::move(label), std::move(text), labelWidth, labelGap, bs);
        }
    };
    addScene(u"第一場　教室", scene1, false);
    // 同じ場をもう一度足してページをまたがせる
    addScene(u"第一場（続き）", scene1, false);
    addScene(u"第二場　通学路", scene2, true);

    // --- 流し込み ---
    page::FlowLayouter layouter(fonts);
    page::FlowLayoutOptions opts;
    opts.fields[u"title"] = u"放課後の三角形";
    opts.drawGuides = true;
    const std::vector<page::Page> pages = layouter.layout(flow, seq, opts);
    std::printf("pages: %zu\n", pages.size());

    sample::savePages(pages, "output_script", 144.0f, 3);
    return 0;
}
