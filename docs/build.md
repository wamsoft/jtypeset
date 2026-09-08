# ビルド

## 依存

- C++17 コンパイラ（MSVC 2022 / GCC 9+ / Clang）、CMake 3.24+、Ninja（preset が使う）
- [vcpkg](https://github.com/microsoft/vcpkg): FreeType / HarfBuzz（subset 含む）/ libunibreak / zlib / stb / doctest を
  `vcpkg.json`（manifest）から入れる。`VCPKG_ROOT` を設定しておく
- [glyphware](https://github.com/wamsoft/glyphware)（FreeType + HarfBuzz の包み）: CMake の FetchContent で自動取得。
  開発中は `GLYPHWARE_DIR=/path/to/glyphware` でローカルツリーを指せる
- Python バインディング: Python 3.9+ と `pip install pybind11`
- テスト・サンプル用フォント: `make fontdata`（Noto Serif/Sans JP、Noto Serif/Sans、絵文字の Noto-COLRv1 / NotoColorEmoji を `data/` にダウンロード）

## 手順

```bash
export VCPKG_ROOT=/path/to/vcpkg
make fontdata
make prebuild                              # cmake --preset <OS に応じた preset>
make build                                 # Release。BUILD_TYPE=Debug も可
make test                                  # ctest（doctest。リポジトリルートで動く）
```

preset は `CMakePresets.json` にあります（`x64-windows`、`x86-windows`、`x64-linux`、`arm64-linux`、`x64-osx`、`arm64-osx`。
Windows は静的 triplet `x64-windows-static`）。`PRESET=...` で選べます。

直接 CMake を叩くなら:

```bash
cmake --preset x64-windows -DGLYPHWARE_DIR=d:/work/glyphware -DTYPESET_BUILD_PYTHON=ON
cmake --build build/x64-windows --config Release
./build/x64-windows/tests/Release/typeset_tests.exe
```

## オプション

| CMake オプション | 既定 | 内容 |
|---|---|---|
| `TYPESET_BUILD_SAMPLES` | ON（トップレベルのとき） | `samples/` の実行ファイル |
| `TYPESET_BUILD_TESTS` | ON（トップレベルのとき） | doctest |
| `TYPESET_BUILD_PYTHON` | OFF | pybind11 モジュール `jtypeset._jtypeset`。`python/<Config>/jtypeset/` にパッケージがまとまる |
| `TYPESET_HANDLER_MICROTEX` | ON | MicroTeX（LaTeX 数式）ハンドラとサンプル。MicroTeX は FetchContent、tinyxml2 は vcpkg。Python 拡張にもリンクされ、数式フォント（res、2MB）がパッケージに同梱される |
| `GLYPHWARE_DIR` | 空 | glyphware のローカルツリー。空なら FetchContent |

## Python パッケージ（jtypeset）

```bash
# 開発ツリーから使う
make prebuild CMAKEOPT=-DTYPESET_BUILD_PYTHON=ON
make build
PYTHONPATH=build/x64-windows/python/Release python -c "import jtypeset; print(jtypeset.__version__)"

# wheel を作る（scikit-build-core。VCPKG_ROOT があれば toolchain は CMakeLists が補う）
pip wheel . -w dist --no-deps
pip install dist/jtypeset-*.whl
```

Windows では `x64-windows-static-md`（静的ライブラリ＋動的 CRT）で wheel を作ります（pyproject の override）。
DLL を同梱しなくてよく、Python 本体と CRT が一致します。

## ドキュメントの生成

```bash
make docs        # C++ リファレンス（Doxygen）→ build/docs/cpp/html。DOXYGEN=path で実行ファイル指定
make pydocs      # 型スタブ（pybind11-stubgen）と Python リファレンス（pdoc）→ build/docs/python
make site        # 上の 2 つとこのサイト（MkDocs）を build/site にまとめる
```

`pip install mkdocs pdoc pybind11-stubgen` が必要です。Doxygen は OS のパッケージか公式のバイナリを入れてください。

## 別のプロジェクトから使う

```cmake
include(FetchContent)
FetchContent_Declare(typeset GIT_REPOSITORY https://github.com/wamsoft/jtypeset.git GIT_TAG main)
FetchContent_MakeAvailable(typeset)
target_link_libraries(myapp PRIVATE typeset)
```

vcpkg の依存（FreeType / HarfBuzz / libunibreak / zlib / stb）は利用側の manifest にも書いてください。
glyphware は typeset が FetchContent で取ります（利用側に `glyphware` ターゲットがあればそれを使います）。
