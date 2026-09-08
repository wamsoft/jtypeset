"""
typeset-md — Markdown → PDF のコマンドライン

    typeset-md report.md                    # report.pdf
    typeset-md report.md -o out.pdf --vertical --paper A5 --font fonts/mincho.otf
    typeset-md report.md --png 120          # 各ページの PNG も出す
"""
from __future__ import annotations

import argparse
import sys

from .convert import Options, convert_file


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="typeset-md", description="Markdown を typeset で組版して PDF にする")
    ap.add_argument("input", help="Markdown ファイル（先頭に YAML front matter を書ける）")
    ap.add_argument("-o", "--output", help="出力 PDF（既定: 入力と同名 .pdf）")
    ap.add_argument("--paper", help="A4 / A5 / B5 / B6 / 文庫 / 新書 / 148x210mm")
    ap.add_argument("--landscape", action="store_true")
    ap.add_argument("--vertical", action="store_true", help="縦組み")
    ap.add_argument("--columns", type=int)
    ap.add_argument("--font", action="append", default=[], help="フォントファイル（複数可。最初が本文の第一候補）")
    ap.add_argument("--font-body", help="本文の family（FontSet のキー）")
    ap.add_argument("--font-heading", help="見出しの family")
    ap.add_argument("--size", type=float, help="本文サイズ（pt）")
    ap.add_argument("--toc", action="store_true", help="先頭に目次")
    ap.add_argument("--no-numbering", action="store_true", help="見出しを採番しない")
    ap.add_argument("--no-indent", action="store_true", help="段落の一字下げをしない")
    ap.add_argument("--math", help="数式ハンドラ: mathtext（matplotlib）/ command:<コマンド>")
    ap.add_argument("--png", type=float, default=0.0, metavar="DPI", help="各ページを PNG にも出す")
    ap.add_argument("--guides", action="store_true", help="版面の枠を描く（デバッグ）")
    args = ap.parse_args(argv)

    # CLI の指定は front matter より優先する
    ov = {}
    if args.paper:
        ov["paper"] = args.paper
    if args.landscape:
        ov["landscape"] = True
    if args.vertical:
        ov["writing"] = "vertical"
    if args.columns:
        ov["columns"] = args.columns
    if args.font:
        ov["fonts"] = list(args.font)
        # 指定されたファイルをそのまま本文の候補にする（キー = パス）
        ov["font_body"] = list(args.font) + ["serif", "sans"]
        ov["font_heading"] = list(args.font) + ["sans", "serif"]
        ov["font_mono"] = list(args.font) + ["sans", "serif"]
    if args.font_body:
        ov["font_body"] = [args.font_body]
    if args.font_heading:
        ov["font_heading"] = [args.font_heading]
    if args.size:
        ov["size"] = args.size
    if args.toc:
        ov["toc"] = True
    if args.no_numbering:
        ov["numbering"] = False
    if args.no_indent:
        ov["indent"] = False
    if args.math:
        if args.math.startswith("command:"):
            ov["math"] = {"handler": "command", "command": args.math[len("command:"):]}
        else:
            ov["math"] = {"handler": args.math}
    if args.guides:
        ov["draw_guides"] = True

    try:
        pages, warnings = convert_file(args.input, args.output, Options(), png_dpi=args.png, overrides=ov)
    except Exception as e:  # noqa: BLE001
        print(f"typeset-md: {e}", file=sys.stderr)
        return 1
    for w in warnings:
        print(f"typeset-md: warning: {w}", file=sys.stderr)
    out = args.output or (args.input.rsplit(".", 1)[0] + ".pdf")
    print(f"{out}: {pages} pages")
    return 0


if __name__ == "__main__":
    sys.exit(main())
