# typeset C++ リファレンス

縦書き・横書きの日本語組版（JLReq 水準）を 1 つのエンジンで行い、同一の組版結果をラスタ / PDF / SVG へ出す
C++17 ライブラリ。この文書はヘッダのコメントから生成したリファレンスの入口で、全体の流れと主要な型を示す。
設計の背景は `検討.md`、型と処理の具体化は `設計.md`、進捗と積み残しは `実装.md`（リポジトリ）にある。

## 使い方の流れ

```cpp
#include "typeset/font/font_set.hpp"
#include "typeset/block/block.hpp"
#include "typeset/page/flow_layouter.hpp"
#include "typeset/backend/pdf_writer.hpp"
using namespace typeset;

font::FontSet fonts;
fonts.loadFile("data/NotoSerifJP-Regular.otf", "serif-ja");   // キーで引く。family 名でも引ける

TextStyle body;                       // 文字スタイル（フォント列・サイズ・色…）
body.font.family = {"serif-ja"};
body.size = 10.5f;

block::Flow flow;                     // ブロックの列（段落・見出し・表・画像・目次…）
inl::Paragraph p = inl::Paragraph::plain(u"吾輩は猫である。", body);
p.annotations.push_back(inl::Annotation::ruby(0, 2, u"わがはい"));
flow.addParagraph(p);

page::PageSequence seq;               // 判型・余白・段・柱・ノンブル
seq.master.size = page::paper::A5;
seq.master.writingMode = WritingMode::VerticalRl;

page::FlowLayouter layouter(fonts);
std::vector<page::Page> pages = layouter.layout(flow, seq);   // 流し込み（相互参照・目次は多パス）

backend::PdfWriter pdf;
for (const page::Page& pg : pages) pdf.addPage(pg.dl);
pdf.save("out.pdf");
```

1 段落だけ組みたいときは `inl::ParagraphLayouter::layout()` で `inl::ParagraphFragment`（行の列）を得て、
`inl::emitParagraph()` で表示リスト（`dl::DisplayList`）にする。表示リストは 3 つの backend（
`backend::RasterRenderer` / `backend::PdfWriter` / `backend::SvgWriter`）がそのまま描く。

## レイヤーと主要な型

| 層 | 主要な型 | 役割 |
|---|---|---|
| `typeset` | `Pt` `Point` `Rect` `Matrix` `Color` `Path`（geom.hpp）、`WritingMode`（writing_mode.hpp）、`TextStyle` `TextLayer` `TextShadow` `TextDecoration` `ParagraphStyle` `SpacingOptions` `BreakOptions`（style.hpp） | 幾何・書字方向・スタイル。文字の外観は塗り＋縁取りのほか、影・多層の縁取り（`layers`）・下線・打消し線 |
| `typeset::font` | `FontSet` `FontDeclaration` | フォントをキー／family 名で引き、文字カバレッジでフォールバックを解決。同じ family の複数 face から weight / italic の最近傍を選ぶ（無ければフェイクボールド／斜体）。`declare()` はメタデータだけ登録して初回使用時に開く。`setLanguageFonts()` で言語ごとに先に試す family。バリアブルフォントは `FontSpec::variations` / weight で軸を固定した別 Face（`instance()`）。シェイピング用の `hb_font_t` も持つ |
| `typeset::text` | `CharClass` `SpacingTable`（JLReq 附属書 A・表 3）、`orientation`（UAX #50）、`line_break`（UAX #14） | 文字クラスとアキ量、向き、分割機会。双方向（UAX #9）は shaper が glyphware の SheenBidi で解き、行ごとに視覚順へ並べ替える（`ParagraphStyle::direction`） |
| `typeset::inl` | `Paragraph` `InlineRun` `Annotation`、`ParagraphLayouter` `ParagraphFragment` `LineBox`、`LineShapeProvider`、`shapeText()` | 行内組版。Box / Glue / Penalty 列を Greedy / Knuth–Plass で解く。ルビ・縦中横・圏点・割注・字取り・行内画像／オブジェクト／プレースホルダ（`addPlaceholder`）・脚注記号 |
| `typeset::inl`（取り出し口） | `charBoxes()` `rectsFor()` `placeholderRects()` `hitTest()` `caretRect()` `measureText()`、`emitParagraph(..., maxChars)` | 組んだあとの問い合わせ。文字の箱、文字範囲 → 矩形（リンク・選択）、プレースホルダの位置、点 → 文字、キャレット、1 行計測。`maxChars` で組み直さずに途中まで描く（段階表示） |
| `typeset::block` | `Flow`、`ParagraphBlock` `HeadingBlock` `RuleBlock` `SpacerBlock` `LabeledBlock` `SectionBlock` `ImageBlock` `TableBlock` `ListBlock` `TocBlock` `IndexBlock` `ObjectBlock`、`BlockStyle` | ブロックの列と改ページ制御（orphans / widows / keepWithNext / keepTogether / spanColumns） |
| `typeset::page` | `PageMaster` `PageSequence` `Margins` `RunningText` `Region`、`FlowLayouter` `FlowLayoutOptions`、`Page` | 判型・段・柱・ノンブル。Flow をページ列へ流し込み、採番・相互参照・目次・索引・脚注を多パスで解く |
| `typeset::dl` | `DisplayList`、`GlyphRun` `PathItem` `RectItem` `ImageItem` `Group` `Bookmark`、`glyphMatrix()` | レイアウトと backend の分割線。グリフ変形は 1 箇所 |
| `typeset::backend` | `RasterRenderer` `PdfWriter` `SvgWriter` | 表示リストの描画。PDF は Identity-H・サブセット・Flate・しおり・fsType 判定 |
| `typeset::image` | `Image`、`loadFile()` `encodePng()` | 画像の読み込み（stb_image）と PNG 出力 |
| `typeset::obj` | `ObjectRegistry` `ObjectRequest` `ObjectResult`、`importSvg()` | 外部オブジェクト（数式・グラフ）の差し込み口。関数／外部コマンドのハンドラ、SVG サブセット読み込み |

## 座標と単位の約束

- 文書内部の単位は **pt**。ページ座標は左上原点・y-down。ラスタは dpi/72 を掛ける
- 行内は論理座標（`inline_` = 送り方向、`block` = 行の中心線からのずれ。縦組みは右が正、横組みは下が正）で組み、
  物理化は `inl::emitParagraph()` の 1 箇所。注記（ルビ・圏点）の付く側は `inl::annotationSide()`（縦: 右、横: 上）
- ブロック軸は「em box の中心＝行の中心線」。横組みのベースラインは第一候補フォントから `inl::baselineOffset()` で決める
- 和文組版は Box / Glue / Penalty 列を解く（禁則は Glue 直前の Penalty(∞)、ぶら下げは幅が負の Penalty、
  和字間には `SpacingOptions::kanjiSkipStretch` の伸び）
- 本文中の `{page}` `{pages}` `{title}`（柱・ノンブル）、`{ref:label}` `{page:label}`（相互参照）、`{fig}` `{table}` `{eq}`
  （キャプションの番号）、`{index:よみ|用語}`（索引）は `page::FlowLayouter` が置換・収集する。
  `InlineRun::literal` を立てた run は置換しない

## 拡張点

- **行の形**: `inl::LineShapeProvider::at(lineIndex, extraBlock)` を実装すると行ごとの行長・字下げを与えられる
  （回り込みの `page::RegionLineShape` がこれ。吹き出しなどの形もここで）
- **外部オブジェクト**: `obj::ObjectRegistry::add()` に関数を登録すると、`inl::Paragraph::addObject()` /
  `block::ObjectBlock` から呼ばれ、返した箱（大きさ・ベースライン）で配置される。`addCommand()` なら外部コマンドの
  標準出力（SVG）を読む。`handlers/microtex/` が MicroTeX（LaTeX 数式）をこの口でつないだ例
- **出力先**: `dl::DisplayList` を受けて描くものを書けば backend が増える（`dl::Item` の variant を訪問する）

## サンプル

`samples/` に用途別の例がある: `sample_inline`（縦横同一文）、`sample_script`（台本）、`sample_novel`（小説 2 段）、
`sample_tech`（技術文書）、`sample_report`（レポート: 目次・採番・図表番号・脚注・索引）、`sample_objects`（外部オブジェクト）、
`handlers/microtex/sample_microtex`（LaTeX 数式）。すべてリポジトリルートで実行し、`data/` のフォントを読む。
