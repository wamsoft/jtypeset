/**
 * sample_objects — 外部オブジェクトの差し込み（関数ハンドラ・外部コマンド・SVG）
 *
 * 1. 関数ハンドラ "frac": 本体のフォントで分数を組んで GlyphRun で返す（PDF でも字として埋め込まれる）
 * 2. 外部コマンド "plot": python samples/handlers/plot.py が SVG を返す（依存なし）
 * 3. 関数ハンドラ "svg": ソース文字列を SVG として読み込む（他のレンダラの出力をそのまま貼る例）
 *
 * 出力: output_objects_p1.png / output_objects.pdf / output_objects_p1.svg
 */

#include <cstdio>
#include <string>

#include "sample_common.hpp"
#include "typeset/block/block.hpp"
#include "typeset/inl/shaper.hpp"
#include "typeset/page/flow_layouter.hpp"
#include "typeset/obj/object.hpp"
#include "typeset/obj/svg_import.hpp"
#include "typeset/text/utf.hpp"

using namespace typeset;

namespace {

/// ShapedText（論理座標、横組み）を GlyphRun に。origin はベースライン左端の位置
dl::GlyphRun toRun(const inl::ShapedText& shaped, Point origin, Pt size, Color color) {
    dl::GlyphRun run;
    run.size = size;
    run.fill = color;
    for (const inl::PlacedGlyph& g : shaped.glyphs) {
        if (!run.face) run.face = g.face;
        if (g.face != run.face) continue;   // 例なので 1 face に限る
        dl::Glyph dg;
        dg.gid = g.gid;
        dg.pos = Point{origin.x + g.inline_, origin.y};
        run.glyphs.push_back(dg);
    }
    return run;
}

/// "分子/分母" を分数として組む関数ハンドラ
obj::ObjectHandler makeFracHandler(font::FontSet& fonts, const TextStyle& base) {
    return [&fonts, base](const obj::ObjectRequest& req) -> obj::ObjectResult {
        const std::u16string& src = req.source;
        const size_t slash = src.find(u'/');
        if (slash == std::u16string::npos) {
            obj::ObjectResult r;
            r.error = "frac: expected 'numerator/denominator'";
            return r;
        }
        TextStyle st = base;
        st.size = req.fontSize * (req.inlineContext ? 0.8f : 1.0f);
        const std::u16string num = src.substr(0, slash), den = src.substr(slash + 1);
        const inl::ShapedText sn = inl::shapeText(num, st, fonts, WritingMode::HorizontalTb);
        const inl::ShapedText sd = inl::shapeText(den, st, fonts, WritingMode::HorizontalTb);
        std::shared_ptr<glyphware::Face> face = fonts.primary(st.font);
        Pt asc = st.size * 0.88f, desc = st.size * 0.12f;
        if (face) font::faceAscentDescent(*face, st.size, asc, desc);

        const Pt pad = st.size * 0.15f;
        const Pt w = std::max(sn.advance, sd.advance) + pad * 2;
        const Pt ruleY = asc + desc + pad * 0.5f;           // 分子の下
        const Pt h = ruleY + pad * 0.5f + asc + desc;
        std::vector<dl::Item> items;
        items.push_back(toRun(sn, Point{(w - sn.advance) * 0.5f, asc}, st.size, Color::rgb(0, 0, 0)));
        items.push_back(toRun(sd, Point{(w - sd.advance) * 0.5f, ruleY + pad * 0.5f + asc}, st.size,
                              Color::rgb(0, 0, 0)));
        dl::RectItem rule;
        rule.rect = Rect{0, ruleY - 0.3f, w, 0.6f};
        rule.fill = Color::rgb(0, 0, 0);
        items.push_back(rule);
        // ベースライン: 罫の位置が本文の x-height の中ほどに来るよう、罫から 0.3em 下
        obj::ObjectResult r = obj::makeResult(Size{w, h}, ruleY + st.size * 0.3f, std::move(items));
        if (face) r.fonts.push_back(face);
        return r;
    };
}

} // namespace

int main() {
    font::FontSet fonts;
    auto serif = fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");
    auto sans = fonts.loadFile("data/NotoSansJP-Regular.otf", "sans-ja");
    if (!serif || !sans) {
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
    reg.add("frac", makeFracHandler(fonts, body));
    reg.addCommand("plot", "python samples/handlers/plot.py");
    reg.add("svg", [](const obj::ObjectRequest& req) {
        obj::ObjectResult r;
        obj::SvgImportOptions so;
        so.fontSize = req.fontSize;
        obj::importSvg(text::utf16ToUtf8(req.source), so, r);
        return r;
    });

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
    flow.addHeading(inl::Paragraph::plain(u"外部オブジェクトの差し込み", head), 1);

    inl::Paragraph intro = inl::Paragraph::plain(
        u"本文には呼び出し先のハンドラ名とソース文字列だけを書く。組版時にハンドラが箱の大きさと描画命令を返し、"
        u"行内なら本文のベースラインに揃えて、別行立てなら行方向に揃えて置く。行内の分数 ", body);
    intro.addObject("frac", u"a+b/2c", {}, body);
    intro.runs.push_back(inl::InlineRun{u" はフォントのグリフで組まれているので、PDF でも文字として埋め込まれる。"
                                        u"続けて ", body});
    intro.addObject("frac", u"1/x", {}, body);
    intro.runs.push_back(inl::InlineRun{u" のように何度でも置ける（同じ式はキャッシュされる）。", body});
    flow.addParagraph(intro);

    block::BlockStyle eqStyle;
    eqStyle.spaceBefore = 6.0f;
    eqStyle.spaceAfter = 6.0f;
    eqStyle.label = "eq-frac";
    flow.addObject("frac", u"x²+y²/2xy", body, {}, true, eqStyle);

    flow.addParagraph(inl::Paragraph::plain(
        u"別行立ての式 {ref:eq-frac} には式番号が付き、本文から参照できる。次は外部コマンドの例で、"
        u"python スクリプトが要求（JSON）を受けて SVG を標準出力に書き、本体がそれを読み込む。"
        u"matplotlib などを使うなら、そのスクリプトが SVG を書くだけでよい。", body));

    block::ObjectBlock plot;
    plot.handler = "plot";
    plot.source = u"sin,cos";
    plot.params = {{"width", "300"}, {"height", "140"}};
    plot.textStyle = body;
    plot.caption = inl::Paragraph::plain(u"外部コマンド plot.py の出力（sin, cos）。式番号 {eq} は本文から {ref:eq-plot} で参照できる", body);
    plot.caption->style.align = Align::Center;
    plot.numbered = true;
    plot.block.spaceBefore = 6.0f;
    plot.block.spaceAfter = 8.0f;
    plot.block.label = "eq-plot";
    flow.addObject(plot);

    flow.addParagraph(inl::Paragraph::plain(
        u"式 {ref:eq-plot} のように、SVG を吐けるものは何でも差し込める。以下は SVG 文字列をそのまま渡す例。"
        u"MathJax や dvisvgm、Typst の出力もこの経路で入る（ベースラインは "
        u"<!-- typeset baseline=\"…\" --> のコメントで伝える）。", body));

    const std::u16string svgSrc =
        u"<svg xmlns='http://www.w3.org/2000/svg' width='90pt' height='45pt' viewBox='0 0 120 60'>"
        u"<!-- typeset baseline='34' -->"
        u"<defs><path id='tri' d='M0 0 L12 0 L6 -10 Z'/></defs>"
        u"<rect x='2' y='2' width='116' height='56' rx='6' fill='#eef' stroke='#446' stroke-width='1'/>"
        u"<path d='M10 45 Q 40 5 70 45 T 110 45' fill='none' stroke='#c33' stroke-width='2'/>"
        u"<use href='#tri' x='30' y='44' fill='#393'/><use href='#tri' x='80' y='44' fill='#393'/>"
        u"<circle cx='60' cy='30' r='4' fill='#36c'/></svg>";
    inl::Paragraph inlineSvg = inl::Paragraph::plain(u"行内に SVG: ", body);
    inlineSvg.addObject("svg", svgSrc, {}, body);
    inlineSvg.runs.push_back(inl::InlineRun{u" ← ベースライン（下端から 11pt）が本文に揃い、行より高いぶん行送りが広がる。", body});
    flow.addParagraph(inlineSvg);

    flow.addParagraph(inl::Paragraph::plain(
        u"未登録のハンドラや失敗したハンドラは代替テキストになり、組版は止まらない: ", body));
    flow.addObject("tex", u"\\int_0^1 f(x)\\,dx", body);

    page::FlowLayoutOptions opts;
    opts.objects = &reg;
    page::FlowLayouter layouter(fonts);
    const std::vector<page::Page> pages = layouter.layout(flow, seq, opts);
    for (const std::string& e : reg.errors()) std::fprintf(stderr, "object: %s\n", e.c_str());
    std::printf("pages: %zu, cached objects: %zu\n", pages.size(), reg.cacheSize());
    sample::savePages(pages, "output_objects", 120.0f);
    return 0;
}
