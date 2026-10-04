# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Exclusive preparation of owned cache state; readers never adopt an incomplete writer."""

from __future__ import annotations

import os
from contextlib import contextmanager
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Iterator
    from pathlib import Path


@contextmanager
def exclusive_cache(path: Path) -> Iterator[None]:
    """Claim a writer file atomically; a crash-leftover claim requires explicit inspection."""
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        stream = path.open("x", encoding="utf-8")
    except FileExistsError as error:
        msg = f"Cache writer already owns {path}; inspect the claim before retrying"
        raise RuntimeError(msg) from error
    try:
        with stream:
            stream.write(str(os.getpid()))
            stream.flush()
            yield
    finally:
        path.unlink()
