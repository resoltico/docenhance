#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Explicit online install of the pinned LLVM tools for CI runners and fresh machines.

Installs clang-tidy and the clang-query the architecture rules use; with --compiler also the pinned
clang and its sanitizer runtime, and with --fuzzing also what building AFL++ needs (on Linux; the
Homebrew formula and the Windows installer contain everything). Every source and digest comes from
deps/tools.json:
  Linux    apt.llvm.org packages, repository key checked against the pinned fingerprint
  macOS    the versioned Homebrew formula
  Windows  the official LLVM installer, SHA-256 verified, extracted without registration
The directory holding them is printed and, under GitHub Actions, exported as DE_CLANG_TIDY_DIR,
which cmake/ProjectOptions.cmake and tools/architecture_build.py search before PATH.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

from dep_acquire import download

ROOT = Path(__file__).resolve().parents[1]
KEYRING = Path("/etc/apt/keyrings/llvm-snapshot.asc")


class InstallError(RuntimeError):
    """The pinned tool could not be installed or verified."""


def tool(name: str) -> str:
    """Resolve an external command to an absolute path."""
    path = shutil.which(name)
    if path is None:
        msg = f"Required command not found: {name}"
        raise InstallError(msg)
    return path


def install_linux(pin: dict[str, Any], work: Path, *, compiler: bool, fuzzing: bool) -> Path:
    """Add apt.llvm.org with a fingerprint-checked key and install the versioned package."""
    key = work / "llvm.asc"
    download(pin["key_url"], key)
    listing = subprocess.run(
        [tool("gpg"), "--show-keys", "--with-colons", str(key)],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    fingerprints = [line.split(":")[9] for line in listing.splitlines() if line.startswith("fpr:")]
    if not fingerprints or fingerprints[0] != pin["key_fingerprint"]:
        msg = f"apt.llvm.org key fingerprint mismatch: {fingerprints[:1]}"
        raise InstallError(msg)
    source = f"deb [signed-by={KEYRING}] {pin['apt_repository']}\n"
    sudo = tool("sudo")
    subprocess.run([sudo, "install", "-D", "-m", "0644", str(key), str(KEYRING)], check=True)
    subprocess.run(
        [sudo, "tee", "/etc/apt/sources.list.d/llvm-toolchain.list"],
        input=source,
        text=True,
        stdout=subprocess.DEVNULL,
        check=True,
    )
    subprocess.run([sudo, "apt-get", "update", "-q"], check=True)
    install = [sudo, "apt-get", "install", "-y", "-q", "--no-install-recommends"]
    packages = list(pin["packages"])
    if compiler or fuzzing:
        packages += pin["compiler_packages"]
    if fuzzing:
        packages += pin["fuzzing_packages"]
    subprocess.run([*install, *packages], check=True)
    return Path("/usr/bin")


def install_macos(pin: dict[str, Any]) -> Path:
    """Install the versioned Homebrew formula."""
    brew = tool("brew")
    subprocess.run([brew, "install", pin["homebrew_formula"]], check=True)
    prefix = subprocess.run(
        [brew, "--prefix", pin["homebrew_formula"]], capture_output=True, text=True, check=True
    ).stdout.strip()
    return Path(prefix) / "bin"


def install_windows(pin: dict[str, Any], work: Path) -> Path:
    """Verify the official MSI and extract it with an administrative (unregistered) install."""
    msi = work / "llvm.msi"
    download(pin["url"], msi)
    with msi.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != pin["sha256"]:
        msg = f"LLVM installer digest mismatch: {digest}"
        raise InstallError(msg)
    target = Path(os.environ.get("RUNNER_TEMP", work)) / "llvm"
    subprocess.run([tool("msiexec"), "/a", str(msi), "/qn", f"TARGETDIR={target}"], check=True)
    found = sorted(target.rglob("clang-tidy.exe"))
    if not found:
        msg = "clang-tidy.exe missing from the extracted LLVM installer"
        raise InstallError(msg)
    return found[0].parent


def main() -> int:
    """Install for the current platform and report the clang-tidy directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--compiler", action="store_true", help="Also install clang and its sanitizer runtime"
    )
    parser.add_argument(
        "--fuzzing", action="store_true", help="Also install clang, libFuzzer and the AFL++ build"
    )
    args = parser.parse_args()
    tidy = json.loads((ROOT / "deps/tools.json").read_text(encoding="utf-8"))["clang_tidy"]
    pins = tidy["install"]
    system = platform.system()
    with tempfile.TemporaryDirectory(prefix="docenhance-llvm-") as temp:
        work = Path(temp)
        if system == "Linux":
            directory = install_linux(
                pins["linux"], work, compiler=args.compiler, fuzzing=args.fuzzing
            )
        elif system == "Darwin":
            directory = install_macos(pins["macos"])
        elif system == "Windows":
            directory = install_windows(pins["windows_x86_64"], work)
        else:
            msg = f"No pinned clang-tidy installation for {system}"
            raise InstallError(msg)
    print(f"clang-tidy {tidy['version']} directory: {directory}")
    if github_env := os.environ.get("GITHUB_ENV"):
        with Path(github_env).open("a", encoding="utf-8") as stream:
            stream.write(f"DE_CLANG_TIDY_DIR={directory}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
