# typeset / jtypeset

縦書き・横書きの日本語組版を 1 つのエンジンで行い、同じ組版結果を PDF / SVG / PNG に出すライブラリです。
Python パッケージ **jtypeset** と、Markdown から日本語の PDF を作るコマンド **jtypeset-md** が付いています。

## まず何をしたいか

| したいこと | 読むところ |
|---|---|
| Markdown を書いて PDF にしたい | [Markdown → PDF の手引き](markdown.md) |
| Python から段落・表・画像を組んで PDF にしたい | [Python ガイド](python_guide.md)、[Python リファレンス](python/index.html) |
| C++ から使いたい、組み込みたい | [C++ ガイド](cpp_guide.md)、[C++ リファレンス](cpp/html/index.html) |
| ソースからビルドしたい | [ビルド](build.md) |
| 中の仕組みを知りたい | [技術解説](architecture.md) |

## インストール（Python）

```bash
pip install "jtypeset[md]"          # Markdown → PDF まで
jtypeset-md report.md               # → report.pdf
```

Windows / CPython 3.12 の wheel を用意しています。ほかの環境は [ビルド](build.md) を見てソースから入れてください。

## できること

- JLReq に沿った約物の詰め・禁則・追い込み／追い出し・ぶら下げ・両端揃え、Greedy / Knuth–Plass の行分割
- ルビ（グループ／モノ／熟語）・縦中横・圏点・割注・字取り
- 見出しの採番と PDF のしおり、目次、索引、図表番号と相互参照、脚注（ページごとの番号も可）
- 箇条書き、コードブロック（色付け）、表（自動列幅・colspan / rowspan・ヘッダ繰り返し・ページまたぎ）、画像と回り込み
- 段組・段抜き・最終ページの段揃え、柱・ノンブル、見開きの余白
- LaTeX 数式（同梱の MicroTeX、数式フォントで PDF に埋め込み）、グラフなど外部レンダラの出力（SVG）の差し込み
- PDF はフォントをサブセットで埋め込み、埋め込み許可（fsType）を確認。SVG と PNG も同じ組版結果

## 出力例

同じ Markdown（[report.md](https://github.com/wamsoft/jtypeset/blob/main/samples/markdown/report.md)）を組んだ PDF をそのまま見られます:

- 横組み A4: [report.pdf](samples/report.pdf)
- 縦組み A5: [report_vertical.pdf](samples/report_vertical.pdf)（同じファイルを `--vertical --paper A5` で）

1 ページ目の画像と作り方は [サンプル（Markdown と PDF）](samples.md) に。C++ のサンプル（台本・小説・技術文書・レポート）は
リポジトリの `samples/` にあります。
