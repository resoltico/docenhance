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


class WriterStateUnknownError(RuntimeError):
    """The writer may still be active; its claim requires inspection before reuse."""


@contextmanager
def exclusive_cache(path: Path) -> Iterator[None]:
    """Claim a writer file atomically; a crash-leftover claim requires explicit inspection."""
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        stream = path.open("x", encoding="utf-8")
    except FileExistsError as error:
        msg = f"Cache writer already owns {path}; inspect the claim before retrying"
        raise RuntimeError(msg) from error
    retain = False
    try:
        with stream:
            stream.write(str(os.getpid()))
            stream.flush()
            yield
    except WriterStateUnknownError:
        retain = True
        raise
    finally:
        if not retain:
            path.unlink()
