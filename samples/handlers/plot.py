"""
外部コマンド型オブジェクトハンドラの例（依存なし・純 Python）

typeset の ObjectRegistry.addCommand("plot", "python samples/handlers/plot.py") で登録すると、
本文の ObjectBlock / 行内オブジェクトごとに `python plot.py <request.json>` として呼ばれる。
request.json: {"handler","source","params","maxInline","maxBlock","fontSize","inline","vertical"}
標準出力に SVG を書く。ベースラインは <!-- typeset baseline="..." --> で伝える（省略可）。

source は "sin" "cos" "sin,cos" のような関数名の列。params: width / height（pt）。
"""
import json
import math
import sys


def main():
    req = json.load(open(sys.argv[1], encoding="utf-8"))
    funcs = [f.strip() for f in req["source"].split(",") if f.strip()] or ["sin"]
    params = req.get("params", {})
    w = float(params.get("width", 240))
    h = float(params.get("height", 120))
    if req.get("maxInline", 0) > 0:
        w = min(w, req["maxInline"])
    fs = float(req.get("fontSize", 10))

    # 余白と目盛り
    ml, mr, mt, mb = 24.0, 8.0, 8.0, 18.0
    pw, ph = w - ml - mr, h - mt - mb
    colors = ["#1f77b4", "#d62728", "#2ca02c", "#9467bd"]
    out = []
    out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%gpt" height="%gpt" viewBox="0 0 %g %g">' % (w, h, w, h))
    out.append('<!-- typeset baseline="%g" -->' % (h - mb))
    out.append('<rect x="%g" y="%g" width="%g" height="%g" fill="#fafafa" stroke="#888" stroke-width="0.6"/>' % (ml, mt, pw, ph))
    # 格子
    for i in range(1, 4):
        y = mt + ph * i / 4
        out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#ddd" stroke-width="0.5"/>' % (ml, y, ml + pw, y))
    for i in range(1, 4):
        x = ml + pw * i / 4
        out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#ddd" stroke-width="0.5"/>' % (x, mt, x, mt + ph))
    # 軸（y=0）
    y0 = mt + ph / 2
    out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#444" stroke-width="0.8"/>' % (ml, y0, ml + pw, y0))
    # 目盛りの代わりの短い線と丸（文字は使わない: SVG の text は読み込まれない）
    for k, xx in enumerate((0, 0.5, 1.0)):
        x = ml + pw * xx
        out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#444" stroke-width="0.8"/>' % (x, mt + ph, x, mt + ph + 3))
    out.append('<circle cx="%g" cy="%g" r="1.5" fill="#444"/>' % (ml - 4, y0))
    # 曲線
    n = 120
    for fi, name in enumerate(funcs):
        fn = getattr(math, name, None)
        if fn is None:
            continue
        pts = []
        for i in range(n + 1):
            t = i / n
            x = ml + pw * t
            v = fn(t * 4 * math.pi)
            y = y0 - v * (ph / 2) * 0.9
            pts.append("%.2f,%.2f" % (x, y))
        out.append('<polyline points="%s" fill="none" stroke="%s" stroke-width="1.2" stroke-linejoin="round"/>'
                   % (" ".join(pts), colors[fi % len(colors)]))
    out.append("</svg>")
    sys.stdout.write("\n".join(out))


if __name__ == "__main__":
    main()
