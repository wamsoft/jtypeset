"""
台本 PDF を Python から組む例（sample_script.cpp と同じ内容）。

    python python/examples/script.py

リポジトリルートで実行する（./data/ のフォントを読む）。ビルドした typeset モジュールが
sys.path に無ければ build/x64-windows/python/Release を足す。
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
for cand in ("build/x64-windows/python/Release", "build/x64-windows/python"):
    p = os.path.join(ROOT, cand)
    if os.path.isdir(p):
        sys.path.insert(0, p)

import typeset as ts  # noqa: E402


def main():
    fonts = ts.FontSet()
    ok = all([
        fonts.load_file(os.path.join(ROOT, "data/NotoSerifJP-Regular.otf"), "serif-ja"),
        fonts.load_file(os.path.join(ROOT, "data/NotoSansJP-Regular.otf"), "sans-ja"),
        fonts.load_file(os.path.join(ROOT, "data/NotoSerif-Regular.ttf"), "serif"),
        fonts.load_file(os.path.join(ROOT, "data/NotoSans-Regular.ttf"), "sans"),
    ])
    if not ok:
        raise SystemExit("fonts not found under data/ (run `make fontdata`)")

    body = ts.TextStyle(["serif-ja", "serif"], 11.0)
    name = ts.TextStyle(["sans-ja", "sans"], 11.0)
    name.font.weight = 700          # Bold が無いのでフェイクボールド
    heading = ts.TextStyle(["sans-ja", "sans"], 14.0)
    heading.font.weight = 700
    small = ts.TextStyle(["sans-ja", "sans"], 8.0, ts.Color(90, 90, 90))

    body_style = ts.ParagraphStyle()
    body_style.line_height = 1.8
    body_style.spacing.hanging_punctuation = True
    name_style = body_style.copy()
    name_style.align = ts.Align.START
    head_style = body_style.copy()
    head_style.line_height = 2.2

    seq = ts.PageSequence()
    seq.master.size = ts.paper.A5
    seq.master.writing_mode = ts.WritingMode.VERTICAL_RL
    seq.master.margin = ts.Margins(22 * ts.MM, 18 * ts.MM, 18 * ts.MM, 14 * ts.MM)
    seq.master.duplex = True
    h = ts.Paragraph("{title}　　第一稿", small)
    h.style.align = ts.Align.END
    seq.master.header = ts.RunningText(h, 7 * ts.MM)
    f = ts.Paragraph("— {page} / {pages} —", small)
    f.style.align = ts.Align.CENTER
    seq.master.footer = ts.RunningText(f, 8 * ts.MM)

    flow = ts.Flow()
    label_width = body.size * 5
    label_gap = body.size

    scene1 = [
        ("ト書き", "放課後の教室。窓から夕日が差し込んでいる。太郎が机に座り、ノートを広げている。"),
        ("太郎", "なあ、花子。この問題、どう解くんだっけ。"),
        ("花子", "また？　昨日教えたでしょう。三角形の内角の和は百八十度。それを使うの。"),
        ("太郎", "……ああ、そうか。じゃあ、この角は六十度で……。"),
        ("花子", "そう。で、こっちが四十五度だから、残りは？"),
        ("太郎", "七十五度！"),
        ("花子", "正解。ほら、ちゃんと考えれば分かるじゃない。"),
        ("ト書き", "太郎、照れたように頭を掻く。花子は窓の外を見る。"),
        ("花子", "（小さく）……来年も、こうしていられるかな。"),
        ("太郎", "え？　何か言った？"),
        ("ナレーション", "二人の影が、長く教室の床に伸びていた。チャイムが鳴る。遠くで部活の掛け声が聞こえる。"),
    ]
    scene2 = [
        ("ト書き", "翌朝。通学路。桜並木の下を花子が歩いている。後ろから太郎が走ってくる。"),
        ("太郎", "花子ーっ！　待ってくれよ！"),
        ("花子", "遅いよ。また寝坊？"),
        ("太郎", "違うって。昨日の問題、家で全部解き直したんだ。ほら、見て。"),
        ("花子", "（笑って）それは楽しみ。"),
    ]

    def add_scene(title, cues, page_break):
        hs = ts.BlockStyle()
        hs.keep_with_next = True
        hs.space_after = body.size * 0.5
        if page_break:
            hs.break_before = ts.BreakKind.PAGE
        flow.add_heading(ts.Paragraph(title, heading, head_style), 1, hs)
        for who, what in cues:
            bs = ts.BlockStyle()
            bs.orphans = 2
            bs.widows = 2
            bs.space_after = body.size * 0.3
            flow.add_labeled(ts.Paragraph(who, name, name_style),
                             ts.Paragraph(what, body, body_style),
                             label_width, label_gap, bs)

    add_scene("第一場　教室", scene1, False)
    add_scene("第一場（続き）", scene1, False)
    add_scene("第二場　通学路", scene2, True)

    # ルビ付きの段落も足してみる
    p = ts.Paragraph("吾輩は猫である。名前はまだ無い。", body, body_style)
    p.annotate(ts.Annotation.ruby(0, 2, "わがはい"))
    flow.add_paragraph(p)

    layouter = ts.FlowLayouter(fonts)
    pages = layouter.layout(flow, seq, {"title": "放課後の三角形"}, draw_guides=True)
    print(f"pages: {len(pages)}")

    ok, warnings = ts.save_pdf(pages, "output_py_script.pdf", title="放課後の三角形")
    print("pdf:", ok, warnings)
    pages[0].save_png("output_py_script_p1.png", dpi=144)
    pages[0].save_svg("output_py_script_p1.svg")

    # 低レベル API: 行ごとの行長を変えて組む（回り込み・吹き出しの元になる機構）
    lines = ts.layout_paragraph(fonts, ts.Paragraph("吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。", body),
                                ts.WritingMode.HORIZONTAL_TB, [80.0, 120.0], 160.0)
    for l in lines:
        print(" ", l)


if __name__ == "__main__":
    main()
