#!/usr/bin/env bash
# GCC で全ソースを -fsyntax-only にかけ、MSVC では通ってしまう移植性の問題（ヘッダの include 漏れなど）を拾う。
# Windows では WSL から:  wsl -d Ubuntu -- bash scripts/gcc_syntax_check.sh
# 依存ヘッダは vcpkg のビルドツリーと glyphware のローカルツリーを使う（環境変数で差し替え可）。
set -u
cd "$(dirname "$0")/.."
VCPKG_INC=${VCPKG_INC:-build/x64-windows/vcpkg_installed/x64-windows-static/include}
GLYPHWARE=${GLYPHWARE:-/mnt/d/work/kirikiri/glyphware}
INC="-I include -I src -I $VCPKG_INC -I $VCPKG_INC/harfbuzz -I $GLYPHWARE/include -I $GLYPHWARE/src"
status=0
for f in $(find src tests samples -name "*.cpp" | sort); do
    out=$(g++ -std=c++17 -fsyntax-only -Wall -Wno-unknown-pragmas $INC "$f" 2>&1 | grep -E "error|warning: unused" | head -8)
    if [ -n "$out" ]; then
        echo "== $f"
        echo "$out"
        status=1
    fi
done
exit $status
