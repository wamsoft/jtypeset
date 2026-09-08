"""
typeset — 縦書き・横書きの日本語組版ライブラリ（C++ コア + pybind11）

`typeset._typeset` 拡張モジュールの公開名をそのまま再エクスポートする。
Markdown → PDF は `typeset.md`（`pip install typeset[md]`）。
"""
from . import _typeset as _ext
from ._typeset import *  # noqa: F401,F403

# 拡張モジュールのサブモジュール・定数（`paper` など）も見えるように
for _name in dir(_ext):
    if not _name.startswith("_") and _name not in globals():
        globals()[_name] = getattr(_ext, _name)
del _name

__version__ = "0.1.0"
