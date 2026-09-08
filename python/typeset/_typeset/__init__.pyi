"""
typeset — 縦書き・横書きの日本語組版ライブラリ（C++ コアの Python バインディング）

流れ: FontSet でフォントを開く → TextStyle / Paragraph / Flow で内容を組み立てる → PageSequence で判型・段・柱を決める
→ FlowLayouter.layout() でページ列にする → Page.save_png / save_svg、save_pdf(pages, path) で出力する。
単位は pt（MM / CM / INCH の定数で換算）。座標はページ左上原点・y 下向き。
本文中の {page} {pages} {title}（柱・ノンブル）、{ref:label} {page:label}（相互参照）、{fig} {table} {eq}（番号）、
{index:よみ|用語}（索引）は FlowLayouter が置換・収集する。
Markdown → PDF は typeset.md（typeset-md コマンド）。
"""
from __future__ import annotations
import collections.abc
import typing
from . import paper
__all__: list[str] = ['Align', 'Annotation', 'BlockStyle', 'BreakKind', 'BreakOptions', 'CM', 'Color', 'EmphasisMark', 'Flow', 'FlowLayouter', 'FontSet', 'FontSpec', 'INCH', 'Image', 'ImageBlock', 'ImagePlacement', 'IndexBlock', 'InlineRun', 'LineBreakStrategy', 'LineInfo', 'ListBlock', 'ListMarker', 'MM', 'Margins', 'ObjectBlock', 'ObjectRegistry', 'ObjectResult', 'Page', 'PageMaster', 'PageSequence', 'Paragraph', 'ParagraphStyle', 'Point', 'Rect', 'RubyMode', 'RunningText', 'Size', 'SpacingOptions', 'Stroke', 'TableBlock', 'TableBorders', 'TableCell', 'TableColumn', 'TableRow', 'TextOrientation', 'TextStyle', 'TocBlock', 'VAlign', 'WritingMode', 'image_from_rgba', 'layout_paragraph', 'load_image', 'paper', 'save_pdf', 'superscript_style']
class Align:
    """
    Members:
    
      START
    
      END
    
      CENTER
    
      JUSTIFY
    """
    CENTER: typing.ClassVar[Align]  # value = <Align.CENTER: 2>
    END: typing.ClassVar[Align]  # value = <Align.END: 1>
    JUSTIFY: typing.ClassVar[Align]  # value = <Align.JUSTIFY: 3>
    START: typing.ClassVar[Align]  # value = <Align.START: 0>
    __members__: typing.ClassVar[dict[str, Align]]  # value = {'START': <Align.START: 0>, 'END': <Align.END: 1>, 'CENTER': <Align.CENTER: 2>, 'JUSTIFY': <Align.JUSTIFY: 3>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class Annotation:
    """
    行内注記。ruby / tate_chu_yoko / emphasis / warichu / jidori の静的メソッドで作り、範囲は段落テキストの UTF-16 位置
    """
    text: str
    @staticmethod
    def emphasis(start: typing.SupportsInt | typing.SupportsIndex, end: typing.SupportsInt | typing.SupportsIndex, mark: EmphasisMark = ..., scale: typing.SupportsFloat | typing.SupportsIndex = 0.5, opposite_side: bool = False) -> Annotation:
        ...
    @staticmethod
    def jidori(start: typing.SupportsInt | typing.SupportsIndex, end: typing.SupportsInt | typing.SupportsIndex, em: typing.SupportsFloat | typing.SupportsIndex) -> Annotation:
        ...
    @staticmethod
    def ruby(start: typing.SupportsInt | typing.SupportsIndex, end: typing.SupportsInt | typing.SupportsIndex, text: str, mode: RubyMode = ..., scale: typing.SupportsFloat | typing.SupportsIndex = 0.5) -> Annotation:
        ...
    @staticmethod
    def tate_chu_yoko(start: typing.SupportsInt | typing.SupportsIndex, end: typing.SupportsInt | typing.SupportsIndex) -> Annotation:
        ...
    @staticmethod
    def warichu(start: typing.SupportsInt | typing.SupportsIndex, end: typing.SupportsInt | typing.SupportsIndex, text: str = '', scale: typing.SupportsFloat | typing.SupportsIndex = 0.5) -> Annotation:
        ...
    @property
    def end(self) -> int:
        ...
    @end.setter
    def end(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def scale(self) -> float:
        ...
    @scale.setter
    def scale(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def start(self) -> int:
        ...
    @start.setter
    def start(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class BlockStyle:
    """
    ブロックの前後アキ・改ページ制御（orphans / widows / keep_with_next / keep_together / break_before / break_after）・段抜き・ラベル・背景・余白
    """
    background: typeset._typeset.Color | None
    break_after: BreakKind
    break_before: BreakKind
    keep_together: bool
    keep_with_next: bool
    label: str
    span_columns: bool
    def __init__(self) -> None:
        ...
    @property
    def orphans(self) -> int:
        ...
    @orphans.setter
    def orphans(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def padding(self) -> float:
        ...
    @padding.setter
    def padding(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def space_after(self) -> float:
        ...
    @space_after.setter
    def space_after(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def space_before(self) -> float:
        ...
    @space_before.setter
    def space_before(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def widows(self) -> int:
        ...
    @widows.setter
    def widows(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class BreakKind:
    """
    Members:
    
      AUTO
    
      COLUMN
    
      PAGE
    """
    AUTO: typing.ClassVar[BreakKind]  # value = <BreakKind.AUTO: 0>
    COLUMN: typing.ClassVar[BreakKind]  # value = <BreakKind.COLUMN: 1>
    PAGE: typing.ClassVar[BreakKind]  # value = <BreakKind.PAGE: 2>
    __members__: typing.ClassVar[dict[str, BreakKind]]  # value = {'AUTO': <BreakKind.AUTO: 0>, 'COLUMN': <BreakKind.COLUMN: 1>, 'PAGE': <BreakKind.PAGE: 2>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class BreakOptions:
    """
    行分割の方法（Greedy / Knuth–Plass）と両端揃え
    """
    justify: bool
    strategy: LineBreakStrategy
    def __init__(self) -> None:
        ...
    @property
    def line_penalty(self) -> float:
        ...
    @line_penalty.setter
    def line_penalty(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def tolerance(self) -> float:
        ...
    @tolerance.setter
    def tolerance(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class Color:
    """
    色（RGBA、0–255）
    """
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, r: typing.SupportsInt | typing.SupportsIndex, g: typing.SupportsInt | typing.SupportsIndex, b: typing.SupportsInt | typing.SupportsIndex, a: typing.SupportsInt | typing.SupportsIndex = 255) -> None:
        ...
    @property
    def a(self) -> int:
        ...
    @a.setter
    def a(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def b(self) -> int:
        ...
    @b.setter
    def b(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def g(self) -> int:
        ...
    @g.setter
    def g(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def r(self) -> int:
        ...
    @r.setter
    def r(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class EmphasisMark:
    """
    Members:
    
      SESAME
    
      OPEN_SESAME
    
      DOT
    
      FILLED_CIRCLE
    
      OPEN_CIRCLE
    """
    DOT: typing.ClassVar[EmphasisMark]  # value = <EmphasisMark.DOT: 2>
    FILLED_CIRCLE: typing.ClassVar[EmphasisMark]  # value = <EmphasisMark.FILLED_CIRCLE: 3>
    OPEN_CIRCLE: typing.ClassVar[EmphasisMark]  # value = <EmphasisMark.OPEN_CIRCLE: 4>
    OPEN_SESAME: typing.ClassVar[EmphasisMark]  # value = <EmphasisMark.OPEN_SESAME: 1>
    SESAME: typing.ClassVar[EmphasisMark]  # value = <EmphasisMark.SESAME: 0>
    __members__: typing.ClassVar[dict[str, EmphasisMark]]  # value = {'SESAME': <EmphasisMark.SESAME: 0>, 'OPEN_SESAME': <EmphasisMark.OPEN_SESAME: 1>, 'DOT': <EmphasisMark.DOT: 2>, 'FILLED_CIRCLE': <EmphasisMark.FILLED_CIRCLE: 3>, 'OPEN_CIRCLE': <EmphasisMark.OPEN_CIRCLE: 4>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class Flow:
    """
    ブロックの列。add_paragraph / add_heading / add_list / add_table / add_image / add_object / add_toc / add_index / add_rule / add_page_break …
    """
    def __init__(self) -> None:
        ...
    def add_column_break(self) -> None:
        ...
    def add_heading(self, paragraph: Paragraph, level: typing.SupportsInt | typing.SupportsIndex = 1, style: typeset._typeset.BlockStyle | None = None, numbered: bool = False) -> None:
        ...
    def add_image(self, image_block: ImageBlock) -> None:
        ...
    def add_index(self, index: IndexBlock) -> None:
        ...
    def add_labeled(self, label: Paragraph, body: Paragraph, label_width: typing.SupportsFloat | typing.SupportsIndex, gap: typing.SupportsFloat | typing.SupportsIndex = 0.0, style: BlockStyle = ...) -> None:
        ...
    def add_list(self, list: ListBlock) -> None:
        ...
    def add_object(self, object_block: ObjectBlock) -> None:
        ...
    def add_page_break(self) -> None:
        ...
    def add_paragraph(self, paragraph: Paragraph, style: BlockStyle = ...) -> None:
        ...
    def add_rule(self, thickness: typing.SupportsFloat | typing.SupportsIndex = 0.5, color: Color = ..., style: BlockStyle = ...) -> None:
        ...
    def add_section(self, columns: typing.SupportsInt | typing.SupportsIndex | None = None, column_gap: typing.SupportsFloat | typing.SupportsIndex | None = None) -> None:
        ...
    def add_spacer(self, size: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    def add_table(self, table: TableBlock) -> None:
        ...
    def add_toc(self, toc: TocBlock) -> None:
        ...
    @property
    def size(self) -> int:
        ...
class FlowLayouter:
    """
    Flow をページ列へ流し込む。layout(flow, sequence, ...) がページの列を返す（採番・参照・目次・索引は多パス）
    """
    def __init__(self, fonts: FontSet) -> None:
        ...
    def layout(self, flow: Flow, sequence: PageSequence, fields: collections.abc.Mapping[str, str] = {}, draw_guides: bool = False, balance_last_page: bool = True, figure_format: str = '図 {n}', table_format: str = '表 {n}', equation_format: str = '({n})', objects: ObjectRegistry = None, footnote_marker_format: str = '{n}', footnote_label_format: str = '{n} ', footnote_per_page: bool = False) -> list[Page]:
        """
        Flow をページ列へ流し込む。fields は柱・ノンブル・本文の {name} 置換
        """
class FontSet:
    """
    フォントの集合。load_file / load_bytes で開き、TextStyle の family（キーまたは family 名）で引く。文字が無ければ次の family へフォールバックする
    """
    def __init__(self) -> None:
        ...
    def load_bytes(self, key: str, data: bytes, face_index: typing.SupportsInt | typing.SupportsIndex = 0) -> bool:
        ...
    def load_file(self, path: str, key: str = '', face_index: typing.SupportsInt | typing.SupportsIndex = 0) -> bool:
        """
        フォントファイルを開く。key を省略するとパスがキーになる
        """
    @property
    def size(self) -> int:
        ...
class FontSpec:
    """
    フォント指定（family の列＋ウェイト＋斜体）
    """
    italic: bool
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, family: collections.abc.Sequence[str], weight: typing.SupportsInt | typing.SupportsIndex = 400, italic: bool = False) -> None:
        ...
    @property
    def family(self) -> list[str]:
        ...
    @family.setter
    def family(self, arg0: collections.abc.Sequence[str]) -> None:
        ...
    @property
    def weight(self) -> int:
        ...
    @weight.setter
    def weight(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class Image:
    @property
    def height(self) -> int:
        ...
    @property
    def width(self) -> int:
        ...
class ImageBlock:
    """
    画像ブロック。placement で流れに置くか回り込み（FLOAT_START / FLOAT_END）、caption の {fig} は図番号
    """
    align: Align
    block: BlockStyle
    caption: typeset._typeset.Paragraph | None
    image: Image
    placement: ImagePlacement
    size: Size
    def __init__(self) -> None:
        ...
    @property
    def caption_gap(self) -> float:
        ...
    @caption_gap.setter
    def caption_gap(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def gap(self) -> float:
        ...
    @gap.setter
    def gap(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class ImagePlacement:
    """
    Members:
    
      BLOCK
    
      FLOAT_START
    
      FLOAT_END
    """
    BLOCK: typing.ClassVar[ImagePlacement]  # value = <ImagePlacement.BLOCK: 0>
    FLOAT_END: typing.ClassVar[ImagePlacement]  # value = <ImagePlacement.FLOAT_END: 2>
    FLOAT_START: typing.ClassVar[ImagePlacement]  # value = <ImagePlacement.FLOAT_START: 1>
    __members__: typing.ClassVar[dict[str, ImagePlacement]]  # value = {'BLOCK': <ImagePlacement.BLOCK: 0>, 'FLOAT_START': <ImagePlacement.FLOAT_START: 1>, 'FLOAT_END': <ImagePlacement.FLOAT_END: 2>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class IndexBlock:
    """
    索引。本文の {index:用語} / {index:よみ|用語} を集めて読みの順に並べる
    """
    block: BlockStyle
    group_style: typeset._typeset.TextStyle | None
    grouped: bool
    leader: bool
    page_separator: str
    style: TextStyle
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, style: TextStyle) -> None:
        ...
    @property
    def line_height(self) -> float:
        ...
    @line_height.setter
    def line_height(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class InlineRun:
    """
    スタイルの付いたテキスト片（段落の構成要素）。literal なら {name} の置換をしない
    """
    literal: bool
    style: TextStyle
    text: str
    def __init__(self, text: str, style: TextStyle) -> None:
        ...
class LineBreakStrategy:
    """
    Members:
    
      GREEDY
    
      KNUTH_PLASS
    """
    GREEDY: typing.ClassVar[LineBreakStrategy]  # value = <LineBreakStrategy.GREEDY: 0>
    KNUTH_PLASS: typing.ClassVar[LineBreakStrategy]  # value = <LineBreakStrategy.KNUTH_PLASS: 1>
    __members__: typing.ClassVar[dict[str, LineBreakStrategy]]  # value = {'GREEDY': <LineBreakStrategy.GREEDY: 0>, 'KNUTH_PLASS': <LineBreakStrategy.KNUTH_PLASS: 1>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class LineInfo:
    """
    layout_paragraph が返す 1 行の情報
    """
    def __repr__(self) -> str:
        ...
    @property
    def char_end(self) -> int:
        ...
    @property
    def char_start(self) -> int:
        ...
    @property
    def hanging(self) -> bool:
        ...
    @property
    def indent(self) -> float:
        ...
    @property
    def length(self) -> float:
        ...
    @property
    def line_index(self) -> int:
        ...
    @property
    def natural_length(self) -> float:
        ...
    @property
    def paragraph_end(self) -> bool:
        ...
class ListBlock:
    """
    箇条書き（記号／番号）
    """
    block: BlockStyle
    bullet: str
    marker: ListMarker
    number_suffix: str
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, items: collections.abc.Sequence[Paragraph], marker: ListMarker = ...) -> None:
        ...
    @property
    def gap(self) -> float:
        ...
    @gap.setter
    def gap(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def item_gap(self) -> float:
        ...
    @item_gap.setter
    def item_gap(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def items(self) -> list[Paragraph]:
        ...
    @items.setter
    def items(self, arg0: collections.abc.Sequence[Paragraph]) -> None:
        ...
    @property
    def label_width(self) -> float:
        ...
    @label_width.setter
    def label_width(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class ListMarker:
    """
    Members:
    
      BULLET
    
      NUMBERED
    """
    BULLET: typing.ClassVar[ListMarker]  # value = <ListMarker.BULLET: 0>
    NUMBERED: typing.ClassVar[ListMarker]  # value = <ListMarker.NUMBERED: 1>
    __members__: typing.ClassVar[dict[str, ListMarker]]  # value = {'BULLET': <ListMarker.BULLET: 0>, 'NUMBERED': <ListMarker.NUMBERED: 1>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class Margins:
    """
    余白（top / bottom / inner / outer。pt）。duplex では inner がノド側
    """
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, top: typing.SupportsFloat | typing.SupportsIndex, bottom: typing.SupportsFloat | typing.SupportsIndex, inner: typing.SupportsFloat | typing.SupportsIndex, outer: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def bottom(self) -> float:
        ...
    @bottom.setter
    def bottom(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def inner(self) -> float:
        ...
    @inner.setter
    def inner(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def outer(self) -> float:
        ...
    @outer.setter
    def outer(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def top(self) -> float:
        ...
    @top.setter
    def top(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class ObjectBlock:
    """
    外部ハンドラで生成するオブジェクトのブロック（別行立ての数式・グラフなど）
    """
    align: Align
    block: BlockStyle
    caption: typeset._typeset.Paragraph | None
    handler: str
    numbered: bool
    source: str
    text_style: TextStyle
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, handler: str, source: str, style: TextStyle, params: collections.abc.Mapping[str, str] = {}, numbered: bool = False) -> None:
        ...
    @property
    def caption_gap(self) -> float:
        ...
    @caption_gap.setter
    def caption_gap(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def params(self) -> dict[str, str]:
        ...
    @params.setter
    def params(self, arg0: collections.abc.Mapping[str, str]) -> None:
        ...
class ObjectRegistry:
    """
    オブジェクトハンドラの登録。add(name, fn) の fn は request(dict) を受けて SVG 文字列、または {'svg': str, 'baseline': pt} を返す。add_command(name, cmd) は `cmd <request.json>` の標準出力（SVG）を読む
    """
    def __init__(self) -> None:
        ...
    def add(self, name: str, fn: collections.abc.Callable) -> None:
        ...
    def add_command(self, name: str, command: str, workdir: str = '') -> None:
        ...
    def clear_cache(self) -> None:
        ...
    def has(self, name: str) -> bool:
        ...
    @property
    def cache_size(self) -> int:
        ...
    @property
    def errors(self) -> list[str]:
        ...
class ObjectResult:
    def ok(self) -> bool:
        ...
    @property
    def baseline(self) -> float:
        ...
    @property
    def error(self) -> str:
        ...
    @property
    def size(self) -> Size:
        ...
class Page:
    """
    組版結果の 1 ページ（番号＋表示リスト）。save_png / save_svg / to_svg で出力、PDF は save_pdf(pages, path)
    """
    def render(self, dpi: typing.SupportsFloat | typing.SupportsIndex = 144.0, background: Color = ...) -> tuple[int, int, bytes]:
        """
        ラスタライズして (width, height, ARGB8888 bytes) を返す
        """
    def save_png(self, path: str, dpi: typing.SupportsFloat | typing.SupportsIndex = 144.0, background: Color = ...) -> bool:
        ...
    def save_svg(self, path: str) -> bool:
        ...
    def to_svg(self) -> str:
        ...
    @property
    def number(self) -> int:
        ...
class PageMaster:
    """
    ページマスタ: 判型・余白・段数・書字方向・柱・ノンブル・duplex
    """
    duplex: bool
    footer: typeset._typeset.RunningText | None
    header: typeset._typeset.RunningText | None
    margin: Margins
    size: Size
    writing_mode: WritingMode
    def __init__(self) -> None:
        ...
    def body_rect(self, page_number: typing.SupportsInt | typing.SupportsIndex) -> Rect:
        ...
    @property
    def column_gap(self) -> float:
        ...
    @column_gap.setter
    def column_gap(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def columns(self) -> int:
        ...
    @columns.setter
    def columns(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class PageSequence:
    """
    ページ列の設定（マスタ＋開始ページ番号）
    """
    master: PageMaster
    def __init__(self) -> None:
        ...
    @property
    def first_page_number(self) -> int:
        ...
    @first_page_number.setter
    def first_page_number(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class Paragraph:
    """
    段落: run の列＋注記＋段落スタイル。add_run / add_image / add_object / add_footnote / annotate で組み立てる
    """
    style: ParagraphStyle
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, text: str, style: TextStyle, paragraph_style: typeset._typeset.ParagraphStyle | None = None) -> None:
        ...
    def add_footnote(self, note: Paragraph, marker_style: TextStyle) -> None:
        """
        脚注を足す。本文のこの位置に番号（marker_style は superscript_style() で上付きに）が入り、注は段末に置かれる
        """
    def add_image(self, image: ..., size: Size, style: TextStyle) -> None:
        """
        行内画像を足す（本文中の位置は 1 文字ぶん）
        """
    def add_object(self, handler: str, source: str, style: TextStyle, params: collections.abc.Mapping[str, str] = {}) -> None:
        """
        外部ハンドラのオブジェクト（数式など）を行内に足す。layout(objects=...) の ObjectRegistry で解決される
        """
    def add_run(self, text: str, style: TextStyle, literal: bool = False) -> None:
        """
        run を足す。literal なら {name} の置換や {index:} の収集をしない（コード用）
        """
    def annotate(self, arg0: Annotation) -> None:
        ...
    @property
    def annotations(self) -> list[Annotation]:
        ...
    @annotations.setter
    def annotations(self, arg0: collections.abc.Sequence[Annotation]) -> None:
        ...
    @property
    def runs(self) -> list[InlineRun]:
        ...
    @runs.setter
    def runs(self, arg0: collections.abc.Sequence[InlineRun]) -> None:
        ...
    @property
    def text(self) -> str:
        ...
class ParagraphStyle:
    """
    段落スタイル: 揃え・行送り・一字下げ・向き・空白保持（コード）・タブ幅・アキ量・行分割
    """
    align: Align
    line_break: BreakOptions
    orientation: TextOrientation
    preserve_spaces: bool
    spacing: SpacingOptions
    def __init__(self) -> None:
        ...
    def copy(self) -> ParagraphStyle:
        ...
    @property
    def first_line_indent(self) -> float:
        ...
    @first_line_indent.setter
    def first_line_indent(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def line_height(self) -> float:
        ...
    @line_height.setter
    def line_height(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def line_pitch(self) -> float:
        ...
    @line_pitch.setter
    def line_pitch(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def tab_width(self) -> int:
        ...
    @tab_width.setter
    def tab_width(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class Point:
    """
    点（pt、ページ左上原点・y 下向き）
    """
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, x: typing.SupportsFloat | typing.SupportsIndex, y: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def x(self) -> float:
        ...
    @x.setter
    def x(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def y(self) -> float:
        ...
    @y.setter
    def y(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class Rect:
    """
    矩形（pt）。x, y, w, h
    """
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, x: typing.SupportsFloat | typing.SupportsIndex, y: typing.SupportsFloat | typing.SupportsIndex, w: typing.SupportsFloat | typing.SupportsIndex, h: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def h(self) -> float:
        ...
    @h.setter
    def h(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def w(self) -> float:
        ...
    @w.setter
    def w(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def x(self) -> float:
        ...
    @x.setter
    def x(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def y(self) -> float:
        ...
    @y.setter
    def y(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class RubyMode:
    """
    Members:
    
      GROUP
    
      MONO
    
      JUKUGO
    """
    GROUP: typing.ClassVar[RubyMode]  # value = <RubyMode.GROUP: 0>
    JUKUGO: typing.ClassVar[RubyMode]  # value = <RubyMode.JUKUGO: 2>
    MONO: typing.ClassVar[RubyMode]  # value = <RubyMode.MONO: 1>
    __members__: typing.ClassVar[dict[str, RubyMode]]  # value = {'GROUP': <RubyMode.GROUP: 0>, 'MONO': <RubyMode.MONO: 1>, 'JUKUGO': <RubyMode.JUKUGO: 2>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class RunningText:
    """
    柱・ノンブル。{page} {pages} {title} を置換。mirror_on_even で見開きの偶数ページを左右反転
    """
    mirror_on_even: bool
    paragraph: Paragraph
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, paragraph: Paragraph, offset: typing.SupportsFloat | typing.SupportsIndex = 0.0) -> None:
        ...
    @property
    def offset(self) -> float:
        ...
    @offset.setter
    def offset(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class Size:
    """
    大きさ（pt）
    """
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, w: typing.SupportsFloat | typing.SupportsIndex, h: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def h(self) -> float:
        ...
    @h.setter
    def h(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def w(self) -> float:
        ...
    @w.setter
    def w(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class SpacingOptions:
    """
    約物の詰め・ぶら下げ・和欧間・和字間の伸縮（JLReq のアキ量表）
    """
    hanging_punctuation: bool
    latin_gap: bool
    punctuation_spacing: bool
    def __init__(self) -> None:
        ...
    @property
    def kanji_skip_shrink(self) -> float:
        ...
    @kanji_skip_shrink.setter
    def kanji_skip_shrink(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def kanji_skip_stretch(self) -> float:
        ...
    @kanji_skip_stretch.setter
    def kanji_skip_stretch(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class Stroke:
    """
    線の描き方（色・幅・端・角）
    """
    color: Color
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, color: Color, width: typing.SupportsFloat | typing.SupportsIndex = 1.0) -> None:
        ...
    @property
    def width(self) -> float:
        ...
    @width.setter
    def width(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class TableBlock:
    """
    表。列幅は固定／自動、colspan / rowspan、ヘッダの繰り返し、段より高い行の分割、caption の {table} は表番号
    """
    align: Align
    block: BlockStyle
    borders: TableBorders
    caption: typeset._typeset.Paragraph | None
    full_width: bool
    repeat_header: bool
    def __init__(self) -> None:
        ...
    @property
    def caption_gap(self) -> float:
        ...
    @caption_gap.setter
    def caption_gap(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def cell_padding(self) -> float:
        ...
    @cell_padding.setter
    def cell_padding(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def columns(self) -> list[TableColumn]:
        ...
    @columns.setter
    def columns(self, arg0: collections.abc.Sequence[TableColumn]) -> None:
        ...
    @property
    def rows(self) -> list[TableRow]:
        ...
    @rows.setter
    def rows(self, arg0: collections.abc.Sequence[TableRow]) -> None:
        ...
class TableBorders:
    """
    表の罫線（外枠・内側・ヘッダ下の太さ、縦横の有無、色）
    """
    color: Color
    horizontal: bool
    vertical: bool
    def __init__(self) -> None:
        ...
    @property
    def header_rule(self) -> float:
        ...
    @header_rule.setter
    def header_rule(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def inner(self) -> float:
        ...
    @inner.setter
    def inner(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def outer(self) -> float:
        ...
    @outer.setter
    def outer(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class TableCell:
    """
    表のセル（段落の列、colspan / rowspan、縦位置）
    """
    valign: VAlign
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, paragraph: Paragraph, colspan: typing.SupportsInt | typing.SupportsIndex = 1, rowspan: typing.SupportsInt | typing.SupportsIndex = 1, valign: VAlign = ...) -> None:
        ...
    @property
    def colspan(self) -> int:
        ...
    @colspan.setter
    def colspan(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def paras(self) -> list[Paragraph]:
        ...
    @paras.setter
    def paras(self, arg0: collections.abc.Sequence[Paragraph]) -> None:
        ...
    @property
    def rowspan(self) -> int:
        ...
    @rowspan.setter
    def rowspan(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class TableColumn:
    """
    表の列（幅 0 なら自動、揃え）
    """
    align: Align
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, width: typing.SupportsFloat | typing.SupportsIndex = 0.0, align: Align = ...) -> None:
        ...
    @property
    def width(self) -> float:
        ...
    @width.setter
    def width(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class TableRow:
    """
    表の行（セルの列、header ならページをまたいで繰り返す）
    """
    header: bool
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, cells: collections.abc.Sequence[TableCell], header: bool = False) -> None:
        ...
    @property
    def cells(self) -> list[TableCell]:
        ...
    @cells.setter
    def cells(self, arg0: collections.abc.Sequence[TableCell]) -> None:
        ...
class TextOrientation:
    """
    Members:
    
      MIXED
    
      UPRIGHT
    
      SIDEWAYS
    """
    MIXED: typing.ClassVar[TextOrientation]  # value = <TextOrientation.MIXED: 0>
    SIDEWAYS: typing.ClassVar[TextOrientation]  # value = <TextOrientation.SIDEWAYS: 2>
    UPRIGHT: typing.ClassVar[TextOrientation]  # value = <TextOrientation.UPRIGHT: 1>
    __members__: typing.ClassVar[dict[str, TextOrientation]]  # value = {'MIXED': <TextOrientation.MIXED: 0>, 'UPRIGHT': <TextOrientation.UPRIGHT: 1>, 'SIDEWAYS': <TextOrientation.SIDEWAYS: 2>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class TextStyle:
    """
    文字スタイル: フォント・サイズ・色・縁取り・字間・向き・平体長体・合成ボールド／斜体・ベースラインのずらし
    """
    fake_bold: bool
    fake_italic: bool
    fill: Color
    font: FontSpec
    language: str
    orientation: typeset._typeset.TextOrientation | None
    stroke: typeset._typeset.Stroke | None
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, family: collections.abc.Sequence[str], size: typing.SupportsFloat | typing.SupportsIndex = 10.0, fill: Color = ...) -> None:
        ...
    def copy(self) -> TextStyle:
        ...
    @property
    def baseline_shift(self) -> float:
        ...
    @baseline_shift.setter
    def baseline_shift(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def letter_spacing(self) -> float:
        ...
    @letter_spacing.setter
    def letter_spacing(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def scale_x(self) -> float:
        ...
    @scale_x.setter
    def scale_x(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def scale_y(self) -> float:
        ...
    @scale_y.setter
    def scale_y(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def size(self) -> float:
        ...
    @size.setter
    def size(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class TocBlock:
    """
    目次。見出し（採番済み）とページ番号を前のパスから集めて並べる
    """
    block: BlockStyle
    leader: bool
    style: TextStyle
    sub_style: typeset._typeset.TextStyle | None
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, style: TextStyle, max_level: typing.SupportsInt | typing.SupportsIndex = 2) -> None:
        ...
    @property
    def indent_per_level(self) -> float:
        ...
    @indent_per_level.setter
    def indent_per_level(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def line_height(self) -> float:
        ...
    @line_height.setter
    def line_height(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def max_level(self) -> int:
        ...
    @max_level.setter
    def max_level(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class VAlign:
    """
    Members:
    
      TOP
    
      MIDDLE
    
      BOTTOM
    """
    BOTTOM: typing.ClassVar[VAlign]  # value = <VAlign.BOTTOM: 2>
    MIDDLE: typing.ClassVar[VAlign]  # value = <VAlign.MIDDLE: 1>
    TOP: typing.ClassVar[VAlign]  # value = <VAlign.TOP: 0>
    __members__: typing.ClassVar[dict[str, VAlign]]  # value = {'TOP': <VAlign.TOP: 0>, 'MIDDLE': <VAlign.MIDDLE: 1>, 'BOTTOM': <VAlign.BOTTOM: 2>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class WritingMode:
    """
    Members:
    
      HORIZONTAL_TB
    
      VERTICAL_RL
    
      VERTICAL_LR
    """
    HORIZONTAL_TB: typing.ClassVar[WritingMode]  # value = <WritingMode.HORIZONTAL_TB: 0>
    VERTICAL_LR: typing.ClassVar[WritingMode]  # value = <WritingMode.VERTICAL_LR: 2>
    VERTICAL_RL: typing.ClassVar[WritingMode]  # value = <WritingMode.VERTICAL_RL: 1>
    __members__: typing.ClassVar[dict[str, WritingMode]]  # value = {'HORIZONTAL_TB': <WritingMode.HORIZONTAL_TB: 0>, 'VERTICAL_RL': <WritingMode.VERTICAL_RL: 1>, 'VERTICAL_LR': <WritingMode.VERTICAL_LR: 2>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
def image_from_rgba(width: typing.SupportsInt | typing.SupportsIndex, height: typing.SupportsInt | typing.SupportsIndex, rgba: bytes) -> Image:
    ...
def layout_paragraph(fonts: FontSet, paragraph: Paragraph, writing_mode: WritingMode, line_lengths: collections.abc.Sequence[typing.SupportsFloat | typing.SupportsIndex] = [], default_length: typing.SupportsFloat | typing.SupportsIndex = 200.0) -> list[LineInfo]:
    """
    段落を組んで行ごとの文字範囲と長さを返す（行長は行ごとに指定できる = \\parshape）
    """
def load_image(path: str) -> Image:
    """
    PNG / JPEG / BMP / GIF を読む
    """
def save_pdf(pages: collections.abc.Sequence[Page], path: str, title: str = '', author: str = '', subset_fonts: bool = True, compress: bool = True) -> tuple[bool, list[str]]:
    """
    ページ列を 1 つの PDF に書く。(ok, warnings) を返す
    """
def superscript_style(style: TextStyle) -> TextStyle:
    """
    上付き（脚注記号・指数用）のスタイルを作る
    """
CM: float = 28.346458435058594
INCH: float = 72.0
MM: float = 2.8346457481384277
