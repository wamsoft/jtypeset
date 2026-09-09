# jtypeset Python ガイド

C++ コアの pybind11 バインディング `jtypeset`（拡張モジュールは `jtypeset._jtypeset`）と、Markdown → PDF の `jtypeset.md` の使い方。
API の一覧（クラス・メソッド・引数）は `make pydocs` で生成する HTML（`build/docs/python/index.html`）と
型スタブ `python/jtypeset/_jtypeset/__init__.pyi`（pybind11-stubgen で生成。IDE の補完にも使われる）を参照。C++ の概念・座標系の説明は `docs/cpp_guide.md`。

## インストール

```bash
pip install dist/jtypeset-*.whl            # ビルド済み wheel（pip wheel . -w dist --no-deps で作る。VCPKG_ROOT が要る）
pip install "jtypeset[md]"                 # Markdown → PDF（markdown-it-py / mdit-py-plugins / PyYAML）
pip install "jtypeset[md,highlight]"       # + Pygments（コードブロックの色付け。無くても単色で組める）
```

開発ツリーから使うときは `PYTHONPATH=build/x64-windows/python/Release`（`jtypeset/` パッケージがここにまとまる）。

## 最小の例

```python
import jtypeset as ts

fonts = ts.FontSet()
fonts.load_file("data/NotoSerifJP-Regular.otf", "serif-ja")     # キーで引く。省略するとパスがキー

body = ts.TextStyle(["serif-ja"], 10.5)                          # family 列とサイズ（pt）
seq = ts.PageSequence()
seq.master.size = ts.paper.A5
seq.master.writing_mode = ts.WritingMode.VERTICAL_RL             # 縦組み
seq.master.margin = ts.Margins(22 * ts.MM, 18 * ts.MM, 18 * ts.MM, 14 * ts.MM)

flow = ts.Flow()
p = ts.Paragraph("吾輩は猫である。名前はまだ無い。", body)
p.annotate(ts.Annotation.ruby(0, 2, "わがはい"))                 # 範囲は UTF-16 位置
flow.add_paragraph(p)

pages = ts.FlowLayouter(fonts).layout(flow, seq)
ok, warnings = ts.save_pdf(pages, "out.pdf", title="猫")
pages[0].save_png("out_p1.png", dpi=144)
pages[0].save_svg("out_p1.svg")
```

## 概念

| 型 | 役割 |
|---|---|
| `FontSet` | フォントを開く（`load_file` / `load_bytes`）か宣言する（`declare(path, key, family, weight, italic, languages, ranges, face_index)`: 初回使用時に開く）。`has` / `is_loaded` / `keys` / `find` / `select` / `language_fonts` / `size` で状態を見られる。キー／family 名で引き、同じ family の複数 face から weight / italic の最近傍を選ぶ。文字が無ければ次の family へフォールバック。`set_language_fonts("zh", ["sans-sc"])` で言語ごとに先に試す family |
| `TextStyle` / `ParagraphStyle` | 文字（フォント・サイズ・色・縁取り・影 `shadow`・層 `layers`・下線 `underline`・打消し線 `strikethrough`・ベースラインのずらし…）／段落（揃え・行送り・一字下げ・空白保持・タブ幅…） |
| `FontSpec` | family 列・weight・italic・`variations`（バリアブルフォントの軸 `{'wght': 700}`）。`TextStyle.features` は OpenType feature（`['palt']`） |
| `Paint` / `GradientStop` / `PaintKind` / `PaintUnits` | 塗り。`Paint.linear(start, end, stops)` / `Paint.radial(center, radius, stops)`。座標は既定で対象の外接矩形の 0〜1（`PaintUnits.USER_SPACE` でページ座標）。`Color` を渡せる所にそのまま渡せる |
| `EmojiPresentation` / `Direction` / `TabAlign` / `FontDeclaration` / `CodepointRange` | 絵文字の表示形式・段落の基底方向・タブの揃え・フォントの宣言・コードポイント範囲 |
| `TextShadow` / `TextLayer` / `TextDecoration` | 影（色・ずらし・ぼかし）／外観の 1 層（塗り・縁取り・ずらし・ぼかし。`layers` に下から上の順）／下線・打消し線（色・太さ・位置の補正） |
| `Paragraph` | run の列＋注記。`add_run(text, style, literal=False)`、`add_image`、`add_object`（外部オブジェクト）、`add_placeholder(size, style, id)`（描かない空箱。位置は組んだあと取る）、`add_footnote`、`annotate` |
| `ParagraphLayout` | `layout_paragraph()` の結果。行の列（`lines` / 添字アクセス / `len()`）に加え、`char_boxes(line, origin)`（文字ごとの位置・スタイル・グリフ）、`rects_for(start, end, origin)`（文字範囲 → 矩形）、`placeholder_rects(origin)`、`hit_test(point, origin)`、`caret_rect(index, origin)`、`line_origin(line, origin)`、`origin_in_box(box, block_align, align)`、`render` / `save_png(path, size, origin, dpi, max_chars)`（段階表示は `max_chars`）、`line_pitch` / `block_extent` / `complete` / `char_end` |
| `Hyphenator` / `HyphenationDictionary` | 欧文のハイフネーション（TeX の Liang パターン）。`dict.for_language("en").add_pattern_file("hyph-en-us.tex")` で読み、`BreakOptions.hyphenation` に渡す。引くのは `TextStyle.language`、見つからなければ最初に足した言語（`set_default_language`）。パターンが無くてもテキスト中のソフトハイフン U+00AD は常に切れる |
| `parse_tagged_text(text, options)` / `strip_tags(text)` | ゲーム向けのタグ記法（`<b>` `<ruby>` `<color>` `<outline>` `<link>` …）を段落にする。[タグ記法](tags.md) |
| `measure_text(fonts, text, style, writing_mode)` | 折り返さない 1 行の送り・張り出し・クラスタ数 |
| `fit_paragraph(fonts, para, wm, max_lines, ...)` | 行数上限に収まるまで文字サイズを縮めて組む（吹き出しのフィット）。結果の `scale` / `fits` を見る |
| `WrapMode` / `KinsokuLevel` / `TabStop` / `BlockAlign` | 折返しの方式・禁則の強さ・タブストップ・行送り方向の揃え |
| `Annotation` | `ruby(start, end, text, mode, scale)`、`tate_chu_yoko`、`emphasis(..., opposite_side)`、`warichu`、`jidori`、`indent(start, end, em)`（途中からの字下げ）、`move_to(start, position)`（行内の絶対位置）。`offset` でルビ・圏点と親文字の間隔 |
| `Flow` と各 Block | `add_paragraph` / `add_heading(p, level, style, numbered)` / `add_list(ListBlock)` / `add_table(TableBlock)` / `add_image(ImageBlock)` / `add_object(ObjectBlock)` / `add_toc(TocBlock)` / `add_index(IndexBlock)` / `add_labeled(label, body, ...)` / `add_spacer(size)` / `add_rule` / `add_page_break` / `add_column_break` / `add_section(columns, gap)` |
| `BlockStyle` | 前後アキ、orphans / widows、keep_with_next、keep_together、break_before / after、span_columns、label（相互参照）、background / padding |
| `PageSequence` / `PageMaster` | 判型（`ts.paper.A4` … `ts.paper.landscape(size)`）、余白、段数、書字方向、`header` / `footer`（`RunningText`）、duplex |
| `FlowLayouter.layout(...)` | ページ列を返す。`fields`（`{title}` 等）、`figure_format` / `table_format` / `equation_format`、`objects`（ObjectRegistry）、`footnote_marker_format` / `footnote_label_format` / `footnote_per_page`、`balance_last_page`、`draw_guides` |
| `Page` | `number`、`save_png(path, dpi)`、`save_svg(path)`、`to_svg()`。PDF は `save_pdf(pages, path, title, author, subset_fonts, compress)` → `(ok, warnings)` |
| `ObjectRegistry` | 外部オブジェクトのハンドラ。`add(name, fn)`（fn は request dict → SVG 文字列か `{"svg", "baseline"}`）、`add_command(name, cmd)`、`add_microtex(name, fonts, ...)`（同梱の LaTeX 数式）、`has` / `clear_cache` / `cache_size` / `errors` |

本文中の置換: `{page}` `{pages}` `{title}`（柱・ノンブル、`fields` のキー）、`{ref:label}` `{page:label}`（`BlockStyle.label` を付けた
見出し・図・表・式の番号とページ）、キャプションの `{fig}` `{table}` `{eq}`、索引の `{index:よみ|用語}`。
`literal=True` の run は置換しない。

## 表・画像・目次・索引・脚注

```python
t = ts.TableBlock()
t.columns = [ts.TableColumn(), ts.TableColumn()]        # 幅 0 = 自動
head = ts.TableRow(); head.header = True
c1 = ts.TableCell(); c1.paras = [ts.Paragraph("項目", body)]
c2 = ts.TableCell(); c2.paras = [ts.Paragraph("説明", body)]
head.cells = [c1, c2]                                   # 注意: 属性の list はコピーなので、作ってから代入する
t.rows = [head]
t.caption = ts.Paragraph("{table}　見出し", body)
t.block.label = "tab-a"                                 # 本文の {ref:tab-a} で「1」
flow.add_table(t)

img = ts.ImageBlock()
img.image = ts.load_image("figure.png")
img.size = ts.Size(0, 0)                                # 0 なら画素数を 72dpi として使う。段より大きければ縮む
img.caption = ts.Paragraph("{fig}　図の説明", body)
flow.add_image(img)

flow.add_toc(ts.TocBlock(body, max_level=2))            # 前のパスの見出しで作る（多パスは自動）
flow.add_index(ts.IndexBlock(body))                     # 本文の {index:よみ|用語} を集める

p = ts.Paragraph("本文", body)
p.add_footnote(ts.Paragraph("注の本文", small), ts.superscript_style(body))   # 記号は上付き、注は段末
```

pybind の `std::vector` 属性（`rows`, `cells`, `items`, `columns`, `runs` …）は **コピーを返す**ので、
`t.rows.append(...)` は効かない。Python のリストを作ってから `t.rows = rows` と代入する。

## 文字の装飾

```python
body = ts.TextStyle(["serif-ja"], 10.5)

body.underline = ts.TextDecoration()                          # 下線（縦組みでは右側の傍線）
body.strikethrough = ts.TextDecoration(ts.Color(200, 0, 0))   # 打消し線（色つき）
body.shadow = ts.TextShadow(ts.Color(0, 0, 0, 110), ts.Point(0.7, 0.7), 1.2)   # 影（色・ずらし・ぼかし半径 pt）
body.emoji_presentation = ts.EmojiPresentation.TEXT   # 絵文字を字形で（EMOJI でカラー、AUTO は VS15/VS16 に従う）

# 二重縁取り: 層を下から上へ並べる（fill / stroke は層 1 枚の糖衣なので、layers を書くとそちらが優先）
outer = ts.Stroke(ts.Color(30, 60, 160), 1.6)
inner = ts.Stroke(ts.Color(255, 255, 255), 0.8)
body.layers = [ts.TextLayer(stroke=outer), ts.TextLayer(stroke=inner),
               ts.TextLayer(fill=ts.Color(30, 60, 160))]

# グラデーション（既定は対象の外接矩形を 0〜1 に正規化した座標）
body.fill = ts.Paint.linear(ts.Point(0, 0), ts.Point(1, 0), [
    ts.GradientStop(0.0, ts.Color(220, 30, 30)),
    ts.GradientStop(1.0, ts.Color(20, 80, 220))])
```

ぼかしはラスタと SVG で本物のぼかしになり、PDF はずらして置くだけです。グラデーションは 3 つの出力先すべてで出ますが、
PDF は停止点ごとの不透明度を持てないので最大値を全体に掛けます。

## フォントの選び方

```python
fonts = ts.FontSet()
fonts.load_file("NotoSerifJP-Regular.otf", "serif")
fonts.load_file("NotoSerifJP-Bold.otf", "serif-b")         # family 名が同じなので weight 700 で選ばれる
fonts.declare("NotoSansSC-Regular.otf", "sans-sc", languages=["zh"])   # 初回に使うまで開かない

body.font.family = ["Noto Serif JP"]   # キーのほか、フォントの family 名でも引ける
body.font.weight = 700          # 太字の face があればそれ、無ければ合成ボールド
body.font.italic = True         # 同上
body.font.variations = {"wght": 350, "wdth": 87.5}   # バリアブルフォントの軸
body.features = ["palt"]        # OpenType feature（palt を使う run は JLReq の約物の詰めを使わない）
fonts.set_language_fonts("zh", ["sans-sc"])          # 中国語の run だけ別フォント
```

## 折返し・禁則・ハイフネーション

```python
ps = ts.ParagraphStyle()
bo = ps.line_break
bo.wrap = ts.WrapMode.CHAR                 # 欧文の語中でも切る（WORD / NONE もある）
sp = ps.spacing
sp.kinsoku = ts.KinsokuLevel.NORMAL        # 弱い禁則（狭い段で行末が揃いやすい）
sp.line_start_prohibited = "ヶ"            # 文字クラスより優先の追加・除外
ps.spacing = sp
ps.ellipsis = "…"                          # 行数上限で切れたときの省略記号
ps.hanging_indent = 1.0                    # 2 行目以降の字下げ（em）
ps.tab_stops = [ts.TabStop(50.0), ts.TabStop(200.0, ts.TabAlign.RIGHT)]
ps.rotation = 15.0                         # 段落全体を回す（度。行頭を中心に時計回り）

hyph = ts.HyphenationDictionary()          # 欧文のハイフネーション（TeX のパターン）
hyph.for_language("en").add_pattern_file("hyph-en-us.tex")
bo.hyphenation = hyph                      # 辞書は組版の間、生かしておくこと
ps.line_break = bo
```

パターンが無くても、本文中のソフトハイフン `\u00ad` は常に分割位置として扱われます（字面は出ません）。

辞書は `TextStyle.language` で引きますが、見つからないときは**最初に足した言語**（`set_default_language` で変えられる）に
落ちます。和文の文書（`language = "ja"`）に混ざる英単語を英語のパターンで割りたい、という普通のケースがそのまま動きます。

## リンク・ヒットテスト・キャレット・段階表示

`layout_paragraph()` が返す `ParagraphLayout` から、組んだあとの位置を引けます。`origin` は 1 行目の行頭です。

```python
layout = ts.layout_paragraph(fonts, p, ts.WritingMode.HORIZONTAL_TB, default_length=300)
origin = ts.Point(20, 30)

for box in layout.char_boxes(0, origin):          # 行 0 の文字ごとの位置・スタイル・グリフ
    print(box.char_index, box.rect)

rects = layout.rects_for(3, 8, origin)            # 文字範囲 → 行ごとの矩形（リンクの当たり判定・選択範囲）
hit = layout.hit_test(ts.Point(45, 30), origin)   # 点 → 文字（行の外なら None）
caret = layout.caret_rect(5, origin)              # キャレットの矩形

p.add_placeholder(ts.Size(30, 20), body, "widget")          # 描かない空箱
for ph in layout.placeholder_rects(origin):                  # そこにウィジェットを重ねる
    print(ph.id, ph.rect)

layout.save_png("out.png", ts.Size(320, 80), origin, max_chars=12)   # 途中まで描く（組み直さない）
metrics = ts.measure_text(fonts, "見出し", body)              # 折り返さない 1 行の計測
```

吹き出しのように「箱に収める」なら `fit_paragraph()` と `origin_in_box()` を使います。

```python
fit = ts.fit_paragraph(fonts, p, ts.WritingMode.HORIZONTAL_TB, max_lines=3, default_length=150)
print(fit.scale, fit.fits)                                   # 収めるために縮めた倍率
origin = fit.origin_in_box(ts.Rect(10, 10, 150, 90), ts.BlockAlign.CENTER, ts.Align.CENTER)
```

## ゲーム向けのタグ記法

`<b>` `<ruby>` `<color>` `<outline>` `<link>` などのタグ付きテキストをそのまま段落にできます。
書式は [タグ記法](tags.md) を参照してください。

```python
opts = ts.TagParseOptions()
opts.base_style = ts.TextStyle(["serif-ja"], 18)
result = ts.parse_tagged_text('<ruby text="わがはい">吾輩</ruby>は<b>猫</b>である。', opts)
layout = ts.layout_paragraph(fonts, result.paragraph, ts.WritingMode.HORIZONTAL_TB, default_length=360)
```

## 外部オブジェクト（数式・グラフ）

```python
reg = ts.ObjectRegistry()
reg.add_microtex("tex", fonts, text_family="serif-ja")       # 同梱の MicroTeX で LaTeX 数式（ts.HAS_MICROTEX が True のビルド）
reg.add_command("plot", "python samples/handlers/plot.py")   # 要求 JSON → 標準出力の SVG
def bars(req):                                               # Python 関数でも可
    return {"svg": "<svg ...>", "baseline": 20.0}
reg.add("bars", bars)

p.add_object("tex", r"\frac{a}{b}", body)                    # 行内の数式（ベースライン揃え）
p.add_object("bars", "3,5,2", body, {"width": "60"})         # 行内のグラフ
flow.add_object(ts.ObjectBlock("plot", "sin,cos", body, {"width": "300"}, numbered=True))   # 別行立て＋式番号
pages = ts.FlowLayouter(fonts).layout(flow, seq, objects=reg)
print(reg.errors)                                            # 失敗したハンドラ（本文には代替テキスト）
```

## Markdown → PDF（`jtypeset.md`）

```bash
jtypeset-md report.md                                    # report.pdf
jtypeset-md report.md --vertical --paper A5 --toc --font fonts/mincho.otf
python -m jtypeset.md report.md -o out.pdf --png 120 --math mathtext
```

```python
from jtypeset.md import convert_file, Options
pages, warnings = convert_file("report.md", "report.pdf", Options(), png_dpi=0,
                               overrides={"paper": "A5", "writing": "vertical"})
```

先頭の YAML front matter（`Options` のフィールド。kebab-case も可）:

| キー | 内容 | 既定 |
|---|---|---|
| `title` `author` `date` | 表題ブロックと `{title}` | |
| `paper` `landscape` | A4 / A5 / B5 / B6 / 文庫 / 新書 / `148x210mm` | A4 |
| `writing` | horizontal / vertical | horizontal |
| `direction` | auto / ltr / rtl（段落の基底方向） | auto |
| `wrap` `kinsoku` `ruby-offset` | 折返しの方式 / 禁則の強さ / ルビと親文字の間隔（em） | mixed / strict / 0 |
| `lang` `hyphenation` | 本文の言語（BCP47）／ 欧文のハイフネーションのパターン（`hyph-en-us.tex` のパス、または `{en: パス}`） | ja / 無し |
| `columns` `column-gap` | 段数と段間（pt） | 1 |
| `margin` | mm。数値または `{top, bottom, inner, outer}` | 20（A5 等は 16） |
| `fonts` | フォントファイルの列（`{path, key, index, family, weight, italic, languages, lazy}` も可。family 等を書くか `lazy: true` にすると初回使用時に開く）。無ければ `data/` の Noto → OS のフォント | |
| `font-languages` | 言語 → 先に試す family の列（`{zh: [sans-sc]}`） | |
| `font-body` `font-heading` `font-mono` | 本文・見出し・コードの family（FontSet のキー） | serif / sans / sans |
| `size` `line-height` `indent` `justify` | 本文サイズ・行送り・一字下げ・両端揃え | 10.5 / 1.75 / true / true |
| `toc` `toc-depth` `numbering` `heading-page-break` | 目次・見出しの採番・改ページする見出しレベル | false / 2 / true / 0 |
| `header` `footer` | 柱・ノンブル（`{title}` `{page}` `{pages}`） | 題名 / `{page} / {pages}` |
| `links` | footnote / inline / none | footnote |
| `math` | `{handler: auto\|microtex\|mathtext\|command\|none}`（`command` は `command: "..."` も） | auto（MicroTeX 入りのビルドならそれで組む） |
| `highlight` `highlight-style` `tab-width` | Pygments による色付け・スタイル・タブ幅 | true / default / 4 |
| `title-page` `balance-last-page` `draw-guides` | 表題を独立ページに / 最終ページの段揃え / 版面のガイドを描く | false / true / false |
| `footnote-marker-format` `footnote-label-format` `footnote-per-page` | 脚注の書式とページごとの番号 | `{n}` / `{n} ` / false |
| `figure-format` `table-format` `equation-format` | 番号の書式 | 図 {n} / 表 {n} / ({n}) |

対応する記法は `python/jtypeset/md/__init__.py` の docstring と `samples/markdown/report.md` を参照。
