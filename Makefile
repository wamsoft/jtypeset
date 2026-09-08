SHELL = /bin/bash

# fontdata ターゲットは VCPKG_ROOT 不要
ifeq ($(filter fontdata,$(MAKECMDGOALS)),)
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

.PHONY: prebuild build clean test fontdata docs pydocs

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
	PYTHONPATH=$(PYPKG) pybind11-stubgen typeset._jtypeset -o python --ignore-all-errors
	cp -r python/jtypeset/_jtypeset $(PYPKG)/typeset/
	mkdir -p build/docs
	PYTHONPATH=$(PYPKG) pdoc typeset -o build/docs/python --no-show-source
