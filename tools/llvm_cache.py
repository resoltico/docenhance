# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Readiness and the closed installed payload for source-built LLVM analysis tools."""

from __future__ import annotations

import hashlib
import json
import os
import stat
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from pathlib import Path

from dep_verify import digest_file

MAX_RESOURCE_ENTRIES = 4096
MAX_RESOURCE_BYTES = 64 * 1024 * 1024
MAX_RESOURCE_PATH_BYTES = 1024
MAX_RECEIPT_BYTES = 8 * 1024 * 1024


class InstallError(RuntimeError):
    """The pinned LLVM tool could not be installed or verified."""


def resource_inventory(root: Path) -> dict[str, str]:
    """Inspect a finite regular-file/directory tree before hashing bounded file payloads."""
    if root.is_symlink() or not root.is_dir():
        msg = "LLVM builtin headers require an ordinary directory"
        raise InstallError(msg)
    directories = [root]
    entries: dict[str, tuple[Path, int | None]] = {}
    total = 0
    while directories:
        directory = directories.pop()
        with os.scandir(directory) as children:
            for child in children:
                path = directory / child.name
                relative = resource_path(root, path)
                observed = child.stat(follow_symlinks=False)
                if len(entries) >= MAX_RESOURCE_ENTRIES:
                    msg = "LLVM builtin headers exceed the entry limit"
                    raise InstallError(msg)
                if stat.S_ISDIR(observed.st_mode):
                    entries[relative] = (path, None)
                    directories.append(path)
                elif stat.S_ISREG(observed.st_mode):
                    total += observed.st_size
                    if total > MAX_RESOURCE_BYTES:
                        msg = "LLVM builtin headers exceed the byte limit"
                        raise InstallError(msg)
                    entries[relative] = (path, observed.st_size)
                else:
                    msg = f"Unsupported LLVM builtin header entry: {relative}"
                    raise InstallError(msg)
    result = {}
    for name, (path, size) in sorted(entries.items()):
        if size is None:
            result[name] = "directory"
            continue
        result[name] = resource_digest(path, size)
    return result


def resource_path(root: Path, path: Path) -> str:
    """Bound each UTF-8 receipt identity independently of filesystem path limits."""
    relative = path.relative_to(root).as_posix()
    if len(relative.encode("utf-8")) > MAX_RESOURCE_PATH_BYTES:
        msg = "LLVM builtin header path exceeds the byte limit"
        raise InstallError(msg)
    return relative


def resource_digest(path: Path, size: int) -> str:
    """Refuse changed file types and read at most the observed size plus one byte."""
    flags = os.O_RDONLY | getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_BINARY", 0)
    descriptor = os.open(path, flags | getattr(os, "O_NOFOLLOW", 0))
    with os.fdopen(descriptor, "rb") as stream:
        observed = os.fstat(stream.fileno())
        if not stat.S_ISREG(observed.st_mode) or observed.st_size != size:
            msg = f"Changed LLVM builtin header: {path}"
            raise InstallError(msg)
        digest = hashlib.sha256()
        remaining = size
        while chunk := stream.read(min(1024 * 1024, remaining + 1)):
            remaining -= len(chunk)
            if remaining < 0:
                msg = f"Growing LLVM builtin header: {path}"
                raise InstallError(msg)
            digest.update(chunk)
        if remaining:
            msg = f"Truncated LLVM builtin header: {path}"
            raise InstallError(msg)
        return digest.hexdigest()


def source_tool_receipt(target: Path, identity: dict[str, str]) -> dict[str, Any]:
    """Bind both tools and the exact builtin headers needed by ClangTool parsing."""
    files = {}
    for name in ("clang-tidy", "clang-query"):
        path = target / "bin" / name
        if not path.is_file() or path.is_symlink():
            msg = f"Incomplete source-built LLVM cache: {path}"
            raise InstallError(msg)
        files[name] = digest_file(path)
    resource = target / "lib/clang" / identity["version"].split(".")[0] / "include"
    if resource.is_symlink() or not (resource / "stddef.h").is_file():
        msg = f"Missing source-built LLVM builtin headers: {resource}"
        raise InstallError(msg)
    return {"identity": identity, "files": files, "headers": resource_inventory(resource)}


def directory_matches(path: Path, names: set[str]) -> bool:
    """Stop at the first unexpected entry without materializing an arbitrary directory."""
    if path.is_symlink() or not path.is_dir():
        return False
    observed = set()
    with os.scandir(path) as entries:
        for entry in entries:
            if entry.name not in names:
                return False
            observed.add(entry.name)
    return observed == names


def read_receipt(path: Path) -> object:
    """Bound untrusted receipt bytes before JSON decoding."""
    with path.open("rb") as stream:
        data = stream.read(MAX_RECEIPT_BYTES + 1)
    if len(data) > MAX_RECEIPT_BYTES:
        msg = "LLVM cache receipt exceeds the byte limit"
        raise InstallError(msg)
    observed: object = json.loads(data)
    return observed


def validate_source_tools(target: Path, identity: dict[str, str]) -> None:
    """Refuse changed readiness, extra build products and redirected cache paths."""
    major = identity["version"].split(".")[0]
    directories = {
        target: {"bin", "lib", "receipt.json"},
        target / "bin": {"clang-tidy", "clang-query"},
        target / "lib": {"clang"},
        target / "lib/clang": {major},
        target / "lib/clang" / major: {"include"},
    }
    receipt = target / "receipt.json"
    if (
        any(not directory_matches(path, names) for path, names in directories.items())
        or receipt.is_symlink()
        or not receipt.is_file()
        or read_receipt(receipt) != source_tool_receipt(target, identity)
    ):
        msg = f"Incomplete or changed LLVM cache; inspect and remove only {target} before retrying"
        raise InstallError(msg)
