# typeset Python ガイド

C++ コアの pybind11 バインディング `typeset` と、Markdown → PDF の `typeset.md` の使い方。
API の一覧（クラス・メソッド・引数）は `make pydocs` で生成する HTML（`build/docs/python/index.html`）と
型スタブ `python/typeset/_typeset/__init__.pyi`（pybind11-stubgen で生成。IDE の補完にも使われる）を参照。C++ の概念・座標系の説明は `docs/cpp_guide.md`。

## インストール

```bash
pip install dist/typeset-*.whl            # ビルド済み wheel（pip wheel . -w dist --no-deps で作る。VCPKG_ROOT が要る）
pip install "typeset[md]"                 # Markdown → PDF（markdown-it-py / mdit-py-plugins / PyYAML）
pip install "typeset[md,highlight]"       # + Pygments（コードブロックの色付け。無くても単色で組める）
```

開発ツリーから使うときは `PYTHONPATH=build/x64-windows/python/Release`（`typeset/` パッケージがここにまとまる）。

## 最小の例

```python
import typeset as ts

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
| `FontSet` | フォントを開いてキー／family 名で引く。文字が無ければ次の family へフォールバック |
| `TextStyle` / `ParagraphStyle` | 文字（フォント・サイズ・色・ベースラインのずらし…）／段落（揃え・行送り・一字下げ・空白保持・タブ幅…） |
| `Paragraph` | run の列＋注記。`add_run(text, style, literal=False)`、`add_image`、`add_object`（外部オブジェクト）、`add_footnote`、`annotate` |
| `Annotation` | `ruby(start, end, text, mode, scale)`、`tate_chu_yoko`、`emphasis(..., opposite_side)`、`warichu`、`jidori` |
| `Flow` と各 Block | `add_paragraph` / `add_heading(p, level, style, numbered)` / `add_list(ListBlock)` / `add_table(TableBlock)` / `add_image(ImageBlock)` / `add_object(ObjectBlock)` / `add_toc(TocBlock)` / `add_index(IndexBlock)` / `add_rule` / `add_page_break` / `add_column_break` / `add_section(columns)` |
| `BlockStyle` | 前後アキ、orphans / widows、keep_with_next、keep_together、break_before / after、span_columns、label（相互参照）、background / padding |
| `PageSequence` / `PageMaster` | 判型（`ts.paper.A4` … `ts.paper.landscape(size)`）、余白、段数、書字方向、`header` / `footer`（`RunningText`）、duplex |
| `FlowLayouter.layout(...)` | ページ列を返す。`fields`（`{title}` 等）、`figure_format` / `table_format` / `equation_format`、`objects`（ObjectRegistry）、`footnote_marker_format` / `footnote_label_format` / `footnote_per_page`、`balance_last_page`、`draw_guides` |
| `Page` | `number`、`save_png(path, dpi)`、`save_svg(path)`、`to_svg()`。PDF は `save_pdf(pages, path, title, author, subset_fonts, compress)` → `(ok, warnings)` |
| `ObjectRegistry` | 外部オブジェクトのハンドラ。`add(name, fn)`（fn は request dict → SVG 文字列か `{"svg", "baseline"}`）、`add_command(name, cmd)` |

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

## 外部オブジェクト（数式・グラフ）

```python
reg = ts.ObjectRegistry()
reg.add_command("plot", "python samples/handlers/plot.py")   # 要求 JSON → 標準出力の SVG
def bars(req):                                               # Python 関数でも可
    return {"svg": "<svg ...>", "baseline": 20.0}
reg.add("bars", bars)

p.add_object("bars", "3,5,2", body, {"width": "60"})         # 行内（ベースライン揃え）
flow.add_object(ts.ObjectBlock("plot", "sin,cos", body, {"width": "300"}, numbered=True))   # 別行立て＋式番号
pages = ts.FlowLayouter(fonts).layout(flow, seq, objects=reg)
print(reg.errors)                                            # 失敗したハンドラ（本文には代替テキスト）
```

## Markdown → PDF（`typeset.md`）

```bash
typeset-md report.md                                    # report.pdf
typeset-md report.md --vertical --paper A5 --toc --font fonts/mincho.otf
python -m typeset.md report.md -o out.pdf --png 120 --math mathtext
```

```python
from typeset.md import convert_file, Options
pages, warnings = convert_file("report.md", "report.pdf", Options(), png_dpi=0,
                               overrides={"paper": "A5", "writing": "vertical"})
```

先頭の YAML front matter（`Options` のフィールド。kebab-case も可）:

| キー | 内容 | 既定 |
|---|---|---|
| `title` `author` `date` | 表題ブロックと `{title}` | |
| `paper` `landscape` | A4 / A5 / B5 / B6 / 文庫 / 新書 / `148x210mm` | A4 |
| `writing` | horizontal / vertical | horizontal |
| `columns` `column-gap` | 段数と段間（pt） | 1 |
| `margin` | mm。数値または `{top, bottom, inner, outer}` | 20（A5 等は 16） |
| `fonts` | フォントファイルの列（`{path, key, index}` も可）。無ければ `data/` の Noto → OS のフォント | |
| `font-body` `font-heading` `font-mono` | 本文・見出し・コードの family（FontSet のキー） | serif / sans / sans |
| `size` `line-height` `indent` `justify` | 本文サイズ・行送り・一字下げ・両端揃え | 10.5 / 1.75 / true / true |
| `toc` `toc-depth` `numbering` `heading-page-break` | 目次・見出しの採番・改ページする見出しレベル | false / 2 / true / 0 |
| `header` `footer` | 柱・ノンブル（`{title}` `{page}` `{pages}`） | 題名 / `{page} / {pages}` |
| `links` | footnote / inline / none | footnote |
| `math` | `{handler: mathtext}` または `{handler: command, command: "..."}` | 無し（代替テキスト） |
| `highlight` `highlight-style` `tab-width` | Pygments による色付け・スタイル・タブ幅 | true / default / 4 |
| `footnote-marker-format` `footnote-label-format` `footnote-per-page` | 脚注の書式とページごとの番号 | `{n}` / `{n} ` / false |
| `figure-format` `table-format` `equation-format` | 番号の書式 | 図 {n} / 表 {n} / ({n}) |

対応する記法は `python/typeset/md/__init__.py` の docstring と `samples/markdown/report.md` を参照。
