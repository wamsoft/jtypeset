# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 概要

縦書き・横書きの日本語組版（JLReq 水準）を 1 つのエンジンで行い、同一の組版結果をラスタ / PDF / SVG へ出す
C++17 ライブラリ。用途は台本・小説・技術文書などのツール作成（Python バインディングあり）。
フォント層は glyphware（FreeType + HarfBuzz）。richtext の縦組みエンジンを書字方向非依存に一般化して移植した。
**minikin と ICU は使わない**（UAX #14 は libunibreak、双方向は SheenBidi、スクリプト判定は HarfBuzz）。

## ビルド

`VCPKG_ROOT` を設定しておく。glyphware は開発中はローカルツリー（`GLYPHWARE_DIR`）、未指定なら GitHub から FetchContent。

```bash
make fontdata                                            # テスト用 Noto フォント → data/
make prebuild GLYPHWARE_DIR=d:/work/kirikiri/glyphware   # CMakeOPT=-DTYPESET_BUILD_PYTHON=ON で Python も
make build                                               # BUILD_TYPE=Debug も可
make test                                                # ctest（doctest。リポジトリルートで動く前提）
./build/x64-windows/Release/sample_tech.exe              # サンプルは必ずリポジトリルートで実行（./data/ を読む）
```

Bash ツールから直接叩く場合: `cmake --build build/x64-windows --config Release`、
テストは `./build/x64-windows/tests/Release/typeset_tests.exe`（フォントが無いテストは skip する）。

## レイヤー構造（下から上へ）

```
font/      FontSet            glyphware の包み。family 列＋文字カバレッジでフォールバック解決。
                              シェイピング用 hb_font はフォントのバイト列から OT funcs で自前に作る
                              （glyphware の Face::hb() は hb-ft でピクセルサイズに縛られるので使わない）
text/      CharClass          JLReq 附属書 A の文字クラス。ASCII の約物は欧文扱い
           SpacingTable       JLReq 表 3 のアキ量（自然値・伸び・縮み = TeX の glue）
           orientation        UAX #50（縦組みの正立／横倒し）
           line_break         libunibreak（UAX #14）。欧文単語内を割らないためだけに使う
inl/       shaper             Itemizer（スタイル・face・向き）＋ HarfBuzz。正立は TTB、他は LTR
           item_builder       クラスタ列＋注記 → Box / Glue / Penalty（禁則・ぶら下げ・ルビ・縦中横・圏点・割注・字取り）
           line_breaker       Greedy / Knuth–Plass。行長は LineShapeProvider::at(lineIndex) で行ごと（\parshape）
           paragraph          Paragraph → ParagraphFragment（LineBox 列）。charStart からの再開と maxLines
block/     Block              段落・見出し・罫線・ラベル付き段落・画像・表・セクション、BlockStyle（orphans/widows/keepWithNext/改ページ）
page/      PageMaster/Region  判型・余白・段。Region は排除領域（回り込み）を持ち RegionLineShape が行の形を返す
           FlowLayouter       Flow をページ列へ流し込む。柱・ノンブル（{page} {pages} {title}）
dl/        DisplayList        GlyphRun / Path / Rect / Image / Group。レイアウトと backend の分割線
           glyph_transform    グリフ固有の変形（回転・平体長体・斜体）。3 backend で共有
backend/   raster             glyphware のカバレッジマスク＋自前合成。パスは自前スキャンライン AA。PNG 出力
           pdf_writer         Identity-H でグリフ ID 直書き、hb-subset でサブセット化、Flate 圧縮、画像 XObject
           svg_writer         グリフは <defs>+<use>、画像は data URI
image/     stb_image 読み込み、PNG エンコード、base64
python/    pybind11 モジュール（psdparse と同じ構成）
```

## 設計上の約束（変えるときは 設計.md も直す）

- 文書内部の単位は **pt**。座標はページ左上原点・y-down。ラスタは dpi/72 を掛ける
- 行内は論理座標 **inline_（送り）/ block（行の中心線からのずれ。縦組みは右が正、横組みは下が正）**で組み、
  物理化は `inl::emitParagraph` / `toPhysical` の 1 箇所。注記の付く側は `annotationSide()`（縦: 右、横: 上）
- ブロック軸は「**em box の中心＝行の中心線**」。横組みのベースラインは第一候補フォントの ascender/descender から
  `baselineOffset()` で決め、フォールバック先も同じベースラインに乗せる
- 和文組版は「1 文字＝1em の箱」ではなく **Box / Glue / Penalty 列を解く**。禁則は Glue の直前の Penalty(∞)、
  ぶら下げは幅が負の Penalty、和字間には `kanjiSkipStretch` の伸び（追い出し用）
- 揃えない段落（ragged）では縮みを使わずに収まりを判定する（TeX の ragged-right と同じ）
- 組版結果は表示リストで切る。backend は組版の知識を持たない。グリフ変形の組み立ては `glyph_transform.hpp` のみ
- PDF: `Tm` にフォントサイズを入れない（`Tf` が掛ける）。y-down → y-up は Y 反転で共役（シアーの符号が入れ替わる）。
  縦組みでも Identity-V は使わない
- Windows の `windows.h` を含むサンプルでは `small` など Windows のマクロ名を変数に使わない

## テストとサンプル

- `tests/` は doctest。組版結果は数値で検算する（禁則違反 0、両端揃えの行長一致、グリフが版面内、ヘッダ行の繰り返し等）。
  フォントは `data/` から読むので `WORKING_DIRECTORY` はリポジトリルート
- `samples/sample_inline`（縦横同一文）、`sample_script`（台本）、`sample_novel`（小説 2 段）、`sample_tech`（技術文書：回り込み・表・2 段組）、
  `sample_dl`（表示リスト直書き）。出力 `output_*.png/.pdf/.svg` はリポジトリルートに出る（gitignore 済み）
- 3 backend の一致確認は Inkscape で SVG / PDF（`--pdf-poppler`）を PNG にして ImageMagick `compare -metric RMSE`。
  外接矩形が 1〜2px 以内、RMSE 数%（AA 差）なら一致とみなす

## 参考ドキュメント

- `検討.md` — 既存資産（richtext / glyphware）の棚卸し、設計判断、参照仕様、フェーズ計画、決定事項
- `設計.md` — 型・処理の流れの具体化
- `実装.md` — フェーズ別の進捗・確認結果・積み残し（作業を進めたらここを更新する）
