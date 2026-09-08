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
math:
  handler: mathtext
---

# はじめに {#sec-intro}

この文書は Markdown{index:Markdown} で書かれ、`typeset.md` で組版{index:くみはん|組版}されている。見出しは自動採番され PDF のしおりになり、
先頭の目次は 2 パス目で確定したページ番号を持つ。第 {ref:sec-features} 章で対応する記法を示し、
図 {ref:fig-grad} と表 {ref:tab-syntax} を参照する例も含める。

段落は既定で一字下げになり、行末は両端揃え、約物は JLReq のアキ量表に従う。**強調**は太字、*斜体*は欧文だけが
傾く（和文は立てたまま）。`inline code` は等幅ではなくゴシックで区別する。リンクは印刷向きに
[リポジトリ](https://github.com/wamsoft/glyphware)のように URL を脚注へ落とす。

## この文書で使っている記法 {#sec-features}

- 見出し `#`〜`###` に `{#label}` を付けると `{ref:label}` `{page:label}` で参照できる
- 脚注{index:きゃくちゅう|脚注}は `[^1]` と定義行[^1]。番号は文書を通して連番で、注は段の末尾に集まる
- ルビは青空文庫記法 ｜組版《くみはん》 か、漢字《かんじ》 に直接 《》 を付ける
    - 入れ子の箇条書きは字下げして続く
    - もう一つの項目
- 数式は `$…$` と `$$…$$`。ハンドラは front matter の `math` で選ぶ（matplotlib の mathtext か外部コマンド）

1. 番号付きの箇条書き
2. 二つ目。文章が長くなって折り返すときも、二行目以降は番号のぶんだけ下がった位置から始まるので読みやすい。
3. 三つ目

[^1]: 脚注の本文。ここにも **強調** や `code` が書ける。

## コードブロックと表

```python
import typeset as ts
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

式 {ref:eq-gauss} のように参照できる。数式ハンドラが無い環境では代替テキストが出るだけで、組版は止まらない。

> 引用は背景を付けて段落ごとに置く。長い引用でも段またぎは通常の段落と同じ扱い。

---

<!-- pagebreak -->

# 縦組みについて

同じ Markdown を `writing: vertical` にすると縦組みで組める。縦組みでは数字や欧文が横倒しになり、
ルビは右側に付く。台本や小説の下書きを Markdown で書いて、そのまま縦組みの PDF にする使い方を想定している。

## まとめ

Markdown の要素と typeset のブロックはほぼ 1:1 に対応するので、変換器は薄い。足りない記法は
`<!-- pagebreak -->` や `{#label}` のような小さな拡張で補い、組版の判断はすべて typeset 側に任せている。

<!-- pagebreak -->

# 索引

本文の `{index:よみ|用語}` を集めた索引。`[index]` の位置に置かれる。

[index]
