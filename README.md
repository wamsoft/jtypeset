# typeset / jtypeset — 縦書き・横書きの日本語組版

**typeset** は、縦書き・横書きの日本語組版を 1 つのエンジンで行い、同じ組版結果を **PDF / SVG / PNG** に出す
C++17 ライブラリです。Python パッケージ **jtypeset** から使え、**Markdown を書いて日本語の PDF を作る**
コマンド `jtypeset-md` が付いています。

- 日本語組版の基本（JLReq）に沿った約物の詰め・禁則・追い込み／追い出し・ぶら下げ・両端揃え
- ルビ（グループ／モノ／熟語）・縦中横・圏点・割注・字取り
- 見出しの採番と PDF のしおり、目次、索引、図表番号と相互参照、脚注、箇条書き、コードブロック、表（ページまたぎ・rowspan）、
  画像の回り込み、段組と段抜き、柱・ノンブル
- LaTeX 数式（同梱の MicroTeX。数式フォントのグリフとして PDF に埋め込む）と、グラフなど外部レンダラの出力（SVG）の差し込み
- フォントはファイルから直接読み、PDF にはサブセットで埋め込む（埋め込み許可 fsType を確認）。カラー絵文字（COLR / ビットマップ）も PDF / SVG / PNG で同じ色に

想定している用途は台本・小説・技術文書・レポートなどのツール作りです。詳しい説明は
**[ドキュメントサイト](https://wamsoft.github.io/jtypeset/)** にあります（Markdown → PDF の手引き、C++ / Python のリファレンス）。

## Python 版（jtypeset）の入手と使い方

```bash
pip install jtypeset                   # 本体
pip install "jtypeset[md]"             # Markdown → PDF（markdown-it-py など）
pip install "jtypeset[md,highlight]"   # + Pygments（コードブロックの色付け。無くても単色で組める）
```

### Markdown から PDF を作る

```bash
jtypeset-md report.md                                   # → report.pdf
jtypeset-md novel.md --vertical --paper A5 --font fonts/mincho.otf   # 縦組み・A5・フォント指定
jtypeset-md report.md --toc --png 120                   # 先頭に目次、各ページの PNG も出す
```

Markdown の先頭に YAML front matter を書くと、判型・書字方向・フォント・目次・柱／ノンブル・数式ハンドラなどを指定できます。

```markdown
---
title: 組版の手引き
author: 組版基盤チーム
paper: A5
writing: vertical
toc: true
---

# はじめに {#sec-intro}

本文。ルビは｜組版《くみはん》のように書ける[^1]。第 {ref:sec-body} 章を参照。

[^1]: 脚注は段の末尾に集まる。
```

見出し・箇条書き・表・画像・脚注・数式（`$…$`）・ルビ・`<!-- pagebreak -->`・`[toc]`・`[index]` に対応しています。
一覧と例は [Markdown → PDF の手引き](https://wamsoft.github.io/jtypeset/markdown/) と
[`samples/markdown/report.md`](samples/markdown/report.md) を参照してください。
フォントを指定しなければ OS のフォント（Windows: 游明朝／游ゴシック、macOS: ヒラギノ、Linux: Noto CJK）を探します。

### Python API

```python
import jtypeset as ts

fonts = ts.FontSet()
fonts.load_file("NotoSerifJP-Regular.otf", "serif-ja")

body = ts.TextStyle(["serif-ja"], 11.0)
seq = ts.PageSequence()
seq.master.size = ts.paper.A5
seq.master.writing_mode = ts.WritingMode.VERTICAL_RL

flow = ts.Flow()
p = ts.Paragraph("吾輩は猫である。名前はまだ無い。", body)
p.annotate(ts.Annotation.ruby(0, 2, "わがはい"))
flow.add_paragraph(p)

pages = ts.FlowLayouter(fonts).layout(flow, seq)
ts.save_pdf(pages, "neko.pdf")
pages[0].save_png("neko_p1.png", dpi=144)
```

段落・見出し・表・画像・目次・索引・脚注・外部オブジェクトの使い方は
[Python ガイド](https://wamsoft.github.io/jtypeset/python_guide/) と、`help(jtypeset)` / 型スタブ（IDE の補完）を参照してください。

## ビルド（C++ ライブラリ・開発者向け）

依存は [vcpkg](https://github.com/microsoft/vcpkg) で入れます（FreeType / HarfBuzz / libunibreak / zlib / stb / doctest）。
フォント層の [glyphware](https://github.com/wamsoft/glyphware) は CMake の FetchContent で取得します（開発中は `GLYPHWARE_DIR` でローカルツリーを指せます）。

```bash
export VCPKG_ROOT=/path/to/vcpkg
make fontdata                                   # テスト・サンプル用の Noto フォントを data/ にダウンロード
make prebuild                                   # cmake --preset（Windows: x64-windows。Linux / macOS の preset もある）
make build                                      # cmake --build
make test                                       # ctest（doctest）

./build/x64-windows/Release/sample_report.exe   # サンプルはリポジトリルートで実行（./data/ を読む）
```

- Python バインディングも一緒にビルド: `make prebuild CMAKEOPT=-DTYPESET_BUILD_PYTHON=ON`（`pip install pybind11` が必要）。
  ビルドツリーの `build/<preset>/python/Release` を `PYTHONPATH` に足すと `import jtypeset` できます
- wheel: `pip wheel . -w dist --no-deps`（scikit-build-core。`VCPKG_ROOT` があれば toolchain は自動で補います）
- MicroTeX（LaTeX 数式）は既定でビルドされ Python 拡張にも入る（`-DTYPESET_HANDLER_MICROTEX=OFF` で外せる）
- リファレンス: `make docs`（Doxygen）、`make pydocs`（型スタブ＋ pdoc）、`make site`（MkDocs でドキュメントサイトを `build/site` に）

サンプル: `sample_inline`（縦横同一文）、`sample_script`（台本）、`sample_novel`（小説 2 段）、`sample_tech`（技術文書）、
`sample_report`（レポート: 目次・採番・図表番号・脚注・索引）、`sample_objects`（外部オブジェクト）。
出力 `output_*.png/.pdf/.svg` はリポジトリルートに出ます。詳細は [ビルド](https://wamsoft.github.io/jtypeset/build/)。

## 技術解説

```
Flow（段落・見出し・表・画像・目次・索引…）
  → page    : ページマスタ・段・流し込み・回り込み・柱・ノンブル・改ページ制御・多パス（採番・参照・目次・索引）
  → inl     : Itemizer → HarfBuzz → Box / Glue / Penalty（JLReq）→ 行分割（Greedy / Knuth–Plass）→ 行
  → dl      : 表示リスト（GlyphRun / Path / Rect / Image / Group）
  → backend : ラスタ（PNG）/ PDF（Identity-H、サブセット、Flate、しおり）/ SVG（defs + use）
  obj       : 外部オブジェクト（数式・グラフ）の差し込み口。関数／外部コマンド、SVG 読み込み
```

- **1 つのエンジンで縦横**: 行内を論理座標（送り方向と、行の中心線からのずれ）で組み、最後に 1 箇所で物理座標に写す。
  縦組みの正立／横倒し（UAX #50）、ルビや圏点の付く側（縦: 右、横: 上）もここで決まる
- **Box / Glue / Penalty**: 和文を「1 文字 1 マス」ではなく TeX と同じ箱・のり・ペナルティの列として解く。
  JLReq の文字クラスとアキ量表から Glue を作り、禁則は Penalty、ぶら下げは幅が負の Penalty、
  追い出しは和字間の伸びで表す。Greedy と Knuth–Plass を選べ、行長は行ごとに変えられる（回り込み・吹き出し）
- **表示リストで分離**: 組版結果は描画命令の列になり、3 つの出力先が同じものを描く。
  グリフ変形（回転・平体長体・斜体）の組み立ても 1 箇所。Inkscape / poppler で描き直して一致を確認している
- **ページ組版**: 段落を段に入る行数だけ組んで残りを次の段へ続ける。orphans / widows、keepWithNext（見出しの巻き取り）、
  段抜きと段の高さ揃えは「ページの再開点から段を縮めて組み直す」試行で解く。見出し番号・図表番号・相互参照・目次・
  索引・総ページ数は 2〜3 パスで安定させる
- **フォント**: glyphware（FreeType + HarfBuzz）。フォントファイルのバイト列から HarfBuzz のフォントを自前に作り、
  フォントユニットで位置を受け取る。PDF は hb-subset でサブセット化し、OS/2 の fsType（埋め込み許可）を確認する。
  minikin と ICU は使わない（UAX #14 は libunibreak、双方向は SheenBidi）

続きは [技術解説](https://wamsoft.github.io/jtypeset/architecture/) と [C++ ガイド](docs/cpp_guide.md)。
経緯・判断は `検討.md` / `設計.md` / `実装.md` に記録しています。

## ライセンス

MIT License（`LICENSE`）。依存: FreeType（FTL）、HarfBuzz（MIT）、SheenBidi（Apache-2.0、glyphware 経由）、
libunibreak（zlib）、zlib、stb（MIT / PD）、doctest（MIT、テストのみ）。Python の任意依存: markdown-it-py（MIT）、
mdit-py-plugins（MIT）、PyYAML（MIT）、Pygments（BSD）。MicroTeX ハンドラは MIT（付属フォントは OFL / Knuth ライセンス）。
