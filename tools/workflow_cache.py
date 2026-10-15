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


LLVM_CONDITION = "endsWith(matrix.platform, 'x86_64') && runner.os == 'macOS'"
LLVM_ENVIRONMENT_COMMAND = """import hashlib
import json
import os
import platform
identity = {
    "host_os_version": platform.mac_ver()[0],
    "developer_directory": os.environ.get("DEVELOPER_DIR", "system-default"),
}
digest = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
    output.write(f"identity={digest}\\n")"""
LLVM_CACHE_KEY = (
    "llvm-source-${{ matrix.os }}-${{ runner.arch }}-"
    "${{ steps.intel-llvm-environment.outputs.identity }}-"
    "${{ hashFiles('deps/tools.json', 'tools/install_llvm.py', 'tools/llvm_cache.py') }}"
)


def llvm_cache(job: dict[str, Any], selection: dict[str, Any]) -> None:
    """An outer cache hit must identify the same environment as its installed generation."""
    steps = job["steps"]
    fingerprints = [step for step in steps if step.get("id") == "intel-llvm-environment"]
    restores = [step for step in steps if step.get("id") == "intel-llvm-cache"]
    saves = [step for step in steps if step.get("name") == "Save verified Intel LLVM tools"]
    installations = [s for s in steps if s.get("run") == "python tools/install_llvm.py"]
    if any(len(found) != 1 for found in (fingerprints, restores, saves, installations)):
        message = "Intel LLVM cache requires one environment fingerprint, restore and save"
        raise ValueError(message)
    fingerprint, restore, save = fingerprints[0], restores[0], saves[0]
    if (
        fingerprint.get("if") != LLVM_CONDITION
        or fingerprint.get("shell") != "python"
        or fingerprint.get("run", "").strip() != LLVM_ENVIRONMENT_COMMAND
        or set(fingerprint) != {"name", "id", "if", "shell", "run"}
    ):
        message = "Intel LLVM cache must fingerprint the actual installer environment"
        raise ValueError(message)
    for step, action, condition in (
        (restore, "restore", LLVM_CONDITION),
        (save, "save", LLVM_CONDITION + " && steps.intel-llvm-cache.outputs.cache-hit != 'true'"),
    ):
        if (
            step.get("uses") != f"actions/cache/{action}@{SOURCE_CACHE_ACTION}"
            or step.get("if") != condition
            or step.get("with") != {"path": "~/.cache/docenhance/llvm", "key": LLVM_CACHE_KEY}
            or step.get("continue-on-error", "false") != "false"
        ):
            message = "Intel LLVM cache must bind the environment and verified tool recipe"
            raise ValueError(message)
    installation = installations[0]
    if (
        not steps.index(selection)
        < steps.index(fingerprint)
        < steps.index(restore)
        < steps.index(installation)
        < steps.index(save)
    ):
        message = "Intel LLVM cache must select Xcode before fingerprinting and verified save"
        raise ValueError(message)
