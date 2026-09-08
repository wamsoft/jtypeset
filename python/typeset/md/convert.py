"""
Markdown（markdown-it-py のトークン列）→ typeset の Flow

方針: Markdown の要素と typeset のブロックはほぼ 1:1 なので、トークンを順に読んで Flow に足していく。
見出し・図・表・数式のラベルは `{#label}` をテキスト末尾（図はキャプション末尾）に書き、本文から
`{ref:label}` `{page:label}` で参照する（typeset の置換にそのまま渡す）。
"""
from __future__ import annotations

import os
import re
import sys
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Sequence, Tuple

from .. import _typeset as ts  # 拡張モジュール

try:
    from markdown_it import MarkdownIt
    from markdown_it.token import Token
    from mdit_py_plugins.dollarmath import dollarmath_plugin
    from mdit_py_plugins.footnote import footnote_plugin
    from mdit_py_plugins.front_matter import front_matter_plugin
except ImportError as e:  # pragma: no cover
    raise ImportError("typeset.md には markdown-it-py と mdit-py-plugins が要ります: pip install typeset[md]") from e


# ------------------------------------------------------------------------------
# 設定
# ------------------------------------------------------------------------------

PAPERS = {
    "A4": "A4", "A5": "A5", "B5": "B5", "B6": "B6",
    "BUNKO": "BUNKO", "文庫": "BUNKO", "SHINSHO": "SHINSHO", "新書": "SHINSHO",
}

# フォントが指定されないときに探す場所（見つかった最初のものを使う）
DEFAULT_FONT_CANDIDATES = {
    "serif": [
        "data/NotoSerifJP-Regular.otf",
        "C:/Windows/Fonts/yumin.ttf", "C:/Windows/Fonts/msmincho.ttc",
        "/System/Library/Fonts/ヒラギノ明朝 ProN.ttc",
        "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSerifCJK-Regular.ttc",
    ],
    "sans": [
        "data/NotoSansJP-Regular.otf",
        "C:/Windows/Fonts/YuGothM.ttc", "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/msgothic.ttc",
        "/System/Library/Fonts/ヒラギノ角ゴシック W4.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    ],
}


@dataclass
class Options:
    """front matter / CLI で指定できる設定（キーは front matter では kebab-case でもよい）"""
    title: str = ""
    author: str = ""
    date: str = ""
    paper: str = "A4"                 # A4 / A5 / B5 / B6 / 文庫 / 新書 / "148x210mm"
    landscape: bool = False
    writing: str = "horizontal"       # horizontal / vertical
    columns: int = 1
    column_gap: float = 0.0           # pt（0 = 既定）
    margin: Any = None                # mm。数値、または {top, bottom, inner, outer}
    fonts: List[Any] = field(default_factory=list)   # ファイルパス、または {path, key, index}
    font_body: List[str] = field(default_factory=lambda: ["serif"])
    font_heading: List[str] = field(default_factory=lambda: ["sans"])
    font_mono: List[str] = field(default_factory=lambda: ["sans"])
    size: float = 10.5
    line_height: float = 1.75
    indent: bool = True               # 段落の一字下げ
    justify: bool = True
    toc: bool = False                 # 先頭に目次（本文の [toc] でも置ける）
    toc_depth: int = 2
    numbering: bool = True            # 見出しの採番
    heading_page_break: int = 0       # このレベル以下の見出しで改ページ（0 = しない）
    header: Optional[str] = None      # 柱（{title} {page} {pages}）。None なら題名
    footer: Optional[str] = "{page} / {pages}"
    links: str = "footnote"           # footnote / inline / none
    math: Dict[str, Any] = field(default_factory=dict)   # {"handler": "command", "command": "..."} / {"handler": "mathtext"}
    figure_format: str = "図 {n}"
    table_format: str = "表 {n}"
    equation_format: str = "({n})"
    footnote_marker_format: str = "{n}"
    footnote_label_format: str = "{n} "
    title_page: bool = False          # 表題を独立したページに
    balance_last_page: bool = True
    draw_guides: bool = False

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "Options":
        o = cls()
        o.update(d)
        return o

    def update(self, d: Dict[str, Any]) -> None:
        for k, v in d.items():
            key = k.replace("-", "_")
            if not hasattr(self, key):
                print(f"typeset.md: unknown option '{k}' (ignored)", file=sys.stderr)
                continue
            if key in ("fonts", "font_body", "font_heading", "font_mono") and isinstance(v, str):
                v = [v]
            setattr(self, key, v)


# ------------------------------------------------------------------------------
# 補助
# ------------------------------------------------------------------------------

def utf16len(s: str) -> int:
    return len(s.encode("utf-16-le")) // 2


LABEL_RE = re.compile(r"\s*\{#([A-Za-z0-9_:\-.]+)\}\s*$")
# ルビ: ｜漢字《かんじ》 / |漢字《かんじ》 / 漢字《かんじ》（直前の漢字連続） / {漢字|かんじ}
RUBY_RE = re.compile(
    r"[｜|]([^《》｜|]+?)《([^》]+)》"
    r"|([\u4E00-\u9FFF\u3400-\u4DBF々〆ヶ〇]+)《([^》]+)》"
    r"|\{(?!index:|ref:|page:)([^{}|]+)\|([^{}]+)\}"
)


def split_label(text: str) -> Tuple[str, str]:
    """末尾の {#label} を切り出す"""
    m = LABEL_RE.search(text)
    if not m:
        return text, ""
    return text[:m.start()].rstrip(), m.group(1)


def parse_paper(spec: str, landscape: bool) -> Any:
    key = PAPERS.get(spec.strip().upper() if spec.strip().upper() in PAPERS else spec.strip())
    if key:
        size = getattr(ts.paper, key)
    else:
        m = re.match(r"^\s*([\d.]+)\s*[x×]\s*([\d.]+)\s*(mm|pt|in)?\s*$", spec)
        if not m:
            raise ValueError(f"unknown paper: {spec}")
        unit = {"mm": ts.MM, "pt": 1.0, "in": ts.INCH, None: ts.MM}[m.group(3)]
        size = ts.Size(float(m.group(1)) * unit, float(m.group(2)) * unit)
    return ts.paper.landscape(size) if landscape else size


def parse_margins(spec: Any, default_mm: float) -> Any:
    if spec is None:
        return ts.Margins(default_mm * ts.MM, default_mm * ts.MM, default_mm * ts.MM, default_mm * ts.MM)
    if isinstance(spec, (int, float)):
        v = float(spec) * ts.MM
        return ts.Margins(v, v, v, v)
    g = lambda k: float(spec.get(k, default_mm)) * ts.MM  # noqa: E731
    return ts.Margins(g("top"), g("bottom"), g("inner"), g("outer"))


def make_markdown() -> MarkdownIt:
    md = MarkdownIt("commonmark", {"typographer": False, "html": True})
    md.enable(["table", "strikethrough"])
    md.use(footnote_plugin)
    md.use(front_matter_plugin)
    md.use(dollarmath_plugin, allow_labels=True, double_inline=True)
    return md


# ------------------------------------------------------------------------------
# 変換本体
# ------------------------------------------------------------------------------

class Converter:
    def __init__(self, opts: Options, base_dir: str = ".", overrides: Optional[Dict[str, Any]] = None):
        self.opts = opts
        self.overrides = overrides or {}     # front matter より優先する設定（CLI）
        self.base_dir = base_dir
        self.fonts = ts.FontSet()
        self.registry: Optional[ts.ObjectRegistry] = None
        self.warnings: List[str] = []
        self.footnote_defs: Dict[int, List[Token]] = {}
        self.flow = ts.Flow()

    # ---- 準備 ----

    def _setup_fonts(self) -> None:
        loaded: Dict[str, bool] = {}
        for f in self.opts.fonts:
            if isinstance(f, str):
                path, key, index = f, "", 0
            else:
                path, key, index = f.get("path", ""), f.get("key", ""), int(f.get("index", 0))
            full = path if os.path.isabs(path) else os.path.join(self.base_dir, path)
            if not os.path.exists(full):
                full = path
            if not self.fonts.load_file(full, key, index):
                self.warnings.append(f"font not loaded: {path}")
            else:
                loaded[key or path] = True
        # 既定のキー serif / sans が無ければ探す
        for key in ("serif", "sans"):
            if key in loaded:
                continue
            for cand in DEFAULT_FONT_CANDIDATES[key]:
                full = cand if os.path.isabs(cand) else os.path.join(self.base_dir, cand)
                if not os.path.exists(full):
                    full = cand if os.path.exists(cand) else ""
                if full and self.fonts.load_file(full, key):
                    loaded[key] = True
                    break
        if self.fonts.size == 0:
            raise RuntimeError("no font could be loaded: specify fonts in the front matter or --font")
        if "serif" not in loaded and "sans" in loaded:
            pass  # family 解決は FontSet 側でフォールバックする

    def _setup_styles(self) -> None:
        o = self.opts
        self.body = ts.TextStyle(o.font_body, o.size)
        self.mono = ts.TextStyle(o.font_mono, o.size * 0.9)
        self.small = ts.TextStyle(o.font_body, o.size * 0.8)
        self.head_styles = {}
        for level, scale in ((1, 1.7), (2, 1.35), (3, 1.15), (4, 1.05), (5, 1.0), (6, 1.0)):
            st = ts.TextStyle(o.font_heading, o.size * scale)
            st.font.weight = 600
            self.head_styles[level] = st
        self.pstyle = ts.ParagraphStyle()
        self.pstyle.line_height = o.line_height
        self.pstyle.first_line_indent = 1.0 if o.indent else 0.0   # em
        if not o.justify:
            self.pstyle.align = ts.Align.START
        self.plain_pstyle = ts.ParagraphStyle()
        self.plain_pstyle.line_height = o.line_height
        self.plain_pstyle.first_line_indent = 0.0
        self.head_pstyle = ts.ParagraphStyle()
        self.head_pstyle.align = ts.Align.START
        self.head_pstyle.line_height = 1.4
        self.head_pstyle.first_line_indent = 0.0

    def _setup_math(self) -> None:
        m = self.opts.math or {}
        handler = str(m.get("handler", "none")).lower()
        if handler in ("none", ""):
            return
        self.registry = ts.ObjectRegistry()
        if handler == "command":
            cmd = m.get("command")
            if not cmd:
                self.warnings.append("math.handler=command needs math.command")
                return
            self.registry.add_command("tex", str(cmd), str(m.get("workdir", "")))
        elif handler == "mathtext":
            self.registry.add("tex", mathtext_handler)
        else:
            self.warnings.append(f"unknown math handler: {handler}")

    # ---- 文書 ----

    def convert(self, text: str) -> ts.Flow:
        md = make_markdown()
        tokens = md.parse(text)
        # front matter
        for t in tokens:
            if t.type == "front_matter":
                try:
                    import yaml  # type: ignore
                    data = yaml.safe_load(t.content) or {}
                except ImportError:
                    data = simple_yaml(t.content)
                if isinstance(data, dict):
                    self.opts.update(data)
                break
        self.opts.update(self.overrides)
        self._setup_fonts()
        self._setup_styles()
        self._setup_math()
        self._collect_footnotes(tokens)

        self._title_block()
        if self.opts.toc:
            self._add_toc()
        self._blocks(tokens, 0, len(tokens))
        return self.flow

    def _title_block(self) -> None:
        o = self.opts
        if not o.title:
            return
        st = ts.TextStyle(o.font_heading, o.size * 2.2)
        st.font.weight = 600
        center = ts.ParagraphStyle()
        center.align = ts.Align.CENTER
        center.first_line_indent = 0.0
        center.line_height = 1.4
        bs = ts.BlockStyle()
        bs.space_before = o.size * 3
        bs.space_after = o.size * 1.2
        self.flow.add_paragraph(ts.Paragraph(o.title, st, center), bs)
        sub = " ".join(x for x in (o.author, o.date) if x)
        if sub:
            bs2 = ts.BlockStyle()
            bs2.space_after = o.size * 2
            self.flow.add_paragraph(ts.Paragraph(sub, ts.TextStyle(o.font_body, o.size), center), bs2)
        if o.title_page:
            self.flow.add_page_break()

    def _add_toc(self) -> None:
        toc = ts.TocBlock()
        toc.style = ts.TextStyle(self.opts.font_body, self.opts.size)
        toc.max_level = self.opts.toc_depth
        toc.block.space_after = self.opts.size
        head = ts.Paragraph("目次", self.head_styles[2], self.head_pstyle)
        bs = ts.BlockStyle()
        bs.keep_with_next = True
        bs.space_before = self.opts.size
        bs.space_after = self.opts.size * 0.5
        self.flow.add_paragraph(head, bs)
        self.flow.add_toc(toc)

    def _add_index(self) -> None:
        idx = ts.IndexBlock(ts.TextStyle(self.opts.font_body, self.opts.size * 0.9))
        idx.block.space_after = self.opts.size
        self.flow.add_index(idx)

    def _collect_footnotes(self, tokens: List[Token]) -> None:
        i = 0
        while i < len(tokens):
            t = tokens[i]
            if t.type == "footnote_open":
                fid = int(t.meta["id"])
                j = i + 1
                body: List[Token] = []
                while j < len(tokens) and tokens[j].type != "footnote_close":
                    body.append(tokens[j])
                    j += 1
                self.footnote_defs[fid] = body
                i = j
            i += 1

    # ---- ブロック ----

    def _blocks(self, tokens: List[Token], start: int, end: int) -> None:
        i = start
        while i < end:
            t = tokens[i]
            typ = t.type
            if typ in ("front_matter", "footnote_block_open"):
                if typ == "footnote_block_open":
                    break   # 脚注定義は本文の末尾。ここで終わり
                i += 1
                continue
            if typ == "heading_open":
                i = self._heading(tokens, i)
            elif typ == "paragraph_open":
                i = self._paragraph_block(tokens, i)
            elif typ in ("bullet_list_open", "ordered_list_open"):
                i = self._list(tokens, i, 0)
            elif typ in ("fence", "code_block"):
                self._code(t.content)
                i += 1
            elif typ == "table_open":
                i = self._table(tokens, i)
            elif typ == "blockquote_open":
                i = self._blockquote(tokens, i)
            elif typ == "hr":
                bs = ts.BlockStyle()
                bs.space_before = self.opts.size * 0.8
                bs.space_after = self.opts.size * 0.8
                self.flow.add_rule(0.5, ts.Color(0, 0, 0, 255), bs)
                i += 1
            elif typ == "html_block":
                self._html(t.content)
                i += 1
            elif typ in ("math_block", "math_block_label", "math_block_eqno"):
                self._math_block(t.content, t.info if typ != "math_block" else "")
                i += 1
            else:
                i += 1

    def _close_index(self, tokens: List[Token], i: int, open_type: str) -> int:
        """tokens[i] が open のとき、対応する close の位置"""
        close_type = open_type.replace("_open", "_close")
        depth = 0
        for j in range(i, len(tokens)):
            if tokens[j].type == open_type:
                depth += 1
            elif tokens[j].type == close_type:
                depth -= 1
                if depth == 0:
                    return j
        return len(tokens) - 1

    def _heading(self, tokens: List[Token], i: int) -> int:
        level = int(tokens[i].tag[1])
        inline = tokens[i + 1]
        text = inline.content
        _, label = split_label(text)
        if label and inline.children:
            # 末尾の {#label} をテキストから取る
            last = inline.children[-1]
            if last.type == "text":
                last.content = LABEL_RE.sub("", last.content).rstrip()
        p = self._inline(inline.children or [], self.head_styles[min(level, 6)], self.head_pstyle)
        bs = ts.BlockStyle()
        bs.keep_with_next = True
        bs.space_before = self.opts.size * (1.6 if level == 1 else 1.0 if level == 2 else 0.7)
        bs.space_after = self.opts.size * (0.6 if level == 1 else 0.4)
        bs.label = label
        if self.opts.heading_page_break and level <= self.opts.heading_page_break and self.flow.size > 0:
            bs.break_before = ts.BreakKind.PAGE
        self.flow.add_heading(p, level, bs, self.opts.numbering and level <= 3)
        return i + 3

    def _paragraph_block(self, tokens: List[Token], i: int) -> int:
        inline = tokens[i + 1]
        children = inline.children or []
        # [toc] / [index]
        if inline.content.strip().lower() == "[toc]":
            self._add_toc()
            return i + 3
        if inline.content.strip().lower() == "[index]":
            self._add_index()
            return i + 3
        # 画像だけの段落 → 図
        non_ws = [c for c in children if not (c.type == "text" and not c.content.strip()) and c.type != "softbreak"]
        if len(non_ws) == 1 and non_ws[0].type == "image":
            self._figure(non_ws[0])
            return i + 3
        # 表の直後の「表: 〜」はキャプションとして表に付ける（_table が先読みする）
        p = self._inline(children, self.body, self.pstyle)
        bs = ts.BlockStyle()
        bs.orphans = 2
        bs.widows = 2
        bs.space_after = self.opts.size * 0.3
        self.flow.add_paragraph(p, bs)
        return i + 3

    def _list(self, tokens: List[Token], i: int, depth: int) -> int:
        ordered = tokens[i].type == "ordered_list_open"
        end = self._close_index(tokens, i, tokens[i].type)
        lb = ts.ListBlock()
        lb.marker = ts.ListMarker.NUMBERED if ordered else ts.ListMarker.BULLET
        if depth > 0:
            # 入れ子は記号を字下げする（ラベル欄は行頭から始まるので、全角空白で押す）
            lb.bullet = "　" * depth + ("◦" if depth == 1 else "‣")
            lb.label_width = self.opts.size * (1.5 + 1.0 * depth)
        lb.item_gap = self.opts.size * 0.15
        lb.block.space_before = self.opts.size * (0.3 if depth == 0 else 0.1)
        lb.block.space_after = self.opts.size * (0.5 if depth == 0 else 0.1)
        nested: List[Tuple[int, int]] = []   # (項目番号の後に置く, トークン位置)
        items: List[Any] = []                 # pybind の vector 属性はコピーを返すので、集めてから代入する
        j = i + 1
        item_pstyle = ts.ParagraphStyle()
        item_pstyle.line_height = self.opts.line_height
        item_pstyle.first_line_indent = 0.0
        if not self.opts.justify:
            item_pstyle.align = ts.Align.START
        while j < end:
            t = tokens[j]
            if t.type == "list_item_open":
                item_end = self._close_index(tokens, j, "list_item_open")
                runs: List[Token] = []
                k = j + 1
                sub_lists: List[int] = []
                while k < item_end:
                    tk = tokens[k]
                    if tk.type == "paragraph_open":
                        runs.extend(tokens[k + 1].children or [])
                        if k + 3 < item_end and tokens[k + 3].type == "paragraph_open":
                            runs.append(Token("hardbreak", "br", 0))
                        k += 3
                    elif tk.type in ("bullet_list_open", "ordered_list_open"):
                        sub_lists.append(k)
                        k = self._close_index(tokens, k, tk.type) + 1
                    else:
                        k += 1
                items.append(self._inline(runs, self.body, item_pstyle))
                for k in sub_lists:
                    nested.append((len(items), k))
                j = item_end + 1
            else:
                j += 1
        lb.items = items
        # 入れ子の箇条書きは、その項目までを 1 ブロック、入れ子、残りを次のブロックと分けて置く
        if not nested:
            self.flow.add_list(lb)
        else:
            cursor = 0
            for after_item, k in nested:
                part = self._slice_list(lb, cursor, after_item)
                if len(part.items):
                    self.flow.add_list(part)
                self._list(tokens, k, depth + 1)
                cursor = after_item
            rest = self._slice_list(lb, cursor, len(items))
            if len(rest.items):
                self.flow.add_list(rest)
        return end + 1

    @staticmethod
    def _slice_list(lb: ts.ListBlock, a: int, b: int) -> ts.ListBlock:
        part = ts.ListBlock()
        part.marker = lb.marker
        part.bullet = lb.bullet
        part.number_suffix = lb.number_suffix
        part.label_width = lb.label_width
        part.gap = lb.gap
        part.item_gap = lb.item_gap
        part.block = lb.block
        part.items = list(lb.items)[a:b]
        return part

    def _code(self, content: str) -> None:
        text = content.rstrip("\n")
        ps = ts.ParagraphStyle()
        ps.preserve_spaces = True
        ps.align = ts.Align.START
        ps.line_height = 1.45
        ps.first_line_indent = 0.0
        p = ts.Paragraph()
        p.style = ps
        p.add_run(text, self.mono, literal=True)   # コードブロックは {…} を置換しない
        bs = ts.BlockStyle()
        bs.background = ts.Color(242, 242, 242, 255)
        bs.padding = self.opts.size * 0.6
        bs.keep_together = True
        bs.space_before = self.opts.size * 0.4
        bs.space_after = self.opts.size * 0.8
        self.flow.add_paragraph(p, bs)

    def _table(self, tokens: List[Token], i: int) -> int:
        end = self._close_index(tokens, i, "table_open")
        tb = ts.TableBlock()
        cell_pstyle = ts.ParagraphStyle()
        cell_pstyle.align = ts.Align.START
        cell_pstyle.first_line_indent = 0.0
        cell_pstyle.line_height = 1.4
        cell_style = ts.TextStyle(self.opts.font_body, self.opts.size * 0.9)
        aligns: List[Any] = []
        rows: List[Any] = []
        j = i + 1
        in_head = False
        while j < end:
            t = tokens[j]
            if t.type == "thead_open":
                in_head = True
            elif t.type == "thead_close":
                in_head = False
            elif t.type == "tr_open":
                row = ts.TableRow()
                row.header = in_head
                tr_end = self._close_index(tokens, j, "tr_open")
                cells: List[Any] = []
                k = j + 1
                col = 0
                while k < tr_end:
                    tk = tokens[k]
                    if tk.type in ("th_open", "td_open"):
                        style_attr = tk.attrGet("style") or ""
                        al = ts.Align.START
                        if "center" in style_attr:
                            al = ts.Align.CENTER
                        elif "right" in style_attr:
                            al = ts.Align.END
                        if in_head:
                            aligns.append(al)
                        cell = ts.TableCell()
                        ps = cell_pstyle.copy()
                        ps.align = al
                        cell.paras = [self._inline(tokens[k + 1].children or [], cell_style, ps)]
                        cells.append(cell)
                        col += 1
                        k += 3
                    else:
                        k += 1
                row.cells = cells
                rows.append(row)
                j = tr_end
            j += 1
        tb.rows = rows
        ncol = max((len(r.cells) for r in rows), default=0)
        columns: List[Any] = []
        for c in range(ncol):
            tc = ts.TableColumn()
            tc.align = aligns[c] if c < len(aligns) else ts.Align.START
            columns.append(tc)
        tb.columns = columns
        tb.block.space_before = self.opts.size * 0.6
        tb.block.space_after = self.opts.size * 0.8
        tb.align = ts.Align.CENTER
        tb.full_width = len(columns) >= 3   # 列が多ければ版面いっぱいに
        # 直後の段落が「表: 〜」ならキャプション
        nxt = end + 1
        if nxt + 1 < len(tokens) and tokens[nxt].type == "paragraph_open":
            content = tokens[nxt + 1].content.strip()
            m = re.match(r"^(表|Table)\s*[:：]\s*(.*)$", content, re.S)
            if m:
                text, label = split_label(m.group(2))
                cap = ts.ParagraphStyle()
                cap.align = ts.Align.CENTER
                cap.first_line_indent = 0.0
                tb.caption = ts.Paragraph("{table}　" + text, self.small, cap)
                tb.block.label = label
                end = nxt + 2
        self.flow.add_table(tb)
        return end + 1

    def _blockquote(self, tokens: List[Token], i: int) -> int:
        end = self._close_index(tokens, i, "blockquote_open")
        j = i + 1
        while j < end:
            t = tokens[j]
            if t.type == "paragraph_open":
                ps = ts.ParagraphStyle()
                ps.line_height = self.opts.line_height
                ps.first_line_indent = 0.0
                if not self.opts.justify:
                    ps.align = ts.Align.START
                p = self._inline(tokens[j + 1].children or [], self.body, ps)
                bs = ts.BlockStyle()
                bs.background = ts.Color(246, 246, 246, 255)
                bs.padding = self.opts.size * 0.6
                bs.space_after = self.opts.size * 0.4
                self.flow.add_paragraph(p, bs)
                j += 3
            else:
                j += 1
        return end + 1

    def _html(self, content: str) -> None:
        c = content.strip().lower()
        if "pagebreak" in c or "newpage" in c:
            self.flow.add_page_break()
        elif "columnbreak" in c:
            self.flow.add_column_break()
        elif c.startswith("<!--") and "columns" in c:
            m = re.search(r"columns\s*[:=]\s*(\d+)", c)
            if m:
                self.flow.add_section(int(m.group(1)))
        elif "toc" in c and c.startswith("<!--"):
            self._add_toc()

    def _math_block(self, content: str, label: str) -> None:
        ob = ts.ObjectBlock("tex", content.strip(), self.body, {}, numbered=bool(label))
        ob.block.space_before = self.opts.size * 0.5
        ob.block.space_after = self.opts.size * 0.5
        ob.block.label = label
        self.flow.add_object(ob)

    def _figure(self, img: Token) -> None:
        src = img.attrGet("src") or ""
        alt = img.content or ""
        title = img.attrGet("title") or ""
        caption_text, label = split_label(title or alt)
        if not label:
            alt2, label = split_label(alt)
            if not title:
                caption_text = alt2
        image = self._load_image(src)
        if image is None:
            p = ts.Paragraph(f"[image not found: {src}]", self.small, self.plain_pstyle)
            self.flow.add_paragraph(p)
            return
        blk = ts.ImageBlock()
        blk.image = image
        # 幅は版面の 70% を上限（大きければ段に合わせて縮む）
        blk.size = ts.Size(0.0, 0.0)
        blk.placement = ts.ImagePlacement.BLOCK
        blk.align = ts.Align.CENTER
        if caption_text:
            cap = ts.ParagraphStyle()
            cap.align = ts.Align.CENTER
            cap.first_line_indent = 0.0
            blk.caption = ts.Paragraph("{fig}　" + caption_text, self.small, cap)
        blk.block.space_before = self.opts.size * 0.6
        blk.block.space_after = self.opts.size * 0.8
        blk.block.label = label
        self.flow.add_image(blk)

    def _load_image(self, src: str) -> Any:
        path = src if os.path.isabs(src) else os.path.join(self.base_dir, src)
        if not os.path.exists(path):
            path = src
        if not os.path.exists(path):
            self.warnings.append(f"image not found: {src}")
            return None
        try:
            return ts.load_image(path)
        except Exception as e:  # noqa: BLE001
            self.warnings.append(f"image load failed: {src}: {e}")
            return None

    # ---- インライン ----

    def _inline(self, children: Sequence[Token], base: ts.TextStyle, pstyle: ts.ParagraphStyle) -> ts.Paragraph:
        p = ts.Paragraph()
        p.style = pstyle
        state = {"pos": 0, "bold": 0, "italic": 0, "code": 0, "href": None, "link_text": ""}

        def cur_style() -> ts.TextStyle:
            st = self.mono.copy() if state["code"] else base.copy()
            if state["bold"]:
                st.font.weight = 700
                st.fake_bold = True
            if state["italic"]:
                st.fake_italic = True
            return st

        def add_text(text: str) -> None:
            if not text:
                return
            # ルビ記法
            pos = 0
            for m in RUBY_RE.finditer(text):
                if m.start() > pos:
                    emit(text[pos:m.start()])
                base_text = m.group(1) or m.group(3) or m.group(5)
                ruby = m.group(2) or m.group(4) or m.group(6)
                start = state["pos"]
                emit(base_text)
                p.annotate(ts.Annotation.ruby(start, state["pos"], ruby))
                pos = m.end()
            emit(text[pos:])

        def emit(text: str) -> None:
            if not text:
                return
            p.add_run(text, cur_style(), literal=bool(state["code"]))   # コードは {…} をそのまま出す
            state["pos"] += utf16len(text)
            if state["href"] is not None:
                state["link_text"] += text

        for c in children:
            t = c.type
            if t == "text":
                add_text(c.content)
            elif t == "softbreak":
                # 和文の途中の改行はアキにしない。欧文同士なら空白
                prev = p.text[-1:] if p.text else ""
                if prev and prev.isascii() and not prev.isspace():
                    emit(" ")
            elif t == "hardbreak":
                emit("\n")
            elif t == "strong_open":
                state["bold"] += 1
            elif t == "strong_close":
                state["bold"] -= 1
            elif t == "em_open":
                state["italic"] += 1
            elif t == "em_close":
                state["italic"] -= 1
            elif t == "code_inline":
                state["code"] += 1
                emit(c.content)
                state["code"] -= 1
            elif t == "link_open":
                state["href"] = c.attrGet("href") or ""
                state["link_text"] = ""
            elif t == "link_close":
                href = state["href"] or ""
                shown = state["link_text"]
                state["href"] = None
                if href and href != shown and not href.startswith("#"):
                    if self.opts.links == "footnote":
                        note = ts.Paragraph(href, ts.TextStyle(self.opts.font_mono, self.opts.size * 0.75), self.plain_pstyle)
                        p.add_footnote(note, ts.superscript_style(base))
                        state["pos"] += utf16len("{fn}")   # 番号に置き換わる（幅は変わるが位置は typeset 側で補正）
                    elif self.opts.links == "inline":
                        emit(f"（{href}）")
            elif t == "image":
                image = self._load_image(c.attrGet("src") or "")
                if image is not None:
                    p.add_image(image, ts.Size(0.0, base.size), cur_style())
                    state["pos"] += 1
                else:
                    add_text(c.content)
            elif t == "footnote_ref":
                fid = int(c.meta["id"])
                note = self._footnote_paragraph(fid)
                p.add_footnote(note, ts.superscript_style(base))
                state["pos"] += utf16len("{fn}")
            elif t == "math_inline" or t == "math_inline_double":
                p.add_object("tex", c.content.strip(), cur_style())
                state["pos"] += 1
            elif t == "html_inline":
                low = c.content.lower()
                if low.startswith("<br"):
                    emit("\n")
            elif t in ("s_open", "s_close"):
                pass
            elif c.children:
                # 未対応の入れ子はテキストだけ拾う
                for cc in c.children:
                    if cc.type == "text":
                        add_text(cc.content)
        return p

    def _footnote_paragraph(self, fid: int) -> ts.Paragraph:
        body = self.footnote_defs.get(fid, [])
        children: List[Token] = []
        first = True
        for t in body:
            if t.type == "inline":
                if not first:
                    children.append(Token("hardbreak", "br", 0))
                children.extend(x for x in (t.children or []) if x.type != "footnote_anchor")
                first = False
        ps = ts.ParagraphStyle()
        ps.first_line_indent = 0.0
        ps.line_height = 1.4
        ps.align = ts.Align.START
        return self._inline(children, self.small, ps)

    # ---- ページ ----

    def page_sequence(self) -> ts.PageSequence:
        o = self.opts
        seq = ts.PageSequence()
        seq.master.size = parse_paper(o.paper, o.landscape)
        vertical = str(o.writing).lower().startswith("v")
        seq.master.writing_mode = ts.WritingMode.VERTICAL_RL if vertical else ts.WritingMode.HORIZONTAL_TB
        seq.master.margin = parse_margins(o.margin, 20.0 if o.paper.upper() in ("A4", "B5") else 16.0)
        seq.master.columns = max(1, int(o.columns))
        if o.column_gap:
            seq.master.column_gap = float(o.column_gap)
        running = ts.TextStyle(o.font_heading, o.size * 0.8)
        header = o.header if o.header is not None else ("{title}" if o.title else None)
        if header:
            ps = ts.ParagraphStyle()
            ps.align = ts.Align.START
            ps.first_line_indent = 0.0
            seq.master.header = ts.RunningText(ts.Paragraph(header, running, ps), 8 * ts.MM)
        if o.footer:
            ps = ts.ParagraphStyle()
            ps.align = ts.Align.CENTER
            ps.first_line_indent = 0.0
            seq.master.footer = ts.RunningText(ts.Paragraph(o.footer, running, ps), 10 * ts.MM)
        return seq

    def layout(self) -> List[ts.Page]:
        o = self.opts
        layouter = ts.FlowLayouter(self.fonts)
        fields = {"title": o.title, "author": o.author, "date": o.date}
        pages = layouter.layout(
            self.flow, self.page_sequence(), fields=fields, draw_guides=o.draw_guides,
            balance_last_page=o.balance_last_page, figure_format=o.figure_format, table_format=o.table_format,
            equation_format=o.equation_format, objects=self.registry,
            footnote_marker_format=o.footnote_marker_format, footnote_label_format=o.footnote_label_format)
        if self.registry is not None:
            self.warnings.extend(self.registry.errors)
        return pages


# ------------------------------------------------------------------------------
# 数式ハンドラ（matplotlib の mathtext。任意）
# ------------------------------------------------------------------------------

def mathtext_handler(req: Dict[str, Any]) -> Any:
    """matplotlib の mathtext で TeX のサブセットを SVG にする（`pip install matplotlib`）"""
    try:
        import io
        import matplotlib  # type: ignore
        matplotlib.use("Agg")
        from matplotlib import rcParams  # type: ignore
        from matplotlib.figure import Figure  # type: ignore
        from matplotlib.mathtext import MathTextParser  # type: ignore
        from matplotlib.font_manager import FontProperties  # type: ignore
    except ImportError:
        return {"error": "matplotlib is not installed (pip install matplotlib)"}
    size = float(req["font_size"])
    src = "$" + req["source"] + "$"
    prop = FontProperties(size=size)
    parser = MathTextParser("path")
    width, height, depth, _glyphs, _rects = parser.parse(src, dpi=72, prop=prop)
    pad = 1.0
    w, h = width + pad * 2, height + pad * 2
    rcParams["svg.fonttype"] = "path"
    fig = Figure(figsize=(w / 72.0, h / 72.0), dpi=72)
    fig.patch.set_alpha(0.0)
    fig.text(pad / w, (depth + pad) / h, src, fontproperties=prop)
    buf = io.StringIO()
    fig.savefig(buf, format="svg", dpi=72, transparent=True)
    return {"svg": buf.getvalue(), "baseline": h - depth - pad}


# ------------------------------------------------------------------------------
# 簡易 YAML（PyYAML が無いときの front matter 用: `key: value` と `key: [a, b]` だけ）
# ------------------------------------------------------------------------------

def simple_yaml(text: str) -> Dict[str, Any]:
    out: Dict[str, Any] = {}
    for line in text.splitlines():
        if not line.strip() or line.strip().startswith("#") or ":" not in line:
            continue
        k, v = line.split(":", 1)
        v = v.strip()
        if v.startswith("[") and v.endswith("]"):
            out[k.strip()] = [x.strip().strip("'\"") for x in v[1:-1].split(",") if x.strip()]
        elif v.lower() in ("true", "false"):
            out[k.strip()] = v.lower() == "true"
        else:
            try:
                out[k.strip()] = float(v) if "." in v else int(v)
            except ValueError:
                out[k.strip()] = v.strip("'\"")
    return out


# ------------------------------------------------------------------------------
# 入口
# ------------------------------------------------------------------------------

def build_flow(text: str, opts: Optional[Options] = None, base_dir: str = ".",
               overrides: Optional[Dict[str, Any]] = None) -> Converter:
    """Markdown → Converter（flow / fonts / registry を持つ）。overrides は front matter より優先する設定"""
    conv = Converter(opts or Options(), base_dir, overrides)
    conv.convert(text)
    return conv


def convert_text(text: str, output: str, opts: Optional[Options] = None, base_dir: str = ".",
                 png_dpi: float = 0.0, overrides: Optional[Dict[str, Any]] = None) -> Tuple[int, List[str]]:
    """Markdown 文字列 → PDF。(ページ数, 警告) を返す。png_dpi > 0 なら各ページの PNG も出す"""
    conv = build_flow(text, opts, base_dir, overrides)
    pages = conv.layout()
    ok, warnings = ts.save_pdf(pages, output, conv.opts.title, conv.opts.author)
    if not ok:
        conv.warnings.append(f"failed to write {output}")
    conv.warnings.extend(warnings)
    if png_dpi > 0:
        stem = os.path.splitext(output)[0]
        for pg in pages:
            pg.save_png(f"{stem}_p{pg.number}.png", dpi=png_dpi)
    return len(pages), conv.warnings


def convert_file(path: str, output: Optional[str] = None, opts: Optional[Options] = None,
                 png_dpi: float = 0.0, overrides: Optional[Dict[str, Any]] = None) -> Tuple[int, List[str]]:
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if output is None:
        output = os.path.splitext(path)[0] + ".pdf"
    return convert_text(text, output, opts, os.path.dirname(os.path.abspath(path)), png_dpi, overrides)
