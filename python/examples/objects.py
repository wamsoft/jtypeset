"""
外部オブジェクトの差し込み（Python）

- Python 関数をハンドラとして登録し、SVG 文字列を返す
- 外部コマンド（samples/handlers/plot.py）を登録する
- 行内・別行立て（式番号・参照）に置く

実行: リポジトリルートで `python python/examples/objects.py`
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "build", "x64-windows", "python", "Release"))
import typeset as ts  # noqa: E402


def bar_chart(req):
    """source を数値の列として棒グラフを描く SVG を返す（ベースラインは軸の位置）"""
    values = [float(v) for v in req["source"].split(",") if v.strip()]
    w = float(req["params"].get("width", 200))
    h = float(req["params"].get("height", 80))
    if req["max_inline"] > 0:
        w = min(w, req["max_inline"])
    vmax = max(values) if values else 1.0
    gap = 4.0
    bw = (w - gap * (len(values) + 1)) / max(1, len(values))
    axis_y = h - 6
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="%gpt" height="%gpt" viewBox="0 0 %g %g">' % (w, h, w, h)]
    for i, v in enumerate(values):
        bh = (axis_y - 4) * v / vmax
        x = gap + i * (bw + gap)
        parts.append('<rect x="%g" y="%g" width="%g" height="%g" fill="#4a7ab8"/>' % (x, axis_y - bh, bw, bh))
    parts.append('<line x1="0" y1="%g" x2="%g" y2="%g" stroke="#333" stroke-width="0.8"/>' % (axis_y, w, axis_y))
    parts.append("</svg>")
    return {"svg": "".join(parts), "baseline": axis_y}


def main():
    fonts = ts.FontSet()
    if not fonts.load_file("data/NotoSerifJP-Regular.otf", "serif-ja"):
        print("fonts not found; run from the repository root", file=sys.stderr)
        return 1
    body = ts.TextStyle(["serif-ja"], 10.5)

    reg = ts.ObjectRegistry()
    reg.add("bars", bar_chart)
    reg.add_command("plot", "python samples/handlers/plot.py")

    seq = ts.PageSequence()
    seq.master.size = ts.paper.A5
    seq.master.margin = ts.Margins(40, 36, 36, 36)
    seq.master.writing_mode = ts.WritingMode.HORIZONTAL_TB

    flow = ts.Flow()
    p = ts.Paragraph("Python の関数をハンドラにした行内の棒グラフ ", body)
    p.add_object("bars", "3,5,2,6,4", body, {"width": "60", "height": "14"})
    p.add_run(" は本文のベースラインに軸が揃う。別行立てなら式番号が付く:", body)
    flow.add_paragraph(p)

    blk = ts.ObjectBlock("bars", "8,3,6,9,5,7", body, {"width": "220", "height": "90"}, numbered=True)
    blk.block.label = "eq-bars"
    blk.block.space_before = 6.0
    blk.block.space_after = 6.0
    flow.add_object(blk)

    flow.add_paragraph(ts.Paragraph("式 {ref:eq-bars} を参照。外部コマンド（plot.py）の出力:", body))
    plot = ts.ObjectBlock("plot", "sin", body, {"width": "260", "height": "110"})
    plot.block.space_before = 6.0
    flow.add_object(plot)

    layouter = ts.FlowLayouter(fonts)
    pages = layouter.layout(flow, seq, objects=reg)
    for e in reg.errors:
        print("object:", e, file=sys.stderr)
    print("pages:", len(pages), "cached:", reg.cache_size)
    pages[0].save_png("out/objects_py_p1.png", dpi=144)
    ok, warnings = ts.save_pdf(pages, "out/objects_py.pdf")
    print("pdf:", ok, warnings)
    return 0


if __name__ == "__main__":
    sys.exit(main())
