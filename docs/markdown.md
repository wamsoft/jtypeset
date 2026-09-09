# Markdown → PDF の手引き

`jtypeset-md` は Markdown を typeset で組版して PDF にします。見出しの採番・目次・索引・脚注・表・図・数式・ルビなど、
レポートや小説に必要な要素を Markdown の記法（と少しの拡張）で書けます。

## 1. インストール

```bash
pip install "jtypeset[md]"            # markdown-it-py / mdit-py-plugins / PyYAML
pip install "jtypeset[md,highlight]"  # + Pygments（コードブロックの色付け。無くても単色で組める）
pip install matplotlib                # 任意: 数式を MicroTeX でなく matplotlib の mathtext で組むとき
```

確認:

```bash
jtypeset-md --help
```

## 2. 最初の 1 枚

`hello.md`:

```markdown
---
title: はじめての組版
author: 名前
paper: A5
---

# 見出し

本文は既定で一字下げになり、行末は両端揃えです。**強調**、*斜体*、`コード`、リンクは
[typeset](https://github.com/wamsoft/jtypeset) のように書けます（URL は脚注に落ちます）。

- 箇条書き
- 二つ目

> 引用。
```

```bash
jtypeset-md hello.md            # hello.pdf ができる
jtypeset-md hello.md --png 120  # hello_p1.png … も出す（確認用）
```

フォントを指定しなければ、カレントの `data/` の Noto → OS のフォント（Windows: 游明朝・游ゴシック、macOS: ヒラギノ、
Linux: Noto CJK）の順に探します。見つからなければエラーになるので、`--font` か front matter の `fonts` で指定してください。
絵文字は `data/` の Noto 絵文字フォント（`make fontdata`）か OS のカラー絵文字フォント（Segoe UI Emoji / Apple Color Emoji /
Noto Color Emoji）が見つかれば自動でフォールバックに加わり、PDF / SVG / PNG のどれでもカラーで組まれます（縦組みでは正立）。

## 3. コマンドラインオプション

| オプション | 内容 |
|---|---|
| `-o OUT` | 出力 PDF（既定: 入力と同名の `.pdf`） |
| `--paper A4\|A5\|B5\|B6\|文庫\|新書\|148x210mm` | 判型 |
| `--landscape` | 横置き |
| `--vertical` | 縦組み |
| `--columns N` | 段数 |
| `--font PATH`（複数可） | フォントファイル。最初のものが本文の第一候補。TTC も可 |
| `--font-body KEY` `--font-heading KEY` | 本文・見出しの family（読み込んだフォントのキー） |
| `--size PT` | 本文サイズ |
| `--toc` | 先頭に目次 |
| `--no-numbering` | 見出しを採番しない |
| `--no-indent` | 段落の一字下げをしない |
| `--math mathtext` / `--math command:<cmd>` | 数式ハンドラ |
| `--png DPI` | 各ページを PNG にも出す |
| `--guides` | 版面と段の枠を描く（レイアウト確認用） |

コマンドラインの指定は front matter より優先します。

## 4. front matter（文書の設定）

Markdown の先頭に `---` で囲んだ YAML を書きます。キーは `snake_case` でも `kebab-case` でも構いません。

```yaml
---
title: 組版エンジン評価レポート
author: 組版基盤チーム
date: 2026 年 9 月 8 日
paper: A4                 # A4 / A5 / B5 / B6 / 文庫 / 新書 / "148x210mm"
landscape: false
writing: horizontal       # horizontal / vertical
direction: auto           # auto / ltr / rtl（段落の基底方向。行内のアラビア文字・ヘブライ文字は常に双方向で並ぶ）
wrap: mixed               # mixed / char / word / none（折返し: 既定は和文が字ごと・欧文が語ごと）
lang: ja                  # 本文の言語（BCP47）
hyphenation: hyph-en-us.tex   # 欧文のハイフネーション（TeX のパターン。{en: 〜.tex} と言語ごとにも書ける）
kinsoku: strict           # strict / normal / loose（禁則の強さ。狭い段では normal / loose で行末が揃いやすい）
ruby-offset: 0            # ルビと親文字の間隔（親文字の em）
columns: 1
margin: 20                # mm。{top: 25, bottom: 20, inner: 22, outer: 18} も可
fonts:                    # フォントファイル（相対パスは Markdown の場所から）
  - fonts/NotoSerifJP-Regular.otf
  - {path: fonts/NotoSansJP-Regular.otf, key: sans}
  - {path: C:/Windows/Fonts/YuGothM.ttc, key: yugo, index: 0}
  - {path: fonts/NotoSerifJP-Bold.otf, key: serif-b, family: [serif], weight: 700}   # 同じ family の太字（**強調** で使われる）
  - {path: fonts/NotoSansSC-Regular.otf, key: sans-sc, languages: [zh]}             # 中国語のテキストで先に試す
font-languages: {zh: [sans-sc]}   # 言語 → 先に試す family（宣言の languages と同じ意味）
font-body: [serif]        # 本文の family（キー）。最初に無い字は次へフォールバック
font-heading: [sans]
font-mono: [sans]
size: 10.5
line-height: 1.75
indent: true              # 一字下げ
justify: true             # 両端揃え
toc: true                 # 先頭に目次（本文の [toc] でも置ける）
toc-depth: 2
numbering: true           # 見出しの採番（1. / 1.1 / 1.1.1）
heading-page-break: 0     # このレベル以下の見出しで改ページ（1 なら章ごとに改ページ）
header: "{title}"         # 柱。null で無し
footer: "{page} / {pages}"
links: footnote           # footnote / inline / none
math:
  handler: auto           # auto（MicroTeX があれば使う）/ microtex / mathtext / command（command: "..."）/ none
highlight: true           # コードブロックの色付け（Pygments）
highlight-style: default  # Pygments のスタイル名
tab-width: 4
footnote-per-page: false  # 脚注番号をページごとに 1 から
footnote-marker-format: "{n}"
footnote-label-format: "{n} "
figure-format: "図 {n}"
table-format: "表 {n}"
equation-format: "({n})"
---
```

キーの一覧は `jtypeset.md.Options` のフィールドです（`python -c "import jtypeset.md as m; help(m.Options)"`）。

## 5. 記法

### 見出し・参照・しおり

```markdown
# 章の見出し {#sec-intro}
## 節の見出し
### 項の見出し
```

`numbering: true`（既定）なら `1.` `1.1` `1.1.1` と採番され、PDF のしおり（アウトライン）になります。
末尾の `{#label}` を付けると本文から参照できます:

```markdown
第 {ref:sec-intro} 章（{page:sec-intro} ページ）を参照。
```

`{ref:label}` は番号、`{page:label}` はページ番号に置き換わります。図・表・数式・見出しのどれにも使えます。
`{` をそのまま出したいときはコードスパン（`` `{…}` ``）に入れてください（コードは置換の対象外）。

### 段落

- 段落は空行で区切ります。既定で一字下げ（`indent: false` で無し）、両端揃え（`justify: false` で行頭揃え）
- 行末に空白 2 つ、または `<br>` で段落内の改行
- **強調**は太字、*斜体*は欧文だけが傾きます（和文は立てたまま）。`インラインコード` はゴシック体
- `~~打消し線~~`（`<s>` `<del>` でも）と `<u>下線</u>`（`<ins>` でも）。縦組みの下線は右側の傍線になります
- リンク `[表示](URL)` は表示文字を本文に置き、URL を脚注へ落とします（`links: inline` で本文に、`none` で捨てる）

### 箇条書き

```markdown
- 項目
- 項目
    - 入れ子は 4 空白で
1. 番号付き
2. 二つ目。折り返した行は番号のぶん下がった位置から始まる
```

### コードブロック

````markdown
```python
def hello():
	print("タブは 4 桁のタブ位置に展開される")
```
````

背景付き・空白保持で組まれます。言語名があり Pygments が入っていれば色が付きます（`highlight: false` で切る）。
コードの中の `{…}` は置換されません。

### 表（GFM）

```markdown
| 項目 | 説明 | 値 |
|:-----|:-----|---:|
| A    | 左揃え | 1 |
| B    | セル内は 1 段落 | 22 |

表: キャプションは直後の「表: 〜」で書く {#tab-a}
```

列幅は内容から自動で決まり（3 列以上は版面いっぱい）、ヘッダ行はページをまたぐと繰り返されます。
段より高い行は途中で分けて続きます。セル内で `|` を書くときは `\|`。

### 画像

```markdown
![キャプション。alt から取る {#fig-a}](figure.png)

文中の画像 ![](icon.png) は行の高さに合わせて行内に置かれる。
```

画像だけの段落は図になり、`{fig}` の番号（図 1）とキャプションが付きます。版面より大きい画像は段に合わせて縮みます。
PNG / JPEG / BMP / GIF を読めます（JPEG は PDF にそのまま入ります）。

### 脚注

```markdown
本文[^1]。
[^1]: 注の本文。**強調**やコードも書ける。
```

記号は上付きの番号、注はその行が載った段の末尾に集まり、本文との間に短い罫が入ります。
番号は文書を通して連番（`footnote-per-page: true` でページごとに 1 から）。

### 数式

```markdown
行内の数式 $e^{i\pi} + 1 = 0$ と、別行立て:

$$
\int_{-\infty}^{\infty} e^{-x^2}\,dx = \sqrt{\pi}
$$ (eq-gauss)

式 {ref:eq-gauss} を参照。
```

既定では同梱の **MicroTeX**（LaTeX 数式のレンダラ）が組み、数式のグリフは数式フォント（Computer Modern 系）として PDF に埋め込まれます。
分数・根号・積分・総和・行列（`pmatrix`）・場合分け（`cases`）・ギリシャ文字・`\mathbf` `	ext{}` などが使えます。
`math` でハンドラを切り替えられます（無い環境では `[tex]` の代替テキストになるだけで、組版は止まりません）。

- `math: {handler: auto}`（既定）— MicroTeX があればそれ、無ければ何もしない
- `math: {handler: microtex}` — 同梱の MicroTeX。`res_dir` で数式フォントの場所を差し替え可
- `math: {handler: mathtext}` — matplotlib の mathtext（`pip install matplotlib`）。TeX のサブセット
- `math: {handler: command, command: "..."}` — 外部コマンド。要求（JSON ファイル）を引数に受け、SVG を標準出力に書く。
  MathJax / dvisvgm / Typst など、SVG を吐けるものなら何でも接続できます。要求の形式とベースラインの伝え方は
  [Python ガイド](python_guide.md) の「外部オブジェクト」を参照
- `math: {handler: none}` — 数式を組まない

### ルビ・縦組み向けの記法

```markdown
｜組版《くみはん》     ← 青空文庫形式（| でも可）
漢字《かんじ》         ← 直前の漢字の連続に付く
{東京|とうきょう}      ← 波括弧形式
```

縦組み（`writing: vertical`）では数字や欧文は横倒しになり、ルビは右側、脚注は段の左端に付きます。
縦中横や圏点は Markdown 記法を用意していないので、必要なら Python API（`Annotation`）で組んでください。

### 改ページ・段組・目次・索引

```markdown
<!-- pagebreak -->        改ページ
<!-- columnbreak -->      改段
<!-- columns: 2 -->       ここから 2 段組（ページ単位で切り替わる）
[toc]                     目次をここに
[index]                   索引をここに。本文の {index:よみ|用語} を集める
```

索引の項目は本文に `{index:くみはん|組版}`（読み|用語）と書きます。読みでかなの五十音順（欧文は A–Z）に並び、
見出し文字（あ・か・さ…）を挟んで用語とページ番号を点線で結びます。

## 6. Python から呼ぶ

```python
from jtypeset.md import convert_file, convert_text, build_flow, Options

pages, warnings = convert_file("report.md", "report.pdf", Options(), png_dpi=110,
                               overrides={"paper": "A5", "writing": "vertical"})
for w in warnings:
    print(w)

# Flow を取り出して自分で手を加える
conv = build_flow(open("report.md", encoding="utf-8").read(), base_dir=".")
conv.flow.add_page_break()
pages = conv.layout()
```

## 7. 困ったとき

| 症状 | 見るところ |
|---|---|
| `no font could be loaded` | `--font` か front matter の `fonts` でフォントファイルを指定する。TTC も可 |
| 文字が豆腐（□）になる | そのフォントに無い字。`font-body` に別のフォントのキーを続けて書くとフォールバックする |
| `font not embedded: … (fsType: Restricted License embedding)` | 埋め込みが許可されていないフォント。別のフォントを使う |
| 数式が `[tex: …]` のまま | `math` のハンドラが無い、または失敗。警告メッセージに原因が出る |
| 表の列が狭い／広い | 列数が 3 以上なら版面いっぱいに広がる。セルの内容の自然幅で配分されるので、長い語を分けるか列を減らす |
| 版面を確認したい | `--guides` で版面と段の枠を描く |
| 図がページの先頭に送られる | 図＋キャプションが残りに入らないと次の段へ移る。図を小さくするか位置を変える |
