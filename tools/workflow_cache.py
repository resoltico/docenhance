# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Enforce source-cache identity, payload and successful verification ordering."""

from __future__ import annotations

from typing import Any

SOURCE_CACHE_ACTION = "55cc8345863c7cc4c66a329aec7e433d2d1c52a9"
SOURCE_CACHE_KEY = (
    "deps-source-${{ runner.os }}-${{ hashFiles('deps/lock.json', 'tools/deps.py', "
    "'tools/dep_acquire.py', 'tools/dep_verify.py', 'tools/cache_lock.py', "
    "'cmake/AcquireDependencies.cmake') }}"
)
SOURCE_CACHE_PATHS = "\n".join(
    f".cache/deps/{name}" for name in ("archives", "sources", "receipts")
)


def source_cache(job: dict[str, Any], acquired: dict[str, Any]) -> None:
    """Restored bytes remain untrusted until unconditional acquisition verifies them."""
    steps = job["steps"]
    restores = [step for step in steps if step.get("id") == "dependency-source-cache"]
    saves = [
        step
        for step in steps
        if step.get("uses") == f"actions/cache/save@{SOURCE_CACHE_ACTION}"
        and step.get("with", {}).get("key", "").startswith("deps-source-")
    ]
    if len(restores) != 1 or len(saves) != 1:
        msg = "Dependency source cache requires exactly one restore and save"
        raise ValueError(msg)
    restore, save = restores[0], saves[0]
    for step in steps:
        if step in (restore, save) or not step.get("uses", "").lower().startswith(
            ("actions/cache/", "actions/cache@")
        ):
            continue
        if step.get("with", {}).get("path") != "~/.cache/docenhance/llvm":
            msg = "Dependency source cache prohibits alternate cache payloads"
            raise ValueError(msg)
    for step, action in ((restore, "restore"), (save, "save")):
        payload = step.get("with", {})
        if (
            step.get("uses") != f"actions/cache/{action}@{SOURCE_CACHE_ACTION}"
            or payload.get("path", "").strip() != SOURCE_CACHE_PATHS
            or payload.get("key") != SOURCE_CACHE_KEY
            or set(payload) != {"path", "key"}
            or step.get("continue-on-error", "false") != "false"
        ):
            msg = "Dependency source cache must bind exact verified-source paths and policy"
            raise ValueError(msg)
    if "if" in restore or save.get("if") != (
        "steps.dependency-source-cache.outputs.cache-hit != 'true'"
    ):
        msg = "Dependency source cache must restore unconditionally and save only on success/miss"
        raise ValueError(msg)
    if not steps.index(restore) + 1 == steps.index(acquired) == steps.index(save) - 1:
        msg = "Dependency source cache must bracket successful unconditional acquisition"
        raise ValueError(msg)
