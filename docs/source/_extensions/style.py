# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import annotations

from typing import Any

from pygments.filters import VisibleWhitespaceFilter
from pygments.lexers.compiled import CppLexer, RustLexer
from pygments.lexers.make import CMakeLexer
from sphinx.highlighting import lexers


def setup(app: Any) -> None:
    """Replace tabs with 4 spaces"""
    lexers["C++"] = CppLexer()
    lexers["rust"] = RustLexer()
    lexers["CMake"] = CMakeLexer()

    ws_filter = VisibleWhitespaceFilter(tabs=" ", tabsize=4)
    for lx in lexers.values():
        lx.add_filter(ws_filter)
