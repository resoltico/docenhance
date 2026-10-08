#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Inspect a relocated native package against the tested build and reviewed source contracts."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

from binary_hardening import errors as hardening_errors
from deps import ROOT, safe_extract
from documentation import local_link_errors
from hardening_compilation import errors as compilation_errors
from method_metadata import support_matrix
from package_inspection import binary_identity, contents, differences, expected_files, imports
from package_selection import current_package
from project_version import project_version


def run_json(exe: Path, *args: str) -> dict[str, Any]:
    """Require successful JSON delivery without diagnostics and validate its complete response."""
    result = subprocess.run(
        [str(exe), *args], capture_output=True, text=True, encoding="utf-8", check=False, timeout=60
    )
    if result.returncode or result.stderr:
        msg = (
            f"Packaged JSON command failed delivery/execution ({result.returncode}): "
            f"{result.stdout} {result.stderr}"
        )
        raise ValueError(msg)
    data: dict[str, Any] = json.loads(result.stdout)
    Draft202012Validator(
        json.loads((ROOT / "schemas/command-response.schema.json").read_bytes())
    ).validate(data)
    return data


def macos_minimum_errors(exe: Path) -> list[str]:
    """Check the declared minimum OS version, without claiming execution on that OS."""
    if platform.system() != "Darwin":
        return []
    expected = json.loads((ROOT / "deps/tools.json").read_text())["macos"]["deployment_target"]
    otool = shutil.which("otool")
    if otool is None:
        return ["otool is required to read the executable's minimum macOS version"]
    commands = subprocess.run(
        [otool, "-l", str(exe)], capture_output=True, text=True, check=True, timeout=30
    ).stdout
    found = re.findall(r"^\s*minos\s+(\S+)$", commands, flags=re.MULTILINE)
    return (
        [] if found == [expected] else [f"Executable minimum macOS is {found}, expected {expected}"]
    )


def gray_fixture(width: int, height: int) -> bytes:
    """Construct nonbinary grayscale PNG bytes with independent CRCs and compressed scanlines."""

    def chunk(name: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))
        )

    rows = b"".join(
        b"\0" + bytes(64 if x % 2 == 0 else 192 for x in range(width)) for _ in range(height)
    )
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows))
        + chunk(b"IEND", b"")
    )


def exercise(exe: Path, root: Path) -> None:
    """Run all advertised methods after relocation and validate responses, records and bundles."""
    source = root / "source.png"
    source.write_bytes(gray_fixture(128, 128))
    jpeg = root / "source.jpg"
    jpeg.write_bytes((ROOT / "tests/fixtures/jpeg/ycbcr-2x2-progressive.jpg").read_bytes())
    tiff = root / "source.tiff"
    tiff.write_bytes((ROOT / "tests/fixtures/tiff/d16-c3-t1-p2-b1-l0.tif").read_bytes())
    cases: tuple[tuple[str, Path, list[str]], ...] = (
        ("tiff", tiff, []),
        ("clahe", source, ["--contrast", "clahe"]),
        ("unsharp", source, ["--sharpen", "unsharp"]),
        ("levels", source, ["--contrast", "levels"]),
        ("gamma", source, ["--contrast", "gamma"]),
        ("morph", source, ["--illumination", "morph", "--background-radius", "8"]),
        ("tvl1", source, ["--denoise", "tvl1"]),
        ("fixed", source, ["--output-mode", "bw", "--binarize", "fixed"]),
        ("sauvola", source, ["--output-mode", "bw", "--binarize", "sauvola"]),
        ("otsu", source, ["--output-mode", "bw", "--binarize", "otsu"]),
        ("illumination", source, ["--illumination", "surface", "--background-cell", "8"]),
        ("denoising", jpeg, ["--denoise", "nlm", "--nlm-patch", "3", "--nlm-search", "7"]),
    )
    record_schema = Draft202012Validator(
        json.loads((ROOT / "schemas/run-record.schema.json").read_bytes())
    )
    for name, image, arguments in cases:
        output = root / name
        response = run_json(
            exe, "process", str(image), "--out-dir", str(output), *arguments, "--json"
        )
        if response["publication"] != "completed":
            msg = f"Packaged processing did not complete: {name}"
            raise ValueError(msg)
        if name == "denoising" and response["denoising"]["native_calls"] != 1:
            msg = "Relocated executable did not execute and reuse native D01"
            raise ValueError(msg)
        record_schema.validate(json.loads((output / "run.json").read_bytes()))
        run_json(exe, "verify", str(output), "--json")


def smoke(root: Path, owner: Path) -> list[str]:
    """Reject changed/missing/extra package bytes before running its tested native executable."""
    build = owner / "app"
    candidates = [*root.rglob("docenhance.exe"), *root.rglob("bin/docenhance")]
    if len(candidates) != 1:
        return ["Package must contain exactly one runtime executable"]
    exe = candidates[0]
    package = exe.parent.parent
    failures = differences(
        contents(root),
        {
            f"{package.relative_to(root).as_posix()}/{name}": data
            for name, data in expected_files(owner, exe.name).items()
        },
    )
    failures += local_link_errors(package / "share/docenhance")
    failures += macos_minimum_errors(exe)
    failures += hardening_errors(exe)
    failures += compilation_errors(build, build / "hardening-policy.json")
    if failures:
        return failures
    native_imports = imports(exe)
    version = run_json(exe, "version", "--json")
    metadata = json.loads((package / "share/docenhance/build-info.json").read_bytes())
    for field in ("version", "platform", "compiler", "dependency_lock_sha256"):
        if version[field] != metadata[field]:
            failures.append(f"Executable and inventory disagree on {field}")
    if (
        version["version"] != project_version()
        or version["dependency_lock_sha256"]
        != hashlib.sha256((ROOT / "deps/lock.json").read_bytes()).hexdigest()
    ):
        failures.append("Packaged build identity disagrees with current sources")
    contract = json.loads((ROOT / "spec/method-contract.json").read_bytes())
    expected = [
        {"id": m["id"], "method_version": m["method_version"]}
        for m in contract["methods"]
        if m["status"] == "implemented"
    ]
    if version["methods"] != expected or version["input_support"] != support_matrix(
        json.loads((ROOT / "spec/cli-contract.json").read_bytes())["input_support"]
    ):
        failures.append("Packaged capability matrix disagrees with supported admission")
    if not failures:
        exercise(exe, root)
        print(f"Inspected binary SHA-256: {binary_identity(exe)}; OS imports: {native_imports}")
    return failures


def main() -> int:
    """Extract one archive and compare it with the independently tested build."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build", type=Path, required=True, help="Tested owning superbuild directory"
    )
    args = parser.parse_args()
    try:
        owner = args.build.resolve()
        archive = current_package(owner)
        with tempfile.TemporaryDirectory(prefix="docenhance-package-") as temporary:
            root = Path(temporary)
            safe_extract(archive, root)
            failures = smoke(root, owner)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Native package inspection failed: {error}", file=sys.stderr)
        return 1
    print("\n".join(failures) if failures else "PASS: inspected relocated native package")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
