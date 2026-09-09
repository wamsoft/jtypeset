# タグ記法（ゲーム向けのインライン記法）

ゲームのシナリオでよく使う「タグ付きテキスト」をそのまま組めます。吉里吉里Z の richtext プラグインと同じタグ名・属性なので、
既存のテキストをほぼそのまま持ってこられます。

```python
import jtypeset as ts

fonts = ts.FontSet()
fonts.load_file("NotoSerifJP-Regular.otf", "serif")
fonts.load_file("NotoSansJP-Regular.otf", "sans")

opts = ts.TagParseOptions()
opts.base_style = ts.TextStyle(["serif"], 18)
opts.named_families = {"gothic": ["sans"]}          # <font face="gothic">
opts.evaluate = lambda name: "太郎"                  # <eval name="...">

result = ts.parse_tagged_text(
    '<ruby text="わがはい">吾輩</ruby>は<b>猫</b>である。'
    '<keywait><link name="next">つづく</link>', opts)

print(result.errors)                                 # 未知のタグ・閉じ忘れ
layout = ts.layout_paragraph(fonts, result.paragraph, ts.WritingMode.HORIZONTAL_TB, default_length=360)
layout.save_png("out.png", ts.Size(390, 60), ts.Point(15, 28), dpi=192)
```

C++ は `typeset/inl/tag_parser.hpp` の `inl::parseTaggedText()` です。

## タグ

### 文字スタイル

| タグ | 属性 | 意味 |
|---|---|---|
| `<font>` | `size` `weight` `face` `spacing` `width` `height` `italic` `language` | サイズ（pt）・ウェイト（100〜900）・family・字間（em）・平体／長体（倍率）・斜体・言語 |
| `<b>` `<strong>` | | 太字（ウェイト 700。太字の face があればそれを使い、無ければ合成） |
| `<i>` `<em>` | | 斜体（同上） |
| `<u>` / `<s>` `<strike>` `<del>` | `color` | 下線 / 打消し線。縦組みの下線は右側の傍線 |
| `<sup>` / `<sub>` | | 上付き / 下付き |
| `<color>` | `value="#rrggbb"` または `r` `g` `b` `a` | 文字色 |
| `<outline>` | `color` `width` `add` | 縁取り。`add` を付けると層を重ねる（二重縁取り） |
| `<shadow>` | `color` `x` `y` `blur` `add` | 影。`blur` は typeset の拡張（ぼかし半径 pt） |
| `<style>` | `name` | `TagParseOptions.named_styles` に登録したスタイルを丸ごと適用 |

### 注記

| タグ | 属性 | 意味 |
|---|---|---|
| `<ruby>` | `text` `mode=group\|mono\|jukugo` `scale` `offset` | ルビ |
| `<emphasis>` `<dot>` | `mark=sesame\|opensesame\|dot\|circle\|opencircle` `opposite` `scale` `offset` | 圏点 |
| `<tcy>` | | 縦中横 |
| `<warichu>` | `text` `scale` | 割注 |
| `<jidori>` | `em` | 字取り |

### そのほか

| タグ | 属性 | 意味 |
|---|---|---|
| `<br>` / `<sp>` | `width`（`<sp>`、既定 1） | 改行 / 空白 |
| `<link>` | `name` | リンクの範囲。結果の `links` に文字範囲で返るので、`rects_for()` で矩形にできる |
| `<graph>` | `name` `width` `height` | 行内プレースホルダ（描かない箱）。結果の `placeholders` と `placeholder_rects()` で位置が取れる |
| `<start>` `<delay>` `<wait>` `<sync>` `<keywait>` | `value` `diff` `all` | マーカー。結果の `markers` に位置だけ返る（タイミングの制御はホスト側） |
| `<eval>` | `name` `alt` | `TagParseOptions.evaluate` が返した文字列に置き換える（空なら `alt`、それも無ければ `name`） |

実体参照は `&lt; &gt; &amp; &quot; &apos; &#nnn; &#xhhh;` が使えます。`<` で始まってもタグとして読めないもの（`1 < 2` など）は
本文の文字として扱います。未知のタグや閉じ忘れは `errors` に記録して読み飛ばします（`keep_unknown_tags` でそのまま本文に残せます）。

## 組版側との対応

- タグは**スタイルと注記になるだけ**で、組版そのもの（禁則・約物の詰め・両端揃え）は本文の段落と同じです
- `<link>` の範囲は `ParagraphLayout.rects_for(start, end, origin)` で行ごとの矩形になります。ヒットテストは `hit_test(point, origin)`
- `<graph>` の箱は `placeholder_rects(origin)` で位置が取れます。そこにホストがウィジェットや画像を重ねます
- `<keywait>` などのマーカーは位置だけ返るので、`caret_rect(char_index, origin)` で画面上の位置に直せます。
  1 文字ずつの表示は組み直さずに `save_png(..., max_chars=n)` / `emitParagraph(..., maxChars)` で出します
