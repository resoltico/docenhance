#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Explicit online install of the pinned LLVM tools for CI runners and fresh machines.

Installs clang-tidy and clang-query; --compiler also installs the selected-major clang and
sanitizer runtimes. --fuzzing adds the Linux tools needed by AFL++. Sources and recipes come from
deps/tools.json:
  Linux    the supported LLVM major from apt.llvm.org, with a fingerprint-checked key
  macOS    the configured Homebrew formula with immediate major validation; Intel tools
           come from a digest-pinned source archive
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
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

from cache_lock import exclusive_cache
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
    """Install the Homebrew formula selected by the reviewed tool policy."""
    brew = tool("brew")
    subprocess.run([brew, "install", pin["homebrew_formula"]], check=True)
    prefix = subprocess.run(
        [brew, "--prefix", pin["homebrew_formula"]], capture_output=True, text=True, check=True
    ).stdout.strip()
    return Path(prefix) / "bin"


def install_macos_intel_source(pin: dict[str, Any], version: str, work: Path) -> Path:
    """Build only the pinned analysis tools when Homebrew lacks the required Intel formula."""
    identity = {
        "version": version,
        "source_url": pin["source_url"],
        "source_sha256": pin["source_sha256"],
        "architecture": "x86_64",
        "recipe_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }
    identifier = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:16]
    target = Path.home() / ".cache" / "docenhance" / "llvm" / f"{version}-x86_64-{identifier}"
    with exclusive_cache(target.with_suffix(".active")):
        tools = target / "build" / "bin"
        if target.exists():
            validate_source_tools(target, identity)
            return tools
        build_source_tools(pin, work, target)
        observed = source_tool_receipt(target, identity)
        (target / "receipt.json").write_text(
            json.dumps(observed, sort_keys=True) + "\n", encoding="utf-8"
        )
        return tools


def source_tool_receipt(target: Path, identity: dict[str, str]) -> dict[str, Any]:
    """Readiness and byte identity for both tools; this is not a binary reproducibility claim."""
    files = {}
    for name in ("clang-tidy", "clang-query"):
        path = target / "build/bin" / name
        if not path.is_file() or path.is_symlink():
            msg = f"Incomplete source-built LLVM cache: {path}"
            raise InstallError(msg)
        with path.open("rb") as stream:
            files[name] = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"identity": identity, "files": files}


def validate_source_tools(target: Path, identity: dict[str, str]) -> None:
    """Reject missing/changed readiness evidence instead of adopting partial cache contents."""
    receipt = target / "receipt.json"
    if not receipt.is_file() or json.loads(receipt.read_text()) != source_tool_receipt(
        target, identity
    ):
        msg = f"Incomplete or changed LLVM cache; inspect and remove only {target} before retrying"
        raise InstallError(msg)


def build_source_tools(pin: dict[str, Any], work: Path, target: Path) -> None:
    """Build both analysis tools from one digest-verified source archive."""
    archive = work / "llvm.tar.xz"
    download(pin["source_url"], archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != pin["source_sha256"]:
        msg = "LLVM source archive digest mismatch"
        raise InstallError(msg)
    source = work / "source"
    source.mkdir()
    subprocess.run(
        [tool("tar"), "-xJf", str(archive), "-C", str(source), "--strip-components=1"], check=True
    )
    build = target / "build"
    build.parent.mkdir(parents=True, exist_ok=True)
    cmake = tool("cmake")
    subprocess.run(
        [
            cmake,
            "-S",
            str(source / "llvm"),
            "-B",
            str(build),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DLLVM_ENABLE_PROJECTS=clang;clang-tools-extra",
            "-DLLVM_TARGETS_TO_BUILD=X86",
            "-DLLVM_ENABLE_TERMINFO=OFF",
            "-DLLVM_ENABLE_ZLIB=OFF",
            "-DLLVM_INCLUDE_TESTS=OFF",
            "-DLLVM_INCLUDE_EXAMPLES=OFF",
            "-DLLVM_ENABLE_BINDINGS=OFF",
        ],
        check=True,
    )
    subprocess.run(
        [cmake, "--build", str(build), "--target", "clang-tidy", "clang-query", "--parallel", "3"],
        check=True,
    )


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


def resolve_analysis_tool(directory: Path, name: str, major: str) -> Path:
    """Prefer the installed major-qualified tool over a runner's older generic executable."""
    candidates = (directory / f"{name}-{major}", directory / name, directory / f"{name}.exe")
    selected = next((path for path in candidates if path.is_file()), None)
    if selected is None:
        msg = f"{name} {major}.x is missing from {directory}"
        raise InstallError(msg)
    return selected


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
            directory = (
                install_macos_intel_source(pins["macos"], tidy["version"], work)
                if platform.machine() == "x86_64"
                else install_macos(pins["macos"])
            )
        elif system == "Windows":
            directory = install_windows(pins["windows_x86_64"], work)
        else:
            msg = f"No pinned clang-tidy installation for {system}"
            raise InstallError(msg)
    expected_major = str(tidy["version"]).split(".")[0]
    executable = resolve_analysis_tool(directory, "clang-tidy", expected_major)
    required = [executable]
    if system != "Windows":
        query = resolve_analysis_tool(directory, "clang-query", expected_major)
        required.append(query)
    for path in required:
        output = subprocess.run(
            [str(path), "--version"], capture_output=True, text=True, check=True
        ).stdout
        match = re.search(r"\bversion\s+([0-9]+)\.", output, re.IGNORECASE)
        if match is None or match[1] != expected_major:
            msg = f"LLVM {expected_major}.x is required; found {path}: {output.strip()}"
            raise InstallError(msg)
        print(output.strip())
    print(f"clang-tidy directory: {directory}")
    if github_env := os.environ.get("GITHUB_ENV"):
        with Path(github_env).open("a", encoding="utf-8") as stream:
            stream.write(f"DE_CLANG_TIDY_DIR={directory}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
