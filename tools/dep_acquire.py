# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Explicit, verified dependency acquisition into the source cache. The only networked step."""

from __future__ import annotations

import json
import shutil
import tempfile
import urllib.request
from pathlib import Path
from typing import Any

from dep_verify import (
    Dependency,
    DependencyError,
    digest_file,
    receipt_for,
    run,
    safe_extract,
    verify,
)

MAX_DOWNLOAD_BYTES = 1_000_000_000
DOWNLOAD_CHUNK_BYTES = 1024 * 1024
DOWNLOAD_TIMEOUT_SECONDS = 120


def fetch_git(dep: Dependency, staging: Path) -> None:
    """Fetch exactly the pinned tag object into an empty staging repository."""
    run("git", "-c", "init.defaultBranch=main", "init", str(staging))
    run("git", "config", "core.autocrlf", "false", cwd=staging)
    run("git", "config", "core.hooksPath", ".disabled-hooks", cwd=staging)
    run("git", "remote", "add", "origin", dep["repository"], cwd=staging)
    run(
        "git",
        "-c",
        "protocol.file.allow=never",
        "-c",
        "protocol.ext.allow=never",
        "fetch",
        "--depth=1",
        "--no-recurse-submodules",
        "origin",
        f"{dep['ref']}:{dep['ref']}",
        cwd=staging,
    )
    if run("git", "rev-parse", dep["ref"], cwd=staging) != dep["object"]:
        msg = f"Upstream tag has changed: {dep['name']}"
        raise DependencyError(msg)
    run("git", "checkout", "--detach", f"{dep['object']}^{{commit}}", cwd=staging)
    run("git", "fsck", "--no-reflogs", "--no-dangling", cwd=staging)


def download(url: str, destination: Path) -> None:
    """Stream an HTTPS URL to a new file, refusing redirects to other schemes and huge bodies."""
    # load_lock() admits only https:// URLs, and the final URL is re-checked after redirects.
    request = urllib.request.Request(url, headers={"User-Agent": "DocEnhance-maintainer/0.1"})  # noqa: S310
    with (
        urllib.request.urlopen(request, timeout=DOWNLOAD_TIMEOUT_SECONDS) as response,  # noqa: S310
        destination.open("xb") as stream,
    ):
        if not response.url.startswith("https://"):
            msg = "Insecure archive redirect"
            raise DependencyError(msg)
        total = 0
        while chunk := response.read(DOWNLOAD_CHUNK_BYTES):
            total += len(chunk)
            if total > MAX_DOWNLOAD_BYTES:
                msg = "Compressed archive exceeds acquisition safety limit"
                raise DependencyError(msg)
            stream.write(chunk)


def cached_archive(dep: Dependency, cache: Path) -> Path:
    """Return the digest-verified archive for dep, downloading it first when absent."""
    archives = cache / "archives"
    archives.mkdir(parents=True, exist_ok=True)
    archive = archives / f"{dep['name']}-{dep['version']}.tar.gz"
    if not archive.exists():
        temporary = archive.with_suffix(".download")
        try:
            download(dep["url"], temporary)
            if digest_file(temporary, dep["digest_algorithm"]) != dep["digest"]:
                msg = f"Archive digest mismatch: {dep['name']}"
                raise DependencyError(msg)
            temporary.rename(archive)
        finally:
            temporary.unlink(missing_ok=True)
    if digest_file(archive, dep["digest_algorithm"]) != dep["digest"]:
        msg = f"Cached archive digest mismatch: {dep['name']}"
        raise DependencyError(msg)
    return archive


def unpack_archive(archive: Path, staging: Path) -> None:
    """Extract an archive with exactly one root directory into staging, without that root."""
    unpacked = staging / "unpack"
    unpacked.mkdir()
    safe_extract(archive, unpacked)
    roots = list(unpacked.iterdir())
    if len(roots) != 1 or not roots[0].is_dir():
        msg = "Expected exactly one archive root directory"
        raise DependencyError(msg)
    for child in roots[0].iterdir():
        child.rename(staging / child.name)
    shutil.rmtree(unpacked)


def write_receipt(receipt: dict[str, Any], target: Path) -> None:
    """Atomically replace a receipt file."""
    temp_receipt = target.with_suffix(".tmp")
    temp_receipt.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    temp_receipt.replace(target)


def fetch(dep: Dependency, cache: Path) -> None:
    """Acquire one dependency into the cache through a staging directory, or verify it."""
    source = cache / "sources" / dep["name"]
    if source.exists() or source.is_symlink():
        verify(dep, cache)
        print(f"Verified cached {dep['name']} {dep['version']}")
        return
    source.parent.mkdir(parents=True, exist_ok=True)
    (cache / "receipts").mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{dep['name']}-", dir=source.parent))
    try:
        if dep["transport"] == "git":
            fetch_git(dep, staging)
        else:
            unpack_archive(cached_archive(dep, cache), staging)
        receipt = receipt_for(dep, staging)
        staging.rename(source)
        write_receipt(receipt, cache / "receipts" / f"{dep['name']}.json")
        print(f"Acquired and verified {dep['name']} {dep['version']}")
    finally:
        if staging.exists():
            shutil.rmtree(staging)
