# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Compare delivered bytes with their verified build, sources and native OS import boundary."""

from __future__ import annotations

import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from audit_build import read_cache
from license_inventory import generate

ROOT = Path(__file__).resolve().parents[1]


def contents(root: Path) -> dict[str, bytes]:
    """Read regular package files without accepting redirected inventory paths."""
    result = {}
    for path in root.rglob("*"):
        if path.is_symlink():
            msg = f"Package inventory contains a link: {path}"
            raise ValueError(msg)
        if path.is_file():
            result[path.relative_to(root).as_posix()] = path.read_bytes()
    return result


def differences(actual: dict[str, bytes], expected: dict[str, bytes]) -> list[str]:
    """Check the closed file set and every expected byte, without trusting a supplied manifest."""
    return [
        f"Package file missing, changed or unexpected: {name}"
        for name in sorted(actual.keys() | expected.keys())
        if actual.get(name) != expected.get(name)
    ]


def expected_files(build: Path, executable: str) -> dict[str, bytes]:
    """Regenerate licenses/SBOM from verified locked sources and use the tested native binary."""
    metadata = json.loads((build / "package-metadata/build-info.json").read_bytes())
    cache = read_cache(build / "CMakeCache.txt")
    with tempfile.TemporaryDirectory(prefix="docenhance-inventory-") as temporary:
        directory = Path(temporary)
        generate(
            Path(cache["DE_SOURCE_CACHE"]), directory, metadata["platform"], metadata["compiler"]
        )
        expected = {f"share/docenhance/{name}": data for name, data in contents(directory).items()}
    expected[f"bin/{executable}"] = (build / "bin" / executable).read_bytes()
    for name in ("LICENSE", "README.md"):
        expected[name] = (ROOT / name).read_bytes()
    for name in ("cli-contract.json", "method-contract.json"):
        expected[f"share/docenhance/spec/{name}"] = (ROOT / "spec" / name).read_bytes()
    expected.update(
        {
            f"share/docenhance/schemas/{p.name}": p.read_bytes()
            for p in (ROOT / "schemas").glob("*.json")
        }
    )
    return expected


def imports(executable: Path) -> list[str]:
    """Read actual native imports; inspection tools are prerequisites, never optional coverage."""
    system = platform.system()
    tool, arguments = {
        "Darwin": ("otool", ["-L"]),
        "Linux": ("ldd", []),
        "Windows": ("dumpbin", ["/DEPENDENTS"]),
    }[system]
    resolved = shutil.which(tool)
    if resolved is None:
        msg = f"{tool} is required to inspect the delivered executable"
        raise ValueError(msg)
    text = subprocess.run(
        [resolved, *arguments, str(executable)],
        capture_output=True,
        text=True,
        check=True,
        timeout=30,
    ).stdout
    if system == "Darwin":
        values = [line.strip().split(" (", 1)[0] for line in text.splitlines()[1:] if line.strip()]
        if not values or any(
            not name.startswith(("/usr/lib/", "/System/Library/")) for name in values
        ):
            msg = f"Executable loads non-OS native libraries: {values}"
            raise ValueError(msg)
        return values
    if system == "Windows":
        values = re.findall(r"^\s+([\w.-]+\.dll)\s*$", text, re.MULTILINE | re.IGNORECASE)
        system_directory = Path(os.environ["SYSTEMROOT"]) / "System32"
        if not values or any(
            not (system_directory / name).is_file()
            or re.match(r"(?:msvcp|msvcr|vcruntime|ucrtbase|api-ms-win-crt)", name, re.IGNORECASE)
            for name in values
        ):
            msg = f"Executable imports a non-OS library or dynamic CRT: {values}"
            raise ValueError(msg)
        return values
    values = re.findall(r"(?:=>\s+)?(/\S+)\s+\(", text)
    if (
        "not found" in text
        or not values
        or any(
            not name.startswith(("/lib/", "/lib64/", "/usr/lib/", "/usr/lib64/"))
            or not re.fullmatch(
                r"(?:lib(?:c|m|dl|rt|pthread|stdc\+\+|gcc_s)\.so[\d.]*|ld-linux[\w.-]*\.so[\d.]*)",
                Path(name).name,
            )
            for name in values
        )
    ):
        msg = f"Executable loads non-OS native libraries: {text.strip()}"
        raise ValueError(msg)
    return values


def binary_identity(executable: Path) -> str:
    """Expose the inspected binary identity as evidence, not a claim of review or fidelity."""
    return hashlib.sha256(executable.read_bytes()).hexdigest()
