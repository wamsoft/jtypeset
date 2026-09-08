"""
typeset.md — Markdown → typeset の Flow → PDF

    from typeset.md import convert_file
    convert_file("report.md", "report.pdf")

    python -m typeset.md report.md -o report.pdf [--vertical] [--paper A5] [--font path.otf ...]

先頭の YAML front matter で題名・判型・書字方向・フォント・目次などを指定する（Options 参照）。
対応する Markdown: 見出し（採番・しおり・`{#label}`）、段落（一字下げ）、箇条書き／番号付き、コードブロック、
表（GFM。直後の「表: 〜」がキャプション）、画像（`![キャプション](path){#fig}` は図番号付きの図、文中なら行内画像）、
脚注（`[^1]`）、引用、水平線、強調・斜体・行内コード、リンク（URL は脚注へ）、数式（`$…$` / `$$…$$`。
ハンドラは front matter の math で指定）、ルビ（`｜漢字《かんじ》` / `漢字《かんじ》` / `{漢字|かんじ}`）、
`<!-- pagebreak -->` / `<!-- columnbreak -->` / `<!-- columns: 2 -->` / `[toc]` / `[index]`（`{index:よみ|用語}` を集める）、
本文中の `{ref:label}` `{page:label}`。
"""
from .convert import Options, convert_file, convert_text, build_flow  # noqa: F401

__all__ = ["Options", "convert_file", "convert_text", "build_flow"]
