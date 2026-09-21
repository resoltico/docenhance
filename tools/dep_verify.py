# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Dependency lock validation and cache verification. Never opens a network connection.

Git entries pin the *tag object*, not merely a mutable tag name. Archive entries
pin the bytes. Receipts hash every source file and are checked before each build.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
import tarfile
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
# One entry of deps/lock.json. load_lock() validates the fields each transport requires.
Dependency = dict[str, Any]
MAX_ARCHIVE_MEMBERS = 200_000
MAX_UNPACKED_BYTES = 2_000_000_000
DIGEST_HEX_LENGTHS = {"sha256": 64, "sha512": 128}


class DependencyError(ValueError):
    """A dependency lock, cache entry or upstream source failed verification."""


class CommandError(RuntimeError):
    """An external command exited with a non-zero status."""


def run(*args: str, cwd: Path | None = None) -> str:
    """Run a command without prompts or system Git configuration and return its stdout."""
    env = os.environ.copy()
    env.update(GIT_TERMINAL_PROMPT="0", GIT_CONFIG_NOSYSTEM="1")
    result = subprocess.run(
        args, cwd=cwd, env=env, text=True, encoding="utf-8", capture_output=True, check=False
    )
    if result.returncode:
        msg = f"{' '.join(args[:3])}: {result.stderr.strip()}"
        raise CommandError(msg)
    return result.stdout.strip()


def _validate_git(dep: Dependency) -> None:
    if not re.fullmatch(r"[0-9a-f]{40}", dep["object"]):
        msg = f"Unpinned Git object: {dep['name']}"
        raise DependencyError(msg)
    if not dep["repository"].startswith("https://"):
        msg = "Only HTTPS Git sources are supported"
        raise DependencyError(msg)
    if not dep["ref"].startswith("refs/tags/"):
        msg = "Only explicitly named release tags are supported"
        raise DependencyError(msg)


def _validate_archive(dep: Dependency) -> None:
    size = DIGEST_HEX_LENGTHS.get(dep["digest_algorithm"])
    if not size or not re.fullmatch(rf"[0-9a-f]{{{size}}}", dep["digest"]):
        msg = f"Invalid archive digest: {dep['name']}"
        raise DependencyError(msg)
    if not dep["url"].startswith("https://"):
        msg = "Only HTTPS archives are supported"
        raise DependencyError(msg)


def load_lock(path: Path) -> dict[str, Any]:
    """Load deps/lock.json and reject anything that is not fully pinned."""
    lock: dict[str, Any] = json.loads(path.read_text(encoding="utf-8"))
    if lock.get("schema_version") != 1:
        msg = "Unsupported dependency lock schema"
        raise DependencyError(msg)
    names: set[str] = set()
    for dep in lock["dependencies"]:
        name = dep["name"]
        if not re.fullmatch(r"[a-z][a-z0-9-]*", name) or name in names:
            msg = f"Invalid or duplicate dependency name: {name}"
            raise DependencyError(msg)
        names.add(name)
        if dep["transport"] == "git":
            _validate_git(dep)
        elif dep["transport"] == "archive":
            _validate_archive(dep)
        else:
            msg = f"Unknown transport: {name}"
            raise DependencyError(msg)
    return lock


def digest_file(path: Path, algorithm: str = "sha256") -> str:
    """Return the hex digest of a file's bytes."""
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()


def inventory(source: Path) -> dict[str, str]:
    """Hash every file (and record every symlink target) below a source root."""
    files = {}
    base = source.resolve()
    for path in sorted(source.rglob("*")):
        rel = path.relative_to(source)
        if ".git" in rel.parts:
            continue
        if path.is_symlink():
            if not path.resolve().is_relative_to(base):
                msg = f"Source symlink escapes its root: {rel}"
                raise DependencyError(msg)
            files[rel.as_posix()] = "symlink:" + os.readlink(path)  # noqa: PTH115
        elif path.is_file():
            files[rel.as_posix()] = digest_file(path)
    return files


def validate_licenses(dep: Dependency, source: Path) -> None:
    """Require every declared upstream license file inside the source root."""
    for name in dep["license_files"]:
        path = source / name
        if not path.is_file() or not path.resolve().is_relative_to(source.resolve()):
            msg = f"Missing declared upstream license {dep['name']}/{name}"
            raise DependencyError(msg)


def verify_git(dep: Dependency, source: Path) -> str:
    """Check the pinned tag object, its type, the checkout and a clean tree; return the commit."""
    obj = run("git", "rev-parse", dep["ref"], cwd=source)
    if obj != dep["object"]:
        msg = f"Git release object mismatch: {dep['name']}"
        raise DependencyError(msg)
    if run("git", "cat-file", "-t", obj, cwd=source) != dep["object_type"]:
        msg = f"Git object type mismatch: {dep['name']}"
        raise DependencyError(msg)
    commit = run("git", "rev-parse", f"{obj}^{{commit}}", cwd=source)
    if run("git", "rev-parse", "HEAD", cwd=source) != commit:
        msg = f"Wrong checkout: {dep['name']}"
        raise DependencyError(msg)
    if run("git", "status", "--porcelain", "--untracked-files=all", cwd=source):
        msg = f"Modified dependency checkout: {dep['name']}"
        raise DependencyError(msg)
    return commit


def receipt_for(dep: Dependency, source: Path) -> dict[str, Any]:
    """Build the verification receipt for an acquired source tree."""
    validate_licenses(dep, source)
    commit = verify_git(dep, source) if dep["transport"] == "git" else None
    return {"dependency": dep, "resolved_commit": commit, "files": inventory(source)}


def verify(dep: Dependency, cache: Path) -> dict[str, Any]:
    """Recompute a cached source's receipt and require it to equal the recorded one."""
    source, receipt = cache / "sources" / dep["name"], cache / "receipts" / f"{dep['name']}.json"
    if not source.is_dir() or not receipt.is_file():
        msg = f"Missing {dep['name']}. Run cmake -P cmake/AcquireDependencies.cmake first."
        raise DependencyError(msg)
    recorded = json.loads(receipt.read_text(encoding="utf-8"))
    current = receipt_for(dep, source)
    if current != recorded:
        msg = (
            f"Source content or lock changed: {dep['name']}; "
            "remove only that cache entry and reacquire"
        )
        raise DependencyError(msg)
    return current


def safe_extract(archive: Path, target: Path) -> None:
    """Extract a tar archive within size limits, using Python's data filter."""
    with tarfile.open(archive, "r:*") as tf:
        members = tf.getmembers()
        unpacked = sum(max(m.size, 0) for m in members)
        if len(members) > MAX_ARCHIVE_MEMBERS or unpacked > MAX_UNPACKED_BYTES:
            msg = "Archive exceeds acquisition safety limits"
            raise DependencyError(msg)
        # Python's data filter rejects absolute paths, traversal, devices and escaping links.
        tf.extractall(target, filter="data")
