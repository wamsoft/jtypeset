/**
 * sample_microtex — MicroTeX ハンドラで LaTeX 数式を行内・別行立てに差し込む
 *
 * 出力: output_microtex_p1.png / output_microtex.pdf / output_microtex_p1.svg
 */

#include <cstdio>
#include <string>

#include "microtex_handler.hpp"
#include "sample_common.hpp"
#include "typeset/block/block.hpp"
#include "typeset/page/flow_layouter.hpp"

using namespace typeset;

int main() {
    font::FontSet fonts;
    auto serifJp = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
    auto sansJp = fonts.loadFile("data/NotoSansJP-Regular.otf", "sans-ja");
    if (!serifJp || !sansJp) {
        std::fprintf(stderr, "fonts not found (run from the repository root)\n");
        return 1;
    }
    TextStyle body;
    body.font.family = {"serif-ja"};
    body.size = 10.5f;
    TextStyle head = body;
    head.font.family = {"sans-ja"};
    head.size = 16.0f;

    obj::ObjectRegistry reg;
    handlers::MicroTexOptions mo;
    mo.textFamily = "serif-ja";
    mo.sansFamily = "sans-ja";
    reg.add("tex", handlers::makeMicroTexHandler(fonts, mo));

    page::PageSequence seq;
    seq.master.size = page::paper::A4;
    seq.master.margin = page::Margins{60, 55, 55, 55};
    seq.master.writingMode = WritingMode::HorizontalTb;
    {
        TextStyle fs = body;
        fs.size = 9.0f;
        page::RunningText f;
        f.para = inl::Paragraph::plain(u"{page}", fs);
        f.para.style.align = Align::Center;
        f.offset = 24.0f;
        seq.master.footer = f;
    }

    block::Flow flow;
    flow.addHeading(inl::Paragraph::plain(u"MicroTeX による数式", head), 1);

    inl::Paragraph p1 = inl::Paragraph::plain(
        u"行内の数式は本文のベースラインに揃う。たとえば二次方程式の解 ", body);
    p1.addObject("tex", u"x=\\frac{-b\\pm\\sqrt{b^2-4ac}}{2a}", {}, body);
    p1.runs.push_back(inl::InlineRun{u" や、オイラーの等式 ", body});
    p1.addObject("tex", u"e^{i\\pi}+1=0", {}, body);
    p1.runs.push_back(inl::InlineRun{u"、総和 ", body});
    p1.addObject("tex", u"\\sum_{k=1}^{n}k=\\frac{n(n+1)}{2}", {}, body);
    p1.runs.push_back(inl::InlineRun{u" のように書ける。数式のグリフは MicroTeX 付属のフォントを "
                                    u"GlyphRun として出すので、PDF でも文字として埋め込まれる。", body});
    flow.addParagraph(p1);

    block::BlockStyle eq;
    eq.spaceBefore = 6.0f;
    eq.spaceAfter = 6.0f;
    eq.label = "eq-gauss";
    flow.addObject("tex", u"\\int_{-\\infty}^{\\infty} e^{-x^2}\\,dx = \\sqrt{\\pi}", body, {}, true, eq);

    flow.addParagraph(inl::Paragraph::plain(
        u"別行立ての式 {ref:eq-gauss} には式番号が付く。行列や場合分け、大きな括弧も MicroTeX がそのまま組む:", body));

    block::BlockStyle eq2 = eq;
    eq2.label = "eq-matrix";
    flow.addObject("tex",
        u"\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}"
        u"\\begin{pmatrix} x \\\\ y \\end{pmatrix} = "
        u"\\begin{pmatrix} ax+by \\\\ cx+dy \\end{pmatrix}, \\qquad "
        u"f(x)=\\begin{cases} x^2 & (x\\ge 0) \\\\ -x & (x<0) \\end{cases}",
        body, {}, true, eq2);

    block::BlockStyle eq3 = eq;
    eq3.label = "eq-maxwell";
    flow.addObject("tex",
        u"\\nabla\\times\\mathbf{E} = -\\frac{\\partial\\mathbf{B}}{\\partial t}, \\qquad "
        u"\\nabla\\cdot\\mathbf{B} = 0, \\qquad "
        u"\\oint_{\\partial\\Sigma}\\mathbf{B}\\cdot d\\boldsymbol{\\ell} = \\mu_0 I",
        body, {{"color", "#204080"}}, true, eq3);

    flow.addParagraph(inl::Paragraph::plain(
        u"式 {ref:eq-matrix} と式 {ref:eq-maxwell} は params の color で色を変えている。"
        u"\\text{} の中の文字は本文のフォント（textFamily）で組まれる:", body));
    block::BlockStyle eq4 = eq;
    eq4.label.clear();
    flow.addObject("tex", u"\\text{速度} = \\frac{\\text{距離}}{\\text{時間}}, \\quad "
                          u"\\lim_{n\\to\\infty}\\left(1+\\frac{1}{n}\\right)^n = e", body, {}, false, eq4);

    flow.addParagraph(inl::Paragraph::plain(
        u"未定義のコマンドは MicroTeX 自身が赤字で示す。例外になる入力は代替テキストになり、組版は止まらない:", body));
    flow.addObject("tex", u"\\undefinedmacro{x} + 1", body);

    page::FlowLayoutOptions opts;
    opts.objects = &reg;
    page::FlowLayouter layouter(fonts);
    const std::vector<page::Page> pages = layouter.layout(flow, seq, opts);
    for (const std::string& e : reg.errors()) std::fprintf(stderr, "object: %s\n", e.c_str());
    std::printf("pages: %zu, cached objects: %zu\n", pages.size(), reg.cacheSize());
    sample::savePages(pages, "output_microtex", 120.0f);
    handlers::releaseMicroTex();
    return 0;
}
