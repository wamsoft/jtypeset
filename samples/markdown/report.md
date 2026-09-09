---
title: typeset で Markdown から PDF を作る
author: 組版基盤チーム
date: 2026 年 9 月 8 日
paper: A4
writing: horizontal
size: 10.5
toc: true
numbering: true
footer: "{page} / {pages}"
lang: ja                  # 本文の言語（BCP47）。ハイフネーションと字形の選択に使う
ruby-offset: 0.05         # ルビと親文字の間隔（親文字の em）
hyphenation: ../../data/hyph-en-us.tex    # 欧文のハイフネーション（TeX のパターン。make fontdata が取る）
fonts:                    # 既定では data/ の Noto → OS のフォントを自動で探す。ここでは双方向の例のために足す
  - {path: ../../data/NotoSansHebrew-Regular.ttf, key: hebrew, languages: [he]}
  - {path: ../../data/NotoSansArabic-Regular.ttf, key: arabic, languages: [ar]}
font-body: [serif, hebrew, arabic]
# math: 既定は auto（MicroTeX 入りのビルドなら LaTeX 数式をそのまま組む）。mathtext / command / none も選べる
# wrap: mixed / char / word / none、kinsoku: strict / normal / loose も指定できる
---

# はじめに {#sec-intro}

この文書は Markdown{index:Markdown} で書かれ、`jtypeset.md` で組版{index:くみはん|組版}されている。見出しは自動採番され PDF のしおりになり、
先頭の目次は 2 パス目で確定したページ番号を持つ。第 {ref:sec-features} 章で対応する記法を示し、
図 {ref:fig-grad} と表 {ref:tab-syntax} を参照する例も含める。

段落は既定で一字下げになり、行末は両端揃え、約物は JLReq のアキ量表に従う。**強調**は太字（太字の face が
あればそれを使い、無ければ合成する）、*斜体*は欧文だけが傾く（和文は立てたまま）。~~打消し線~~ と
<u>下線</u> も書ける（縦組みの下線は右側の傍線になる）。`inline code` は等幅ではなくゴシックで区別する。
リンクは印刷向きに [リポジトリ](https://github.com/wamsoft/glyphware)のように URL を脚注へ落とす。

## この文書で使っている記法 {#sec-features}

- 見出し `#`〜`###` に `{#label}` を付けると `{ref:label}` `{page:label}` で参照できる
- 脚注{index:きゃくちゅう|脚注}は `[^1]` と定義行[^1]。番号は文書を通して連番で、注は段の末尾に集まる
- ルビは青空文庫記法 ｜組版《くみはん》 か、漢字《かんじ》 に直接 《》 を付ける
    - 入れ子の箇条書きは字下げして続く
    - もう一つの項目
- 絵文字{index:えもじ|絵文字}もカラーで組める 😀👍🏽🇯🇵👨‍👩‍👧❤️（`make fontdata` の Noto 絵文字か OS の絵文字フォントを自動で使う。縦組みでは正立）
- 数式は `$…$` と `$$…$$`。既定で MicroTeX（LaTeX 数式）が組む。front matter の `math` で mathtext や外部コマンドにも切り替えられる
- 装飾は `~~打消し線~~`（`<s>` `<del>` でも）と `<u>下線</u>`（`<ins>` でも）

1. 番号付きの箇条書き
2. 二つ目。文章が長くなって折り返すときも、二行目以降は番号のぶんだけ下がった位置から始まるので読みやすい。
3. 三つ目

[^1]: 脚注の本文。ここにも **強調** や `code` が書ける。

## コードブロックと表

```python
import jtypeset as ts
fonts = ts.FontSet()
fonts.load_file("data/NotoSerifJP-Regular.otf", "serif")
flow = ts.Flow()
body = ts.TextStyle(["serif"], 10.5)
for i, line in enumerate(["吾輩は猫である。", "名前はまだ無い。"]):
	if i % 2 == 0:  # タブ字下げは 4 桁のタブ位置に展開される
		flow.add_paragraph(ts.Paragraph(line, body))
pages = ts.FlowLayouter(fonts).layout(flow, ts.PageSequence())
ts.save_pdf(pages, "neko.pdf")
```

| 記法 | typeset のブロック | 備考 |
|:-----|:------------------|:-----|
| `# 見出し` | HeadingBlock | 採番・しおり・目次 |
| 段落 | ParagraphBlock | 一字下げ・両端揃え・脚注 |
| `- 項目` | ListBlock | 入れ子は字下げ |
| `\| 表 \|` | TableBlock | 直後の「表: 〜」がキャプション |
| `![](img)` | ImageBlock | 単独の段落なら図、文中なら行内画像 |
| `$$…$$` | ObjectBlock | 数式ハンドラで組む |

表: Markdown と typeset の対応 {#tab-syntax}

## 図と数式

![グラデーションの図。キャプションは alt から取る {#fig-grad}](figure.png)

行内の数式 $e^{i\pi} + 1 = 0$ は本文のベースラインに揃い、別行立ての式には番号が付く。

$$
\int_{-\infty}^{\infty} e^{-x^2}\,dx = \sqrt{\pi}
$$ (eq-gauss)

式 {ref:eq-gauss} のように参照できる。数式は MicroTeX が組み、数式フォントのグリフとして PDF に埋め込まれる。ハンドラが無い環境では代替テキストが出るだけで、組版は止まらない。

行列や場合分けもそのまま書ける:

$$
\begin{pmatrix} a & b \\ c & d \end{pmatrix} \begin{pmatrix} x \\ y \end{pmatrix} = \begin{pmatrix} ax+by \\ cx+dy \end{pmatrix}, \qquad f(x)=\begin{cases} x^2 & (x\ge 0) \\ -x & (x<0) \end{cases}
$$

> 引用は背景を付けて段落ごとに置く。長い引用でも段またぎは通常の段落と同じ扱い。

## 欧文と多言語 {#sec-intl}

front matter に `hyphenation` でパターン（TeX の `hyph-en-us.tex` など）を渡すと、欧文の単語を途中で割って
ハイフンを出す。段が狭いほど効きが分かりやすいので、ここだけ 2 段組にしている。パターンが無くても、本文中の
ソフトハイフンは常に分割位置として扱われる。

<!-- columns: 2 -->

Typesetting is the composition of text by means of arranging physical type or digital equivalents in order
to display printed matter. Stored letters and other symbols are retrieved and ordered according to a
language's orthography for visual display. Internationalization and localization of typesetting software
require careful handling of line breaking, hyphenation and bidirectional reordering.

日本語と欧文が混ざる行では和欧間に四分アキが入り、約物との間にはアキを入れない。行末に置けない文字（終わり括弧・
句読点・小書きの仮名など）は禁則で送られ、`kinsoku: normal` にすると小書きの仮名や長音は「弱い禁則」になって
行末が揃いやすくなる。

<!-- columns: 1 -->

アラビア文字やヘブライ文字が混ざる行は UAX #9 で視覚順に並べ替える。日本語の中に
עברית（ヘブライ語）や العربية جميلة（アラビア語）が入っても、右から左に読む部分だけが反転して、
残りはそのまま左から右に流れる。フォントは front matter の `fonts` に `languages` を書いておくと、
その言語のテキストで先に試される（この文書では `make fontdata` で取れる Noto のアラビア・ヘブライを指定している）。

---

<!-- pagebreak -->

# 縦組みについて

同じ Markdown を `writing: vertical` にすると縦組みで組める。縦組みでは数字や欧文が横倒しになり、
ルビは右側に付く。台本や小説の下書きを Markdown で書いて、そのまま縦組みの PDF にする使い方を想定している。

## まとめ

Markdown の要素と typeset のブロックはほぼ 1:1 に対応するので、変換器は薄い。足りない記法は
`<!-- pagebreak -->` や `{#label}` のような小さな拡張で補い、組版の判断はすべて typeset 側に任せている。
第 {ref:sec-intl} 章のハイフネーションや双方向テキストのように、組版側の機能は front matter の 1 行で有効になる。

<!-- pagebreak -->

# 索引

本文の `{index:よみ|用語}` を集めた索引。`[index]` の位置に置かれる。

[index]
