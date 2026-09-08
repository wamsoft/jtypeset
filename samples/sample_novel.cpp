/**
 * sample_novel.cpp — 小説本文（Phase 2 の確認サンプル）
 *
 * B6 縦組み 2 段。章見出し（改ページ・段抜き）、一字下げ、ルビ、ぶら下げ、柱・ノンブル。
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

namespace {

size_t findU(const std::u16string& hay, const char16_t* needle, size_t from = 0) {
    return hay.find(needle, from);
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::printf("=== typeset sample_novel (Phase 2) ===\n");

    font::FontSet fonts;
    auto serifJp = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
    auto sansJp = fonts.loadFile("data/NotoSansJP-Regular.otf", "sans-ja");
    auto serif = fonts.loadFile("data/NotoSerif-Regular.ttf", "serif");
    if (!serifJp || !sansJp || !serif) {
        std::fprintf(stderr, "fonts not found under ./data (run `make fontdata`)\n");
        return 1;
    }

    TextStyle body;
    body.font.family = {"serif-ja", "serif"};
    body.size = 9.0f;

    TextStyle chapter;
    chapter.font.family = {"sans-ja", "serif"};
    chapter.size = 14.0f;

    TextStyle runningStyle;
    runningStyle.font.family = {"serif-ja", "serif"};
    runningStyle.size = 7.0f;
    runningStyle.fill = Color::rgb(80, 80, 80);

    ParagraphStyle bodyStyle;
    bodyStyle.lineHeight = 1.75f;
    bodyStyle.firstLineIndent = 1.0f;
    bodyStyle.spacing.hangingPunctuation = true;
    bodyStyle.lineBreak.strategy = LineBreakStrategy::KnuthPlass;

    page::PageSequence seq;
    seq.master.size = page::paper::B6;
    seq.master.writingMode = WritingMode::VerticalRl;
    seq.master.margin = page::Margins{16.0f * kMm, 16.0f * kMm, 15.0f * kMm, 12.0f * kMm};
    seq.master.columns = 2;
    seq.master.columnGap = 7.0f * kMm;
    seq.master.duplex = true;
    {
        page::RunningText h;
        h.para = inl::Paragraph::plain(u"{title}", runningStyle);
        h.para.style.align = Align::Start;
        h.offset = 6.0f * kMm;
        seq.master.header = h;
        page::RunningText f;
        f.para = inl::Paragraph::plain(u"{page}", runningStyle);
        f.para.style.align = Align::Center;
        f.offset = 7.0f * kMm;
        seq.master.footer = f;
    }

    block::Flow flow;

    auto addChapter = [&](const char16_t* title, bool first) {
        inl::Paragraph h = inl::Paragraph::plain(title, chapter);
        h.style.lineHeight = 2.0f;
        h.style.firstLineIndent = 0.0f;
        block::BlockStyle hs;
        hs.keepWithNext = true;
        hs.spanColumns = true;
        hs.spaceAfter = body.size * 2.0f;
        if (!first) hs.breakBefore = block::BreakKind::Page;
        flow.addHeading(std::move(h), 1, hs);
    };

    auto addBody = [&](std::u16string text, std::vector<inl::Annotation> anns = {}) {
        inl::Paragraph p;
        p.runs.push_back(inl::InlineRun{std::move(text), body});
        p.style = bodyStyle;
        p.annotations = std::move(anns);
        block::BlockStyle bs;
        bs.orphans = 2;
        bs.widows = 2;
        flow.addParagraph(std::move(p), bs);
    };

    addChapter(u"一　吾輩は猫である", true);
    {
        std::u16string t = u"吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。"
                           u"何でも薄暗いじめじめした所でニャーニャー泣いていた事だけは記憶している。"
                           u"吾輩はここで始めて人間というものを見た。しかもあとで聞くとそれは書生という"
                           u"人間中で一番獰悪な種族であったそうだ。";
        std::vector<inl::Annotation> a;
        size_t p = findU(t, u"吾輩");
        a.push_back(inl::Annotation::ruby(p, p + 2, u"わがはい"));
        p = findU(t, u"獰悪");
        a.push_back(inl::Annotation::ruby(p, p + 2, u"どうあく"));
        p = findU(t, u"書生");
        a.push_back(inl::Annotation::ruby(p, p + 2, u"しょ|せい", inl::RubyMode::Jukugo));
        p = findU(t, u"人間中");
        a.push_back(inl::Annotation::ruby(p, p + 3, u"にん|げん|じゅう", inl::RubyMode::Jukugo));
        addBody(t, a);
    }
    addBody(u"この書生というのは時々我々を捕えて煮て食うという話である。しかしその当時は何という考もなかったから別段恐しいとも思わなかった。"
            u"ただ彼の掌に載せられてスーと持ち上げられた時何だかフワフワした感じがあったばかりである。");
    {
        std::u16string t = u"掌の上で少し落ちついて書生の顔を見たのがいわゆる人間というものの見始であろう。"
                           u"この時妙なものだと思った感じが今でも残っている。第一毛をもって装飾されべきはずの顔がつるつるしてまるで薬缶だ。"
                           u"その後猫にもだいぶ逢ったがこんな片輪には一度も出会わした事がない。";
        std::vector<inl::Annotation> a;
        size_t p = findU(t, u"薬缶");
        a.push_back(inl::Annotation::ruby(p, p + 2, u"やかん"));
        p = findU(t, u"片輪");
        a.push_back(inl::Annotation::ruby(p, p + 2, u"かたわ"));
        addBody(t, a);
    }
    addBody(u"のみならず顔の真中があまりに突起している。そうしてその穴の中から時々ぷうぷうと煙を吹く。"
            u"どうも咽せぽくて実に弱った。これが人間の飲む煙草というものである事はようやくこの頃知った。");
    addBody(u"この書生の掌の裏でしばらくはよい心持に坐っておったが、しばらくすると非常な速力で運転し始めた。"
            u"書生が動くのか自分だけが動くのか分らないが無暗に眼が廻る。胸が悪くなる。到底助からないと思っていると、どさりと音がして眼から火が出た。"
            u"それまでは記憶しているがあとは何の事やらいくら考え出そうとしても分らない。");
    addBody(u"ふと気が付いて見ると書生はいない。たくさんおった兄弟が一疋も見えぬ。肝心の母親さえ姿を隠してしまった。"
            u"その上今までの所とは違って無暗に明るい。眼を明いていられぬくらいだ。はてな何でも容子がおかしいと、のそのそ這い出して見ると非常に痛い。"
            u"吾輩は藁の上から急に笹原の中へ棄てられたのである。");

    addChapter(u"二　書生の掌", false);
    addBody(u"ようやくの思いで笹原を這い出すと向うに大きな池がある。吾輩は池の前に坐ってどうしたらよかろうと考えて見た。"
            u"別にこれという分別も出ない。しばらくして泣いたら書生がまた迎に来てくれるかと考え付いた。ニャー、ニャーと試みにやって見たが誰も来ない。"
            u"そのうち池の上をさらさらと風が渡って日が暮れかかる。腹が非常に減って来た。泣きたくても声が出ない。");
    addBody(u"仕方がない、何でもよいから食物のある所まであるこうと決心をしてそろりそろりと池を左りに廻り始めた。"
            u"どうも非常に苦しい。そこを我慢して無理やりに這って行くとようやくの事で何となく人間臭い所へ出た。"
            u"ここへ這入ったら、どうにかなると思って竹垣の崩れた穴から、とある邸内にもぐり込んだ。");
    addBody(u"縁は不思議なもので、もしこの竹垣が破れていなかったなら、吾輩はついに路傍に餓死したかも知れんのである。"
            u"一樹の蔭とはよく云ったものだ。この垣根の穴は今日に至るまで吾輩が隣家の三毛を訪問する時の通路になっている。");

    page::FlowLayouter layouter(fonts);
    page::FlowLayoutOptions opts;
    opts.fields[u"title"] = u"吾輩は猫である";
    opts.drawGuides = true;
    const std::vector<page::Page> pages = layouter.layout(flow, seq, opts);
    std::printf("pages: %zu\n", pages.size());

    sample::savePages(pages, "output_novel", 144.0f, 3);
    return 0;
}
