"""
jtypeset — 縦書き・横書きの日本語組版ライブラリ typeset の Python バインディング

流れ: FontSet でフォントを開く → TextStyle / Paragraph / Flow で内容を組み立てる → PageSequence で判型・段・柱を決める
→ FlowLayouter.layout() でページ列にする → Page.save_png / save_svg、save_pdf(pages, path) で出力する。
単位は pt（MM / CM / INCH の定数で換算）。座標はページ左上原点・y 下向き。
本文中の {page} {pages} {title}（柱・ノンブル）、{ref:label} {page:label}（相互参照）、{fig} {table} {eq}（番号）、
{index:よみ|用語}（索引）は FlowLayouter が置換・収集する。
Markdown → PDF は jtypeset.md（jtypeset-md コマンド）。
"""
from __future__ import annotations
import collections.abc
import typing
from . import paper
__all__: list[str] = ['Align', 'Annotation', 'BlockAlign', 'BlockStyle', 'BreakKind', 'BreakOptions', 'CM', 'CharInfo', 'CodepointRange', 'Color', 'Direction', 'EmojiPresentation', 'EmphasisMark', 'Flow', 'FlowLayouter', 'FontDeclaration', 'FontSet', 'FontSpec', 'GradientStop', 'HAS_MICROTEX', 'HitResult', 'HyphenationDictionary', 'Hyphenator', 'INCH', 'Image', 'ImageBlock', 'ImagePlacement', 'IndexBlock', 'InlineRun', 'KinsokuLevel', 'LineBreakStrategy', 'LineInfo', 'ListBlock', 'ListMarker', 'MM', 'Margins', 'ObjectBlock', 'ObjectRegistry', 'ObjectResult', 'Page', 'PageMaster', 'PageSequence', 'Paint', 'PaintKind', 'PaintUnits', 'Paragraph', 'ParagraphLayout', 'ParagraphStyle', 'PlaceholderRect', 'Point', 'Rect', 'RubyMode', 'RunningText', 'Size', 'SpacingOptions', 'Stroke', 'TabAlign', 'TabStop', 'TableBlock', 'TableBorders', 'TableCell', 'TableColumn', 'TableRow', 'TagLink', 'TagMarker', 'TagParseOptions', 'TagParseResult', 'TagPlaceholder', 'TextDecoration', 'TextLayer', 'TextMetrics', 'TextOrientation', 'TextShadow', 'TextStyle', 'TocBlock', 'VAlign', 'WrapMode', 'WritingMode', 'fit_paragraph', 'image_from_rgba', 'layout_paragraph', 'load_image', 'measure_text', 'paper', 'parse_tagged_text', 'save_pdf', 'strip_tags', 'superscript_style']
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
    def indent(start: typing.SupportsInt | typing.SupportsIndex, end: typing.SupportsInt | typing.SupportsIndex, indent_em: typing.SupportsFloat | typing.SupportsIndex) -> Annotation:
        """
        途中からの字下げ: 行頭が [start, end) にある行を indent_em 下げる
        """
    @staticmethod
    def jidori(start: typing.SupportsInt | typing.SupportsIndex, end: typing.SupportsInt | typing.SupportsIndex, em: typing.SupportsFloat | typing.SupportsIndex) -> Annotation:
        ...
    @staticmethod
    def move_to(start: typing.SupportsInt | typing.SupportsIndex, position: typing.SupportsFloat | typing.SupportsIndex) -> Annotation:
        """
        行内の絶対位置: start の文字を行頭から position（pt）から始める
        """
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
    def offset(self) -> float:
        """
        ルビ・圏点と親文字の間隔（親文字の em）
        """
    @offset.setter
    def offset(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
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
class BlockAlign:
    """
    行送り方向の揃え（箱の中での段落の位置）
    
    Members:
    
      START
    
      CENTER
    
      END
    """
    CENTER: typing.ClassVar[BlockAlign]  # value = <BlockAlign.CENTER: 1>
    END: typing.ClassVar[BlockAlign]  # value = <BlockAlign.END: 2>
    START: typing.ClassVar[BlockAlign]  # value = <BlockAlign.START: 0>
    __members__: typing.ClassVar[dict[str, BlockAlign]]  # value = {'START': <BlockAlign.START: 0>, 'CENTER': <BlockAlign.CENTER: 1>, 'END': <BlockAlign.END: 2>}
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
class BlockStyle:
    """
    ブロックの前後アキ・改ページ制御（orphans / widows / keep_with_next / keep_together / break_before / break_after）・段抜き・ラベル・背景・余白
    """
    background: jtypeset._jtypeset.Color | None
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
    def hyphen_min_left(self) -> int:
        ...
    @hyphen_min_left.setter
    def hyphen_min_left(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def hyphen_min_right(self) -> int:
        ...
    @hyphen_min_right.setter
    def hyphen_min_right(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def hyphen_penalty(self) -> float:
        ...
    @hyphen_penalty.setter
    def hyphen_penalty(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def hyphenation(self) -> HyphenationDictionary:
        """
        欧文のハイフネーション辞書（HyphenationDictionary）。所有しないので、組版の間は生かしておくこと
        """
    @hyphenation.setter
    def hyphenation(self, arg0: HyphenationDictionary) -> None:
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
    @property
    def wrap(self) -> WrapMode:
        """
        折返しの方式（WrapMode）
        """
    @wrap.setter
    def wrap(self, arg0: WrapMode) -> None:
        ...
class CharInfo:
    """
    組んだあとの文字 1 つ（位置・大きさ・スタイル番号・グリフ・物理矩形）
    """
    def __repr__(self) -> str:
        ...
    @property
    def block_max(self) -> float:
        ...
    @property
    def block_min(self) -> float:
        """
        箱の block 範囲（行の中心線から）
        """
    @property
    def char_index(self) -> int:
        """
        元テキストでの位置（UTF-16）
        """
    @property
    def face_key(self) -> str:
        """
        FontSet のキー
        """
    @property
    def gid(self) -> int:
        ...
    @property
    def image(self) -> bool:
        ...
    @property
    def inline_end(self) -> float:
        ...
    @property
    def inline_start(self) -> float:
        """
        箱の始端（行頭から、pt）
        """
    @property
    def line_index(self) -> int:
        ...
    @property
    def object(self) -> bool:
        ...
    @property
    def placeholder(self) -> bool:
        ...
    @property
    def rect(self) -> Rect:
        """
        物理矩形（origin を渡して組んだとき）
        """
    @property
    def size(self) -> float:
        ...
    @property
    def style_index(self) -> int:
        """
        Paragraph の run の番号
        """
class CodepointRange:
    """
    コードポイントの範囲（両端含む）
    """
    hi: str
    lo: str
    def __init__(self, lo: typing.SupportsInt | typing.SupportsIndex, hi: typing.SupportsInt | typing.SupportsIndex) -> None:
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
class Direction:
    """
    段落の基底方向（AUTO は最初の強い文字で決める）
    
    Members:
    
      AUTO
    
      LTR
    
      RTL
    """
    AUTO: typing.ClassVar[Direction]  # value = <Direction.AUTO: 0>
    LTR: typing.ClassVar[Direction]  # value = <Direction.LTR: 1>
    RTL: typing.ClassVar[Direction]  # value = <Direction.RTL: 2>
    __members__: typing.ClassVar[dict[str, Direction]]  # value = {'AUTO': <Direction.AUTO: 0>, 'LTR': <Direction.LTR: 1>, 'RTL': <Direction.RTL: 2>}
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
class EmojiPresentation:
    """
    絵文字の表示形式: AUTO（異体字セレクタに従う）/ TEXT（モノクロの字形）/ EMOJI（カラー）
    
    Members:
    
      AUTO
    
      TEXT
    
      EMOJI
    """
    AUTO: typing.ClassVar[EmojiPresentation]  # value = <EmojiPresentation.AUTO: 0>
    EMOJI: typing.ClassVar[EmojiPresentation]  # value = <EmojiPresentation.EMOJI: 2>
    TEXT: typing.ClassVar[EmojiPresentation]  # value = <EmojiPresentation.TEXT: 1>
    __members__: typing.ClassVar[dict[str, EmojiPresentation]]  # value = {'AUTO': <EmojiPresentation.AUTO: 0>, 'TEXT': <EmojiPresentation.TEXT: 1>, 'EMOJI': <EmojiPresentation.EMOJI: 2>}
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
    def add_heading(self, paragraph: Paragraph, level: typing.SupportsInt | typing.SupportsIndex = 1, style: jtypeset._jtypeset.BlockStyle | None = None, numbered: bool = False) -> None:
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
class FontDeclaration:
    """
    開かずに登録するフォントのメタデータ（キー・ファイル・family 別名・weight・italic・languages・ranges）
    """
    key: str
    path: str
    def __init__(self) -> None:
        ...
    @property
    def face_index(self) -> int:
        ...
    @face_index.setter
    def face_index(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def family(self) -> list[str]:
        ...
    @family.setter
    def family(self, arg0: collections.abc.Sequence[str]) -> None:
        ...
    @property
    def italic(self) -> bool | None:
        """
        None でフォントから
        """
    @italic.setter
    def italic(self, arg0: bool | None) -> None:
        ...
    @property
    def languages(self) -> list[str]:
        """
        BCP47。この言語のテキストで先に試される
        """
    @languages.setter
    def languages(self, arg0: collections.abc.Sequence[str]) -> None:
        ...
    @property
    def ranges(self) -> list[CodepointRange]:
        """
        カバレッジ（空なら開いて cmap を見る）
        """
    @ranges.setter
    def ranges(self, arg0: collections.abc.Sequence[CodepointRange]) -> None:
        ...
    @property
    def weight(self) -> int:
        """
        100〜900。0 でフォントから
        """
    @weight.setter
    def weight(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class FontSet:
    """
    フォントの集合。load_file / load_bytes で開くか declare で宣言（初回使用時に開く）し、TextStyle の family（キーまたは family 名）で引く。同じ family の複数 face から weight / italic の最近傍を選び、文字が無ければ次の family へフォールバックする。set_language_fonts で言語ごとに先に試す family を指定できる
    """
    def __init__(self) -> None:
        ...
    @typing.overload
    def declare(self, path: str, key: str = '', family: collections.abc.Sequence[str] = [], weight: typing.SupportsInt | typing.SupportsIndex = 0, italic: bool | None = None, languages: collections.abc.Sequence[str] = [], ranges: collections.abc.Sequence[CodepointRange] = [], face_index: typing.SupportsInt | typing.SupportsIndex = 0) -> bool:
        """
        開かずに宣言する（初回使用時に開く）。key を省略するとパスがキー。weight=0 / italic=None はフォントから取る
        """
    @typing.overload
    def declare(self, declaration: FontDeclaration) -> bool:
        ...
    def has(self, key: str) -> bool:
        """
        登録済みか（開いていなくてもよい）
        """
    def is_loaded(self, key: str) -> bool:
        """
        開いているか
        """
    def keys(self) -> list[str]:
        """
        登録したキー（登録順）
        """
    def language_fonts(self, language: str) -> list[str]:
        ...
    def load_bytes(self, key: str, data: bytes, face_index: typing.SupportsInt | typing.SupportsIndex = 0) -> bool:
        ...
    def load_file(self, path: str, key: str = '', face_index: typing.SupportsInt | typing.SupportsIndex = 0) -> bool:
        """
        フォントファイルを開く。key を省略するとパスがキーになる
        """
    def select(self, name: str, weight: typing.SupportsInt | typing.SupportsIndex = 400, italic: bool = False) -> str | None:
        """
        キー／family 名と weight / italic に最も近い face のキー（無ければ None）。必要なら開く
        """
    def set_language_fonts(self, language: str, families: collections.abc.Sequence[str]) -> None:
        """
        この言語のテキストで先に試す family 列（空で削除）。"zh-Hans" の完全一致が無ければ "zh" を使う
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
    def variations(self) -> dict[str, float]:
        """
        バリアブルフォントの軸の値（{'wght': 700, 'wdth': 75}）。wght が無ければ weight が入る。辞書はコピーを返すので作って代入する
        """
    @variations.setter
    def variations(self, arg0: collections.abc.Mapping[str, typing.SupportsFloat | typing.SupportsIndex]) -> None:
        ...
    @property
    def weight(self) -> int:
        ...
    @weight.setter
    def weight(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class GradientStop:
    """
    グラデーションの停止点（0〜1 の位置と色）
    """
    color: Color
    def __init__(self, offset: typing.SupportsFloat | typing.SupportsIndex, color: Color) -> None:
        ...
    @property
    def offset(self) -> float:
        ...
    @offset.setter
    def offset(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class HitResult:
    """
    hit_test の結果
    """
    @property
    def after(self) -> bool:
        """
        箱の後半か（キャレットを次の文字の前に置く判断用）
        """
    @property
    def char_index(self) -> int:
        """
        当たった文字（行の後ろの余白なら行末）
        """
    @property
    def inside(self) -> bool:
        """
        文字の箱の中か（False なら行の端に丸めた）
        """
    @property
    def line_index(self) -> int:
        ...
class HyphenationDictionary:
    """
    言語ごとのハイフネーションのパターン。TextStyle.language で引く
    """
    def __init__(self) -> None:
        ...
    def empty(self) -> bool:
        ...
    def find(self, language: str) -> Hyphenator:
        ...
    def for_language(self, language: str) -> Hyphenator:
        """
        その言語の Hyphenator（無ければ作る）
        """
class Hyphenator:
    """
    欧文のハイフネーション（Liang のパターン）。TeX の hyph-*.tex を読ませて使う
    """
    def __init__(self) -> None:
        ...
    def add_exception(self, word: str) -> None:
        """
        as-so-ciate のような例外を足す
        """
    def add_pattern_file(self, path: str) -> int:
        ...
    def add_patterns(self, text: str) -> int:
        """
        TeX のパターン（patterns{} / hyphenation{}）または 1 行 1 パターンの素のリストを読む
        """
    def empty(self) -> bool:
        ...
    def hyphenate(self, word: str, min_left: typing.SupportsInt | typing.SupportsIndex = 2, min_right: typing.SupportsInt | typing.SupportsIndex = 3) -> list[int]:
        """
        分割位置（先頭から k 文字目の後ろで切ってよい k の列）
        """
    def pattern_count(self) -> int:
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
    caption: jtypeset._jtypeset.Paragraph | None
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
    group_style: jtypeset._jtypeset.TextStyle | None
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
class KinsokuLevel:
    """
    禁則の強さ: STRICT（JLReq のまま）/ NORMAL（小書きの仮名・長音・繰返し記号・ハイフン類を弱い禁則に）/ LOOSE（句読点・終わり括弧・中点・区切り約物も弱い禁則に）
    
    Members:
    
      STRICT
    
      NORMAL
    
      LOOSE
    """
    LOOSE: typing.ClassVar[KinsokuLevel]  # value = <KinsokuLevel.LOOSE: 2>
    NORMAL: typing.ClassVar[KinsokuLevel]  # value = <KinsokuLevel.NORMAL: 1>
    STRICT: typing.ClassVar[KinsokuLevel]  # value = <KinsokuLevel.STRICT: 0>
    __members__: typing.ClassVar[dict[str, KinsokuLevel]]  # value = {'STRICT': <KinsokuLevel.STRICT: 0>, 'NORMAL': <KinsokuLevel.NORMAL: 1>, 'LOOSE': <KinsokuLevel.LOOSE: 2>}
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
    caption: jtypeset._jtypeset.Paragraph | None
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
    def add_microtex(self, name: str, fonts: FontSet, text_family: str = 'serif', sans_family: str = 'sans', res_dir: str = '') -> None:
        """
        MicroTeX（LaTeX 数式）をハンドラとして登録する。res_dir は数式フォントの場所（省略時はパッケージ同梱の microtex_res。jtypeset.microtex_res_dir()）。\\text{} などは text_family / sans_family の FontSet キーで組む
        """
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
    footer: jtypeset._jtypeset.RunningText | None
    header: jtypeset._jtypeset.RunningText | None
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
class Paint:
    """
    塗り — 単色または線形／放射グラデーション。Color を渡せる所にはそのまま渡せる（単色）
    """
    color: Color
    end: Point
    kind: PaintKind
    start: Point
    units: PaintUnits
    @staticmethod
    def linear(start: Point, end: Point, stops: collections.abc.Sequence[GradientStop], units: PaintUnits = ...) -> Paint:
        """
        線形グラデーション
        """
    @staticmethod
    def radial(center: Point, radius: typing.SupportsFloat | typing.SupportsIndex, stops: collections.abc.Sequence[GradientStop], units: PaintUnits = ...) -> Paint:
        """
        放射グラデーション
        """
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, color: Color) -> None:
        ...
    def is_gradient(self) -> bool:
        ...
    def solid(self) -> Color:
        """
        単色として扱うときの色
        """
    @property
    def radius(self) -> float:
        ...
    @radius.setter
    def radius(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def stops(self) -> list[GradientStop]:
        """
        停止点のリスト（コピーを返すので作って代入する）
        """
    @stops.setter
    def stops(self, arg0: collections.abc.Sequence[GradientStop]) -> None:
        ...
class PaintKind:
    """
    塗りの種類
    
    Members:
    
      SOLID
    
      LINEAR
    
      RADIAL
    """
    LINEAR: typing.ClassVar[PaintKind]  # value = <PaintKind.LINEAR: 1>
    RADIAL: typing.ClassVar[PaintKind]  # value = <PaintKind.RADIAL: 2>
    SOLID: typing.ClassVar[PaintKind]  # value = <PaintKind.SOLID: 0>
    __members__: typing.ClassVar[dict[str, PaintKind]]  # value = {'SOLID': <PaintKind.SOLID: 0>, 'LINEAR': <PaintKind.LINEAR: 1>, 'RADIAL': <PaintKind.RADIAL: 2>}
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
class PaintUnits:
    """
    グラデーションの座標系: BOUNDING_BOX（対象の外接矩形を 0〜1 に正規化）/ USER_SPACE（ページ座標 pt）
    
    Members:
    
      BOUNDING_BOX
    
      USER_SPACE
    """
    BOUNDING_BOX: typing.ClassVar[PaintUnits]  # value = <PaintUnits.BOUNDING_BOX: 0>
    USER_SPACE: typing.ClassVar[PaintUnits]  # value = <PaintUnits.USER_SPACE: 1>
    __members__: typing.ClassVar[dict[str, PaintUnits]]  # value = {'BOUNDING_BOX': <PaintUnits.BOUNDING_BOX: 0>, 'USER_SPACE': <PaintUnits.USER_SPACE: 1>}
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
class Paragraph:
    """
    段落: run の列＋注記＋段落スタイル。add_run / add_image / add_object / add_footnote / annotate で組み立てる
    """
    style: ParagraphStyle
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, text: str, style: TextStyle, paragraph_style: jtypeset._jtypeset.ParagraphStyle | None = None) -> None:
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
    def add_placeholder(self, size: Size, style: TextStyle, id: str = '') -> None:
        """
        行内プレースホルダ（描かない空箱、本文中の位置は 1 文字ぶん）を足す。位置は ParagraphLayout.placeholder_rects で取る
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
class ParagraphLayout:
    """
    layout_paragraph の結果。行の列（len / 添字 / 反復で LineInfo）と、組んだあとの取り出し口。origin は 1 行目の行頭（横組み: 左端 x と 1 行目の中心線 y、縦組み: 1 列目の中心線 x と上端 y）
    """
    def __getitem__(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> LineInfo:
        ...
    def __iter__(self) -> collections.abc.Iterator:
        ...
    def __len__(self) -> int:
        ...
    def __repr__(self) -> str:
        ...
    def caret_rect(self, char_index: typing.SupportsInt | typing.SupportsIndex, origin: Point = ..., line_offset: typing.SupportsInt | typing.SupportsIndex = 0, thickness: typing.SupportsFloat | typing.SupportsIndex = 1.0) -> jtypeset._jtypeset.Rect | None:
        """
        キャレット矩形（文字の始端。行末なら終端）。範囲外は None
        """
    def char_boxes(self, line: typing.SupportsInt | typing.SupportsIndex, origin: Point = ..., line_offset: typing.SupportsInt | typing.SupportsIndex = 0) -> list[CharInfo]:
        """
        行 line の文字の箱（送り方向の順。ルビ等の注記は含まない）
        """
    def hit_test(self, point: Point, origin: Point = ..., line_offset: typing.SupportsInt | typing.SupportsIndex = 0) -> jtypeset._jtypeset.HitResult | None:
        """
        点 → 文字。行送りの箱の外なら None
        """
    def line_origin(self, line: typing.SupportsInt | typing.SupportsIndex, origin: Point = ..., line_offset: typing.SupportsInt | typing.SupportsIndex = 0) -> Point:
        """
        行 line の行頭（物理）
        """
    def origin_in_box(self, box: Rect, block_align: BlockAlign = ..., align: Align = ...) -> Point:
        """
        箱の中で天地（block_align）・左右（align）に揃えた origin を返す
        """
    def placeholder_rects(self, origin: Point = ..., line_offset: typing.SupportsInt | typing.SupportsIndex = 0) -> list[PlaceholderRect]:
        ...
    def rects_for(self, char_start: typing.SupportsInt | typing.SupportsIndex, char_end: typing.SupportsInt | typing.SupportsIndex, origin: Point = ..., line_offset: typing.SupportsInt | typing.SupportsIndex = 0) -> list[Rect]:
        """
        文字範囲 [char_start, char_end) を覆う矩形（行ごとに 1 つ）。リンク・選択範囲用
        """
    def render(self, size: Size, origin: Point, dpi: typing.SupportsFloat | typing.SupportsIndex = 144.0, background: Color = ..., line_offset: typing.SupportsInt | typing.SupportsIndex = 0, max_chars: typing.SupportsInt | typing.SupportsIndex = 18446744073709551615) -> tuple[int, int, bytes]:
        """
        size（pt）の面に origin から描いて (width, height, ARGB8888 bytes) を返す。max_chars で途中まで（段階表示）
        """
    def save_png(self, path: str, size: Size, origin: Point, dpi: typing.SupportsFloat | typing.SupportsIndex = 144.0, background: Color = ..., line_offset: typing.SupportsInt | typing.SupportsIndex = 0, max_chars: typing.SupportsInt | typing.SupportsIndex = 18446744073709551615) -> bool:
        ...
    @property
    def block_extent(self) -> float:
        """
        全行が占める行送り方向の量（pt）
        """
    @property
    def char_end(self) -> int:
        ...
    @property
    def complete(self) -> bool:
        ...
    @property
    def fits(self) -> bool:
        """
        fit_paragraph が行数上限に収められたか
        """
    @property
    def line_pitch(self) -> float:
        ...
    @property
    def lines(self) -> list[LineInfo]:
        ...
    @property
    def scale(self) -> float:
        """
        fit_paragraph で縮めた倍率（1.0 なら縮めていない）
        """
    @property
    def writing_mode(self) -> WritingMode:
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
    def direction(self) -> Direction:
        """
        基底方向（Direction）。RTL では Start / End が入れ替わる
        """
    @direction.setter
    def direction(self, arg0: Direction) -> None:
        ...
    @property
    def ellipsis(self) -> str:
        """
        行数上限で切れたときに末尾へ置く省略記号（"…"）
        """
    @ellipsis.setter
    def ellipsis(self, arg0: str) -> None:
        ...
    @property
    def first_line_indent(self) -> float:
        ...
    @first_line_indent.setter
    def first_line_indent(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def hanging_indent(self) -> float:
        """
        2 行目以降の字下げ（em）
        """
    @hanging_indent.setter
    def hanging_indent(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
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
    def rotation(self) -> float:
        """
        段落全体の回転（度。行頭を中心に時計回り）
        """
    @rotation.setter
    def rotation(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def tab_stops(self) -> list[TabStop]:
        """
        タブストップ（TabStop のリスト）。空なら tab_width × em ごと。リストはコピーを返すので作って代入する
        """
    @tab_stops.setter
    def tab_stops(self, arg0: collections.abc.Sequence[TabStop]) -> None:
        ...
    @property
    def tab_width(self) -> int:
        ...
    @tab_width.setter
    def tab_width(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class PlaceholderRect:
    """
    プレースホルダの位置（id・文字位置・物理矩形）
    """
    @property
    def char_index(self) -> int:
        ...
    @property
    def id(self) -> str:
        ...
    @property
    def rect(self) -> Rect:
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
    @property
    def kinsoku(self) -> KinsokuLevel:
        """
        禁則の強さ（KinsokuLevel）
        """
    @kinsoku.setter
    def kinsoku(self, arg0: KinsokuLevel) -> None:
        ...
    @property
    def line_end_allowed(self) -> str:
        """
        行末禁則から外す文字
        """
    @line_end_allowed.setter
    def line_end_allowed(self, arg0: str) -> None:
        ...
    @property
    def line_end_prohibited(self) -> str:
        """
        行末に置かない文字を足す
        """
    @line_end_prohibited.setter
    def line_end_prohibited(self, arg0: str) -> None:
        ...
    @property
    def line_start_allowed(self) -> str:
        """
        行頭禁則から外す文字
        """
    @line_start_allowed.setter
    def line_start_allowed(self, arg0: str) -> None:
        ...
    @property
    def line_start_prohibited(self) -> str:
        """
        行頭に置かない文字を足す
        """
    @line_start_prohibited.setter
    def line_start_prohibited(self, arg0: str) -> None:
        ...
    @property
    def weak_kinsoku_penalty(self) -> float:
        """
        弱い禁則のペナルティ
        """
    @weak_kinsoku_penalty.setter
    def weak_kinsoku_penalty(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class Stroke:
    """
    線の描き方（色・幅・端・角）
    """
    color: Paint
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, color: Paint, width: typing.SupportsFloat | typing.SupportsIndex = 1.0) -> None:
        ...
    @property
    def width(self) -> float:
        ...
    @width.setter
    def width(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class TabAlign:
    """
    タブストップの揃え
    
    Members:
    
      LEFT
    
      CENTER
    
      RIGHT
    
      DECIMAL
    """
    CENTER: typing.ClassVar[TabAlign]  # value = <TabAlign.CENTER: 1>
    DECIMAL: typing.ClassVar[TabAlign]  # value = <TabAlign.DECIMAL: 3>
    LEFT: typing.ClassVar[TabAlign]  # value = <TabAlign.LEFT: 0>
    RIGHT: typing.ClassVar[TabAlign]  # value = <TabAlign.RIGHT: 2>
    __members__: typing.ClassVar[dict[str, TabAlign]]  # value = {'LEFT': <TabAlign.LEFT: 0>, 'CENTER': <TabAlign.CENTER: 1>, 'RIGHT': <TabAlign.RIGHT: 2>, 'DECIMAL': <TabAlign.DECIMAL: 3>}
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
class TabStop:
    """
    タブストップ（行頭からの位置 pt・揃え・小数点揃えの文字）
    """
    align: TabAlign
    decimal_char: str
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, position: typing.SupportsFloat | typing.SupportsIndex, align: TabAlign = ..., decimal_char: str = '.') -> None:
        ...
    @property
    def position(self) -> float:
        ...
    @position.setter
    def position(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class TableBlock:
    """
    表。列幅は固定／自動、colspan / rowspan、ヘッダの繰り返し、段より高い行の分割、caption の {table} は表番号
    """
    align: Align
    block: BlockStyle
    borders: TableBorders
    caption: jtypeset._jtypeset.Paragraph | None
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
class TagLink:
    """
    リンクの範囲（名前と本文の文字範囲）
    """
    @property
    def end(self) -> int:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def start(self) -> int:
        ...
class TagMarker:
    """
    タイミング等のマーカー（種類・値・本文での位置）
    """
    @property
    def char_index(self) -> int:
        ...
    @property
    def kind(self) -> str:
        ...
    @property
    def value(self) -> str:
        ...
class TagParseOptions:
    """
    タグ記法の解釈の設定（基準スタイル・段落スタイル・名前付きスタイル／family・置換）
    """
    base_style: TextStyle
    keep_unknown_tags: bool
    paragraph_style: ParagraphStyle
    def __init__(self) -> None:
        ...
    @property
    def evaluate(self) -> collections.abc.Callable[[str], str]:
        """
        <eval name="…"> の置換（名前 → 文字列）
        """
    @evaluate.setter
    def evaluate(self, arg0: collections.abc.Callable[[str], str]) -> None:
        ...
    @property
    def graph_default_size(self) -> float:
        ...
    @graph_default_size.setter
    def graph_default_size(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def named_families(self) -> dict[str, list[str]]:
        """
        <font face="…"> の family 列
        """
    @named_families.setter
    def named_families(self, arg0: collections.abc.Mapping[str, collections.abc.Sequence[str]]) -> None:
        ...
    @property
    def named_styles(self) -> dict[str, TextStyle]:
        """
        <style name="…"> で引くスタイル
        """
    @named_styles.setter
    def named_styles(self, arg0: collections.abc.Mapping[str, TextStyle]) -> None:
        ...
    @property
    def sub_offset(self) -> float:
        ...
    @sub_offset.setter
    def sub_offset(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def sup_offset(self) -> float:
        ...
    @sup_offset.setter
    def sup_offset(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def sup_scale(self) -> float:
        ...
    @sup_scale.setter
    def sup_scale(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class TagParseResult:
    """
    parse_tagged_text の結果（段落・マーカー・リンク・プレースホルダ・エラー）
    """
    @property
    def errors(self) -> list[str]:
        ...
    @property
    def links(self) -> list[TagLink]:
        ...
    @property
    def markers(self) -> list[TagMarker]:
        ...
    @property
    def paragraph(self) -> Paragraph:
        ...
    @property
    def placeholders(self) -> list[TagPlaceholder]:
        ...
class TagPlaceholder:
    """
    <graph> で置いた行内プレースホルダ
    """
    @property
    def char_index(self) -> int:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def size(self) -> Size:
        ...
class TextDecoration:
    """
    下線・打消し線。色（無ければ fill）・太さ（0 でフォントのメトリクス）・位置の補正（em、文字から離れる向きが正）
    """
    color: jtypeset._jtypeset.Paint | None
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, color: jtypeset._jtypeset.Color | None = None, thickness: typing.SupportsFloat | typing.SupportsIndex = 0.0, offset: typing.SupportsFloat | typing.SupportsIndex = 0.0) -> None:
        ...
    @property
    def offset(self) -> float:
        ...
    @offset.setter
    def offset(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def thickness(self) -> float:
        ...
    @thickness.setter
    def thickness(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class TextLayer:
    """
    文字の外観の 1 層（塗り・縁取り・ずらし・ぼかし）。TextStyle.layers に下から上の順で並べる
    """
    fill: jtypeset._jtypeset.Paint | None
    stroke: jtypeset._jtypeset.Stroke | None
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, fill: jtypeset._jtypeset.Color | None = None, stroke: jtypeset._jtypeset.Stroke | None = None, offset: Point = ..., blur: typing.SupportsFloat | typing.SupportsIndex = 0.0) -> None:
        ...
    @property
    def blur(self) -> float:
        """
        ぼかし半径（pt）。ラスタと SVG のみ
        """
    @blur.setter
    def blur(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def offset(self) -> Point:
        """
        ずらし（pt、右・下が正）
        """
    @offset.setter
    def offset(self, arg0: Point) -> None:
        ...
class TextMetrics:
    """
    measure_text の結果
    """
    @property
    def advance(self) -> float:
        """
        送り方向の長さ（pt）
        """
    @property
    def ascent(self) -> float:
        """
        中心線から注記側（横組み: 上）の張り出し
        """
    @property
    def cluster_count(self) -> int:
        ...
    @property
    def descent(self) -> float:
        ...
    @property
    def glyph_count(self) -> int:
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
class TextShadow:
    """
    影（色・ずらし・ぼかし半径）。層の一番下に置かれる
    """
    color: Color
    offset: Point
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, color: Color = ..., offset: Point = ..., blur: typing.SupportsFloat | typing.SupportsIndex = 0.0) -> None:
        ...
    @property
    def blur(self) -> float:
        ...
    @blur.setter
    def blur(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
class TextStyle:
    """
    文字スタイル: フォント・サイズ・色・縁取り・影・層・下線・打消し線・字間・向き・平体長体・合成ボールド／斜体・ベースラインのずらし
    """
    fake_bold: bool
    fake_italic: bool
    fill: Paint
    font: FontSpec
    language: str
    orientation: jtypeset._jtypeset.TextOrientation | None
    stroke: jtypeset._jtypeset.Stroke | None
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, family: collections.abc.Sequence[str], size: typing.SupportsFloat | typing.SupportsIndex = 10.0, fill: Paint = ...) -> None:
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
    def emoji_presentation(self) -> EmojiPresentation:
        """
        絵文字の表示形式（EmojiPresentation）
        """
    @emoji_presentation.setter
    def emoji_presentation(self, arg0: EmojiPresentation) -> None:
        ...
    @property
    def features(self) -> list[str]:
        """
        OpenType feature（['palt', '-liga', 'ss01'] など HarfBuzz の書式）。palt 等の字幅を変える feature を付けた文字は JLReq の約物の詰めを使わない
        """
    @features.setter
    def features(self, arg0: collections.abc.Sequence[str]) -> None:
        ...
    @property
    def layers(self) -> list[TextLayer]:
        """
        外観の層（TextLayer のリスト、下から上）。空なら fill / stroke の 1 層。属性はコピーを返すので、リストを作って代入する
        """
    @layers.setter
    def layers(self, arg0: collections.abc.Sequence[TextLayer]) -> None:
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
    def shadow(self) -> jtypeset._jtypeset.TextShadow | None:
        """
        影（TextShadow）。層の一番下に足す
        """
    @shadow.setter
    def shadow(self, arg0: jtypeset._jtypeset.TextShadow | None) -> None:
        ...
    @property
    def size(self) -> float:
        ...
    @size.setter
    def size(self, arg0: typing.SupportsFloat | typing.SupportsIndex) -> None:
        ...
    @property
    def strikethrough(self) -> jtypeset._jtypeset.TextDecoration | None:
        """
        打消し線（TextDecoration）
        """
    @strikethrough.setter
    def strikethrough(self, arg0: jtypeset._jtypeset.TextDecoration | None) -> None:
        ...
    @property
    def underline(self) -> jtypeset._jtypeset.TextDecoration | None:
        """
        下線（TextDecoration）。縦組みでは右側の傍線
        """
    @underline.setter
    def underline(self, arg0: jtypeset._jtypeset.TextDecoration | None) -> None:
        ...
class TocBlock:
    """
    目次。見出し（採番済み）とページ番号を前のパスから集めて並べる
    """
    block: BlockStyle
    leader: bool
    style: TextStyle
    sub_style: jtypeset._jtypeset.TextStyle | None
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
class WrapMode:
    """
    折返しの方式: MIXED（和文は字ごと・欧文は語ごと）/ CHAR（欧文の語中でも切る）/ WORD（和文も語でだけ切る）/ NONE（折り返さない）
    
    Members:
    
      MIXED
    
      CHAR
    
      WORD
    
      NONE
    """
    CHAR: typing.ClassVar[WrapMode]  # value = <WrapMode.CHAR: 1>
    MIXED: typing.ClassVar[WrapMode]  # value = <WrapMode.MIXED: 0>
    NONE: typing.ClassVar[WrapMode]  # value = <WrapMode.NONE: 3>
    WORD: typing.ClassVar[WrapMode]  # value = <WrapMode.WORD: 2>
    __members__: typing.ClassVar[dict[str, WrapMode]]  # value = {'MIXED': <WrapMode.MIXED: 0>, 'CHAR': <WrapMode.CHAR: 1>, 'WORD': <WrapMode.WORD: 2>, 'NONE': <WrapMode.NONE: 3>}
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
def fit_paragraph(fonts: FontSet, paragraph: Paragraph, writing_mode: WritingMode, max_lines: typing.SupportsInt | typing.SupportsIndex, line_lengths: collections.abc.Sequence[typing.SupportsFloat | typing.SupportsIndex] = [], default_length: typing.SupportsFloat | typing.SupportsIndex = 200.0, min_scale: typing.SupportsFloat | typing.SupportsIndex = 0.5, step: typing.SupportsFloat | typing.SupportsIndex = 0.05000000074505806) -> ParagraphLayout:
    """
    行数上限に収まるまで文字サイズを段階的に縮めて組む（吹き出しのフィット）。ParagraphLayout の scale / fits を見る
    """
def image_from_rgba(width: typing.SupportsInt | typing.SupportsIndex, height: typing.SupportsInt | typing.SupportsIndex, rgba: bytes) -> Image:
    ...
def layout_paragraph(fonts: FontSet, paragraph: Paragraph, writing_mode: WritingMode, line_lengths: collections.abc.Sequence[typing.SupportsFloat | typing.SupportsIndex] = [], default_length: typing.SupportsFloat | typing.SupportsIndex = 200.0) -> ParagraphLayout:
    """
    段落を組んで ParagraphLayout（行ごとの文字範囲と長さ＋取り出し口）を返す（行長は行ごとに指定できる = \\parshape）
    """
def load_image(path: str) -> Image:
    """
    PNG / JPEG / BMP / GIF を読む
    """
def measure_text(fonts: FontSet, text: str, style: TextStyle, writing_mode: WritingMode = ...) -> TextMetrics:
    """
    折り返さない 1 行の計測（送り・張り出し・クラスタ数）
    """
def parse_tagged_text(text: str, options: TagParseOptions) -> TagParseResult:
    """
    タグ付きテキスト（richtext 互換）を段落にする。<b> <i> <u> <s> <sup> <sub> <font> <color> <outline> <shadow> <style> <ruby> <emphasis> <tcy> <warichu> <jidori> <link> <graph> <br> <sp> <eval> とタイミング系（<start> <delay> <wait> <sync> <keywait>）
    """
def save_pdf(pages: collections.abc.Sequence[Page], path: str, title: str = '', author: str = '', subset_fonts: bool = True, compress: bool = True) -> tuple[bool, list[str]]:
    """
    ページ列を 1 つの PDF に書く。(ok, warnings) を返す
    """
def strip_tags(text: str) -> str:
    """
    タグを取り除いた本文を返す
    """
def superscript_style(style: TextStyle) -> TextStyle:
    """
    上付き（脚注記号・指数用）のスタイルを作る
    """
CM: float = 28.346458435058594
HAS_MICROTEX: bool = True
INCH: float = 72.0
MM: float = 2.8346457481384277
