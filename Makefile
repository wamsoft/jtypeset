SHELL = /bin/bash

# ビルドを伴わないターゲット（fontdata / サンプル実行 / ドキュメント）は VCPKG_ROOT 不要
ifeq ($(filter fontdata samples samples-docs samples-md,$(MAKECMDGOALS)),)
ifeq ($(VCPKG_ROOT),)
$(error Variables VCPKG_ROOT not set correctly.)
endif
endif

# Detect OS and set default PRESET accordingly
ifeq ($(OS),Windows_NT)
	PRESET?=x64-windows
else
	UNAME_S := $(shell uname -s)
	UNAME_M := $(shell uname -m)
	ifeq ($(UNAME_S),Linux)
		ifeq ($(UNAME_M),aarch64)
			PRESET?=arm64-linux
		else
			PRESET?=x64-linux
		endif
	else ifeq ($(UNAME_S),Darwin)
		ifeq ($(UNAME_M),arm64)
			PRESET?=arm64-osx
		else
			PRESET?=x64-osx
		endif
	else
		PRESET?=x64-windows
	endif
endif

BUILD_TYPE?=Release
# 開発中はローカルの glyphware ツリーを指す。空にすると FetchContent で取る
GLYPHWARE_DIR?=
CMAKEOPT?=
ifneq ($(GLYPHWARE_DIR),)
CMAKEOPT+=-DGLYPHWARE_DIR=$(GLYPHWARE_DIR)
endif

BUILD_PATH=$(shell cmake --preset $(PRESET) -N | grep BUILD_DIR | sed 's/.*BUILD_DIR="\(.*\)"/\1/')

.PHONY: prebuild build clean test fontdata docs pydocs site samples samples-docs samples-md

all: build

# cmake 処理実行（CMAKEOPT / GLYPHWARE_DIR で引数追加）
prebuild:
	cmake --preset $(PRESET) $(CMAKEOPT)

build:
	cmake --build $(BUILD_PATH) --config $(BUILD_TYPE)

clean:
	cmake --build $(BUILD_PATH) --config $(BUILD_TYPE) --target clean

test:
	ctest --test-dir $(BUILD_PATH) -C $(BUILD_TYPE) --output-on-failure

# テスト用 Noto フォントをダウンロード
fontdata:
	python3 data/download_fonts.py

# C++ リファレンス（Doxygen。DOXYGEN=path で実行ファイルを指定できる）→ build/docs/cpp/html/index.html
DOXYGEN?=doxygen
docs:
	$(DOXYGEN) docs/Doxyfile

# Python リファレンス: 型スタブ（_jtypeset.pyi）と pdoc の HTML → build/docs/python/index.html
# 事前に pip install pdoc pybind11-stubgen。ビルド済みの python パッケージを読む
PYPKG=$(BUILD_PATH)/python/$(BUILD_TYPE)
pydocs:
	PYTHONPATH=$(PYPKG) pybind11-stubgen jtypeset._jtypeset -o python --ignore-all-errors
	cp -r python/jtypeset/_jtypeset $(PYPKG)/jtypeset/
	mkdir -p build/docs
	PYTHONPATH=$(PYPKG) pdoc jtypeset -o build/docs/python --no-show-source

# ドキュメントサイト（MkDocs + Doxygen + pdoc）→ build/site。docs と pydocs を先に実行しておく
site:
	mkdocs build
	mkdir -p build/site/cpp build/site/python
	cp -r build/docs/cpp/html build/site/cpp/
	cp -r build/docs/python/. build/site/python/

# C++ のサンプルをまとめて実行する（リポジトリルートで動かす前提。出力はルートに出る）
samples:
	@for s in sample_dl sample_inline sample_script sample_novel sample_tech sample_report sample_objects 	          sample_text_style sample_game sample_intl; do 	    exe=$$(find build/$(PRESET) -name "$$s" -o -name "$$s.exe" | head -1); 	    if [ -n "$$exe" ]; then echo "--- $$s"; "$$exe" || exit 1; else echo "--- $$s (not built)"; fi; 	done

# ドキュメント用のサンプル画像（C++ サンプルの PNG を縮小して docs/samples へ）
samples-docs: samples
	magick output_text_style.png -resize 720x -strip docs/samples/sample_text_style.png
	magick output_game.png -resize 720x -strip docs/samples/sample_game.png
	magick output_intl.png -resize 720x -strip docs/samples/sample_intl.png

# ドキュメント用のサンプル PDF（Markdown と対になるもの）を再生成する。ビルド済みの python パッケージと data/ のフォントを使う
samples-md:
	PYTHONPATH=$(PYPKG) python -m jtypeset.md samples/markdown/report.md -o docs/samples/report.pdf --png 90
	PYTHONPATH=$(PYPKG) python -m jtypeset.md samples/markdown/report.md -o docs/samples/report_vertical.pdf --vertical --paper A5 --png 90
	rm -f docs/samples/report_p[2-9]*.png docs/samples/report_vertical_p[2-9]*.png
