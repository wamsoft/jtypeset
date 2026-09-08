# 想定

台本、小説、絵入り技術文章、漫画用の吹き出し処理、などを想定した
縦書き対応した、シンプルな日本語組版ライブラリを C++ で構築したい

・もっぱらツール作成に利用
・ゲームエンジンなどにも組み込めるようなもの
　ベクターデータでの処理、SVG出力、PDF出力も視野にいれる

いままで作っているライブラリ資産で活用できるものは活用

d:/test/richtext/  richtext用のライブラリ。想定はパラグラフレベル。
横書き組版には minikin 、縦書きは独自処理

d:/work/kirikiri/glyphware
グリフ処理をキャッシュまで含めて総合的に扱えるようにまとめたライブラリ。
中身は FreeType + Harfbuzz + icu の一部

必要になってくるもの
・ルビ処理、日本語組版全般
・罫線処理、ページ処理、画像差し込み、まわりこみなどなど
必要そうな仕様を、既存のライブラリやドキュメントなどから設計を考える必要あり

C++のライブラリ + python でのバインドができるとよい

---

# typeset — 現状

縦書き・横書きの日本語組版（JLReq 水準）を同じエンジンで行い、同一の組版結果をラスタ・PDF・SVG へ出す
C++17 ライブラリ。フォント層は [glyphware](https://github.com/wamsoft/glyphware)（FreeType + HarfBuzz）、
richtext の縦組みエンジンを書字方向非依存に一般化して移植したものが組版コア。minikin / ICU は使わない。

```
Document / Flow（段落・見出し・ラベル付き段落・画像・表・罫線・セクション）
  → page   : ページマスタ・段・流し込み・回り込み・柱・ノンブル・改ページ制御
  → inl    : Itemizer → HarfBuzz → Box/Glue/Penalty（JLReq）→ 行分割（Greedy / Knuth–Plass）→ LineBox
  → dl     : 表示リスト（GlyphRun / Path / Rect / Image / Group）
  → backend: ラスタ（glyphware マスク合成）/ PDF（Identity-H、グリフ ID 直書き）/ SVG（defs + use）
  obj      : 外部オブジェクト（数式・グラフ）の差し込み口。ハンドラ登録（関数／外部コマンド）と SVG 読み込み
```

## ビルド

```bash
# VCPKG_ROOT を設定しておく。テスト用フォントは make fontdata（または richtext/data からコピー）
make fontdata
make prebuild GLYPHWARE_DIR=d:/work/kirikiri/glyphware   # 開発中はローカルツリー。省略すると GitHub から FetchContent
make build
make test

# サンプル（リポジトリルートで実行。./data/ のフォントを読む）
./build/x64-windows/Release/sample_inline.exe   # 縦横同一文（ルビ・縦中横・圏点・割注・字取り）
./build/x64-windows/Release/sample_script.exe   # 台本（A5 縦組み、名前欄＋本文、柱・ノンブル）
./build/x64-windows/Release/sample_novel.exe    # 小説（B6 縦組み 2 段、章見出し、ルビ）
./build/x64-windows/Release/sample_tech.exe     # 技術文書（A4 横組み、回り込みの図、表、2 段組）
./build/x64-windows/Release/sample_report.exe   # レポート（目次、見出し採番、図表番号と相互参照、箇条書き、コード、しおり）
./build/x64-windows/Release/sample_objects.exe  # 外部オブジェクト（関数ハンドラの分数、外部コマンドのグラフ、SVG、式番号）

# MicroTeX（LaTeX 数式）ハンドラ: 任意ターゲット。tinyxml2 は vcpkg の feature で入る
make prebuild GLYPHWARE_DIR=d:/work/kirikiri/glyphware CMAKEOPT="-DTYPESET_HANDLER_MICROTEX=ON -DVCPKG_MANIFEST_FEATURES=microtex"
make build
./build/x64-windows/handlers/microtex/Release/sample_microtex.exe   # 行内・別行立ての数式、式番号、\text{} の和文
```

## できること（2026-09 時点）

- 縦組み・横組みの JLReq 組版（約物の詰め・禁則・追い込み／追い出し・ぶら下げ・両端揃え、Greedy / Knuth–Plass）
- ルビ（グループ／モノ／熟語）・縦中横・圏点・割注・字取り、行内画像
- 段落・見出し（自動採番、PDF しおり）・箇条書き・コードブロック（背景・空白保持）・罫線・ラベル付き段落（台本の名前欄）
- 画像（配置・回り込み・キャプション）、表（自動列幅、colspan / rowspan、セルの縦位置、ヘッダ繰り返し、段より高い行の分割）
- ページマスタ（判型・内外余白・段組・柱・ノンブル）、改ページ制御（orphans / widows / keepWithNext / keepTogether）、
  段の途中の段抜きと最終ページの段揃え、目次、索引（`{index:よみ|用語}`）、図表番号と相互参照（`{ref:label}` `{page:label}`）、
  脚注（段末に集める）
- 外部オブジェクトの差し込み: 本文に「ハンドラ名＋ソース」を書き、登録した関数／外部コマンドが返す SVG や描画命令を
  行内（ベースライン揃え）・別行立て（式番号・`{ref:}` 参照）に置く。数式レンダラ（MathJax / dvisvgm / Typst）や
  グラフ（matplotlib）を本体に依存を足さずにつなげる。LaTeX 数式は `handlers/microtex/`（MicroTeX、任意ターゲット）で
  数式フォントのグリフとして PDF に埋め込まれる
- 出力: ラスタ（PNG）、PDF（Identity-H・hb-subset・Flate・画像・しおり）、SVG。Python バインディング

## Python

```bash
# 開発ツリーから
pip install pybind11
make prebuild GLYPHWARE_DIR=d:/work/kirikiri/glyphware CMAKEOPT=-DTYPESET_BUILD_PYTHON=ON
make build
PYTHONPATH=build/x64-windows/python/Release python python/examples/script.py    # typeset/ パッケージ（_typeset 拡張 + 純 Python）
PYTHONPATH=build/x64-windows/python/Release python python/examples/objects.py   # Python 関数／外部コマンドをオブジェクトのハンドラに

# wheel（scikit-build-core。VCPKG_ROOT を設定しておけば toolchain は CMakeLists が補う。glyphware は FetchContent）
pip wheel . -w dist --no-deps
pip install dist/typeset-*.whl
```

### Markdown → PDF（`typeset.md`）

```bash
pip install "typeset[md]"            # markdown-it-py / mdit-py-plugins / PyYAML
pip install "typeset[md,highlight]"  # + Pygments（コードブロックの色付け。無くても単色で組める）
typeset-md report.md                 # report.pdf
typeset-md report.md --vertical --paper A5 --font fonts/mincho.otf --toc
python -m typeset.md report.md -o out.pdf --png 120 --math mathtext   # 数式は matplotlib の mathtext（任意）
```

先頭の YAML front matter で題名・著者・判型（A4/A5/B5/B6/文庫/新書/`148x210mm`）・書字方向・段数・余白・フォント・
本文サイズ・行送り・目次・見出し採番・柱／ノンブル・数式ハンドラ（`mathtext` か `command: <cmd>`）を指定できる
（`python/typeset/md/convert.py` の `Options`）。CLI の指定は front matter より優先。

対応する記法: 見出し（採番・PDF しおり・`{#label}` → `{ref:label}` `{page:label}`）、段落（一字下げ・両端揃え）、
箇条書き／番号付き（入れ子）、コードブロック（背景・空白保持・タブ展開。Pygments があれば言語名で色付け）、
表（GFM。直後の「表: 〜」がキャプション）、
画像（単独の段落なら図番号付きの図、文中なら行内画像）、脚注 `[^1]`、引用、水平線、強調・斜体・行内コード、
リンク（URL は脚注へ）、数式 `$…$` `$$…$$ (label)`、ルビ（`｜漢字《かんじ》` `漢字《かんじ》` `{漢字|かんじ}`）、
`<!-- pagebreak -->` `<!-- columnbreak -->` `<!-- columns: 2 -->` `[toc]` `[index]`（本文の `{index:よみ|用語}` を集める）。
例: `samples/markdown/report.md`。

フォントを指定しなければ `data/` の Noto、無ければ OS のフォント（Windows: 游明朝／游ゴシック、macOS: ヒラギノ、
Linux: Noto CJK）を探す。TTC はサブセット化して埋め込む。OS/2 の fsType（Restricted License、ビットマップのみ、
サブセット不可）を見て、埋め込めないフォントは警告を出して埋め込まない。

```python
import typeset as ts
fonts = ts.FontSet(); fonts.load_file("data/NotoSerifJP-Regular.otf", "serif-ja")
body = ts.TextStyle(["serif-ja"], 11.0)
seq = ts.PageSequence(); seq.master.size = ts.paper.A5; seq.master.writing_mode = ts.WritingMode.VERTICAL_RL
flow = ts.Flow()
p = ts.Paragraph("吾輩は猫である。名前はまだ無い。", body); p.annotate(ts.Annotation.ruby(0, 2, "わがはい"))
flow.add_paragraph(p)
pages = ts.FlowLayouter(fonts).layout(flow, seq)
ts.save_pdf(pages, "out.pdf"); pages[0].save_png("out.png", dpi=144)
```

## ドキュメント

| ファイル | 内容 |
|---|---|
| `検討.md` | 既存資産の棚卸し、設計判断、参照仕様、フェーズ計画、決定事項 |
| `設計.md` | ディレクトリ・名前空間・型・処理の流れ（Phase 0〜2 の具体化） |
| `実装.md` | フェーズ別の実装進捗・確認結果・積み残し |

## ライセンス

typeset 本体は MIT（予定）。依存: FreeType（FTL）、HarfBuzz（MIT）、SheenBidi（Apache-2.0、glyphware 経由）、
libunibreak（zlib）、zlib、stb（MIT / PD）、doctest（MIT、テストのみ）。
