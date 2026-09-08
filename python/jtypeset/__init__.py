"""
jtypeset — 縦書き・横書きの日本語組版ライブラリ typeset の Python パッケージ（C++ コア + pybind11）

`typeset._jtypeset` 拡張モジュールの公開名をそのまま再エクスポートする。
Markdown → PDF は `jtypeset.md`（`pip install jtypeset[md]`）。
"""
from . import _jtypeset as _ext
from ._jtypeset import *  # noqa: F401,F403

# 拡張モジュールのサブモジュール・定数（`paper` など）も見えるように
__all__ = []
for _name in dir(_ext):
    if not _name.startswith("_"):
        if _name not in globals():
            globals()[_name] = getattr(_ext, _name)
        __all__.append(_name)
__all__.append("md")
del _name

__version__ = "0.1.0"


def microtex_res_dir() -> str:
    """パッケージに同梱した MicroTeX の数式フォント（res）の場所。ObjectRegistry.add_microtex の res_dir に渡す"""
    import os
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), "microtex_res")
