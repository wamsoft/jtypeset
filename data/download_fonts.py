#!/usr/bin/env python3
"""
typeset のサンプル・テストで使う Noto フォントをダウンロードして data/ に配置する。

使い方:
    python3 data/download_fonts.py
"""

import io
import os
import sys
import zipfile
import urllib.request
import urllib.error

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = SCRIPT_DIR

# (出力ファイル名, URL, zip内のパス or None(直接ダウンロード))
FONTS = [
    # 欧州言語（固定ウェイト）
    (
        "NotoSans-Regular.ttf",
        "https://github.com/notofonts/latin-greek-cyrillic/releases/download/NotoSans-v2.015/NotoSans-v2.015.zip",
        "NotoSans/unhinted/ttf/NotoSans-Regular.ttf",
    ),
    # 欧州言語 Serif（固定ウェイト）
    (
        "NotoSerif-Regular.ttf",
        "https://github.com/notofonts/latin-greek-cyrillic/releases/download/NotoSerif-v2.015/NotoSerif-v2.015.zip",
        "NotoSerif/unhinted/ttf/NotoSerif-Regular.ttf",
    ),
    # 欧文の太字・斜体（ウェイト／斜体の face 選択のテスト用。Regular と同じ zip）
    (
        "NotoSerif-Bold.ttf",
        "https://github.com/notofonts/latin-greek-cyrillic/releases/download/NotoSerif-v2.015/NotoSerif-v2.015.zip",
        "NotoSerif/unhinted/ttf/NotoSerif-Bold.ttf",
    ),
    (
        "NotoSerif-Italic.ttf",
        "https://github.com/notofonts/latin-greek-cyrillic/releases/download/NotoSerif-v2.015/NotoSerif-v2.015.zip",
        "NotoSerif/unhinted/ttf/NotoSerif-Italic.ttf",
    ),
    # 日本語（固定ウェイト）
    (
        "NotoSansJP-Regular.otf",
        "https://github.com/notofonts/noto-cjk/releases/download/Sans2.004/16_NotoSansJP.zip",
        "NotoSansJP-Regular.otf",
    ),
    # Serif フォント（日本語・固定ウェイト）
    (
        "NotoSerifJP-Regular.otf",
        "https://github.com/notofonts/noto-cjk/releases/download/Serif2.003/07_NotoSerifCJKjp.zip",
        "OTF/Japanese/NotoSerifCJKjp-Regular.otf",
    ),
    # 日本語 Serif の太字（ウェイト選択のテスト用。Regular と同じ zip）
    (
        "NotoSerifJP-Bold.otf",
        "https://github.com/notofonts/noto-cjk/releases/download/Serif2.003/07_NotoSerifCJKjp.zip",
        "OTF/Japanese/NotoSerifCJKjp-Bold.otf",
    ),
    # 双方向テキスト（アラビア文字・ヘブライ文字）のテスト用
    (
        "NotoSansArabic-Regular.ttf",
        "https://github.com/notofonts/arabic/releases/download/NotoSansArabic-v2.013/NotoSansArabic-v2.013.zip",
        "NotoSansArabic/unhinted/ttf/NotoSansArabic-Regular.ttf",
    ),
    (
        "NotoSansHebrew-Regular.ttf",
        "https://github.com/notofonts/hebrew/releases/download/NotoSansHebrew-v3.001/NotoSansHebrew-v3.001.zip",
        "NotoSansHebrew/unhinted/ttf/NotoSansHebrew-Regular.ttf",
    ),
    # バリアブルフォント（wdth / wght 軸）のテスト用。NotoSans-Regular と同じ zip
    (
        "NotoSans-Variable.ttf",
        "https://github.com/notofonts/latin-greek-cyrillic/releases/download/NotoSans-v2.015/NotoSans-v2.015.zip",
        "NotoSans/unhinted/variable-ttf/NotoSans[wdth,wght].ttf",
    ),
    # カラー絵文字（CBDT ビットマップ）。絵文字のフォールバック用
    (
        "NotoColorEmoji.ttf",
        "https://github.com/googlefonts/noto-emoji/raw/main/fonts/NotoColorEmoji.ttf",
        None,
    ),
    # カラー絵文字（COLR v1 のベクター版）。PDF / SVG ではこちらの方が軽くきれい
    (
        "Noto-COLRv1.ttf",
        "https://github.com/googlefonts/noto-emoji/raw/main/fonts/Noto-COLRv1.ttf",
        None,
    ),
]


_DOWNLOAD_CACHE: dict = {}


def download_url(url: str) -> bytes:
    """URL からデータをダウンロードする（同じ zip は 1 回だけ）"""
    if url in _DOWNLOAD_CACHE:
        return _DOWNLOAD_CACHE[url]
    print(f"  Downloading: {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=120) as resp:
        data = resp.read()
    _DOWNLOAD_CACHE[url] = data
    return data


def extract_from_zip(data: bytes, inner_path: str) -> bytes:
    """zip データ内の指定パスのファイルを取得する"""
    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        # 完全一致を試す
        names = zf.namelist()
        if inner_path in names:
            return zf.read(inner_path)
        # ファイル名のみで検索（zip内のディレクトリ構造が異なる場合）
        basename = os.path.basename(inner_path)
        candidates = [n for n in names if n.endswith("/" + basename) or n == basename]
        if candidates:
            # 最短パスを優先
            candidates.sort(key=len)
            print(f"  Found in zip: {candidates[0]}")
            return zf.read(candidates[0])
        raise FileNotFoundError(
            f"'{inner_path}' (or '{basename}') not found in zip. "
            f"Available: {[n for n in names if n.endswith('.ttf') or n.endswith('.otf')]}"
        )


# ハイフネーションのパターン（TeX の hyph-utf8）。欧文のハイフネーションのテスト・サンプル用。
# ライセンスがフォントと別なので同梱せず、ここで取る
PATTERNS = [
    (
        "hyph-en-us.tex",
        "https://raw.githubusercontent.com/hyphenation/tex-hyphen/master/hyph-utf8/tex/generic/hyph-utf8/patterns/tex/hyph-en-us.tex",
    ),
]


def download_patterns():
    """ハイフネーションのパターンを取る（失敗しても致命的ではない）"""
    for out_name, url in PATTERNS:
        out_file = os.path.join(DATA_DIR, out_name)
        if os.path.exists(out_file):
            print(f"[SKIP] {out_name} (already exists)")
            continue
        print(f"[GET]  {out_name}")
        try:
            data = download_url(url)
            with open(out_file, "wb") as f:
                f.write(data)
            print(f"  -> Saved {out_name} ({len(data) / 1024:.0f} KB)")
        except Exception as e:  # noqa: BLE001
            print(f"  !! {out_name}: {e}")


def main():
    os.makedirs(DATA_DIR, exist_ok=True)

    success = 0
    failed = 0

    download_patterns()

    for out_name, url, zip_path in FONTS:
        out_file = os.path.join(DATA_DIR, out_name)
        if os.path.exists(out_file):
            size_mb = os.path.getsize(out_file) / (1024 * 1024)
            print(f"[SKIP] {out_name} (already exists, {size_mb:.1f} MB)")
            success += 1
            continue

        print(f"[GET]  {out_name}")
        try:
            data = download_url(url)
            if zip_path is not None:
                font_data = extract_from_zip(data, zip_path)
            else:
                font_data = data
            with open(out_file, "wb") as f:
                f.write(font_data)
            size_mb = len(font_data) / (1024 * 1024)
            print(f"  -> Saved {out_name} ({size_mb:.1f} MB)")
            success += 1
        except Exception as e:
            print(f"  ERROR: {e}", file=sys.stderr)
            failed += 1

    print(f"\nDone: {success} succeeded, {failed} failed")
    return 1 if failed > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
