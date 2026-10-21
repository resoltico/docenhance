#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Inspect a relocated native package against the tested build and reviewed source contracts."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path
from typing import TYPE_CHECKING, Any

from jsonschema import Draft202012Validator

from binary_hardening import errors as hardening_errors
from deps import ROOT, safe_extract
from documentation import local_link_errors
from hardening_compilation import errors as compilation_errors
from method_catalog import catalog_entries, catalog_projection
from method_metadata import support_matrix
from package_inspection import binary_identity, contents, differences, expected_files, imports
from package_selection import current_package
from parameter_contract import merged_options
from project_version import project_version

if TYPE_CHECKING:
    from jsonschema.protocols import Validator


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


QUARTER_TURN_DEGREES = 90


def inspect_run(
    name: str,
    output: Path,
    response: dict[str, Any],
    psf: Path,
    record_schema: Validator,
) -> None:
    """Reconcile actual wire15 artifacts with response facts after relocated execution."""
    if response["publication"] != "completed":
        msg = f"Packaged processing did not complete: {name}"
        raise ValueError(msg)
    record_bytes = (output / "run.json").read_bytes()
    record = json.loads(record_bytes)
    record_schema.validate(record)
    if (
        response["record"] != record
        or response["record_sha256"] != hashlib.sha256(record_bytes).hexdigest()
    ):
        msg = "Relocated executable response disagrees with the exact persisted record"
        raise ValueError(msg)
    if name == "denoising":
        facts = record["execution"]["denoising"]
        settings = record["request"]["denoising"]["parameters"]
        expected_table = math.ceil(3 * (settings["h"] / 255) * settings["patch"] ** 2 * 65535)
        if (
            facts["method"] != "D01"
            or facts["counts"]["evaluated"] != 160 * 128
            or facts["weight_table_entries"] != expected_table
            or facts["reason"] not in {"none", "no_effect"}
        ):
            msg = "Relocated executable did not complete first-party D01 traversal/table work"
            raise ValueError(msg)
    if name == "supplied_psf":
        retained = (output / "assets/psf.png").read_bytes()
        assets = record["assets"]
        if retained != psf.read_bytes() or len(assets) != 1 or assets[0]["role"] != "psf_kernel":
            msg = "Relocated executable did not retain the exact supplied PSF asset"
            raise ValueError(msg)
    if name == "quarter_turn":
        dimensions = struct.unpack(">II", (output / "result.png").read_bytes()[16:24])
        if (
            dimensions != (128, 160)
            or response["record"]["execution"]["interpretation"]["rotation"] != QUARTER_TURN_DEGREES
            or record["request"]["rotation"] != QUARTER_TURN_DEGREES
        ):
            msg = "Relocated executable did not perform and record asymmetric G02 rotation"
            raise ValueError(msg)
    if name in {"fixed", "sauvola", "otsu", "jpeg_binary", "tiff_binary"}:
        inspect_binary(output, record)


def inspect_binary(output: Path, record: dict[str, Any]) -> None:
    """Validate actual physical PNG1 and complete terminal classification coverage."""
    encoded = (output / "result.png").read_bytes()
    if encoded[24:26] != bytes((1, 0)):
        msg = "Relocated executable did not produce physical grayscale PNG1"
        raise ValueError(msg)
    counts = record["execution"]["binarization"]
    width, height = struct.unpack(">II", encoded[16:24])
    if counts["black_samples"] + counts["white_samples"] != width * height:
        msg = "Relocated binary classification did not cover every output pixel"
        raise ValueError(msg)


def exercise(exe: Path, root: Path) -> None:
    """Run all advertised methods after relocation and validate responses, records and bundles."""
    source = root / "source.png"
    source.write_bytes(gray_fixture(160, 128))
    jpeg = root / "source.jpg"
    jpeg.write_bytes((ROOT / "tests/fixtures/jpeg/ycbcr-2x2-progressive.jpg").read_bytes())
    tiff = root / "source.tiff"
    tiff.write_bytes((ROOT / "tests/fixtures/tiff/d16-c3-t1-p2-b1-l0.tif").read_bytes())
    psf = root / "kernel.png"
    psf.write_bytes(gray_fixture(3, 3))
    cases: tuple[tuple[str, Path, list[str]], ...] = (
        ("tiff", tiff, []),
        ("jpeg", jpeg, []),
        ("jpeg_binary", jpeg, ["--output-mode", "bw", "--binarize", "fixed"]),
        ("tiff_binary", tiff, ["--output-mode", "bw", "--binarize", "fixed"]),
        ("quarter_turn", source, ["--rotate", str(QUARTER_TURN_DEGREES)]),
        ("clahe", source, ["--contrast", "clahe"]),
        ("unsharp", source, ["--sharpen", "unsharp"]),
        ("levels", source, ["--contrast", "levels"]),
        ("gamma", source, ["--contrast", "gamma"]),
        ("morph", source, ["--illumination", "morph", "--background-radius", "8"]),
        ("tvl1", source, ["--denoise", "tvl1"]),
        ("restoration", source, ["--deblur", "wiener", "--psf", "gaussian"]),
        ("supplied_psf", source, ["--deblur", "wiener", "--psf", "kernel", "--psf-file", str(psf)]),
        ("fixed", source, ["--output-mode", "bw", "--binarize", "fixed"]),
        ("sauvola", source, ["--output-mode", "bw", "--binarize", "sauvola"]),
        ("otsu", source, ["--output-mode", "bw", "--binarize", "otsu"]),
        ("illumination", source, ["--illumination", "surface", "--background-cell", "8"]),
        ("denoising", source, ["--denoise", "nlm", "--nlm-patch", "3", "--nlm-search", "7"]),
    )
    record_schema = Draft202012Validator(
        json.loads((ROOT / "schemas/run-record.schema.json").read_bytes())
    )
    for name, image, arguments in cases:
        output = root / name
        response = run_json(
            exe, "process", str(image), "--out-dir", str(output), *arguments, "--json"
        )
        inspect_run(name, output, response, psf, record_schema)
        verified = run_json(exe, "verify", str(output), "--json")
        inspect_verification(output, verified, response["record"])


def inspect_verification(output: Path, observed: dict[str, Any], record: dict[str, Any]) -> None:
    """A relocated verifier identifies the exact artifacts and acquired record bytes."""
    names = [record["output"]["path"], *(asset["path"] for asset in record["assets"]), "run.json"]
    if [item["path"] for item in observed["confirmed"]] != names:
        message = "Relocated verifier omitted, duplicated or reordered observed file roles"
        raise ValueError(message)
    for item in observed["confirmed"]:
        raw = (output / item["path"]).read_bytes()
        if item["bytes"] != len(raw) or item["sha256"] != hashlib.sha256(raw).hexdigest():
            message = "Relocated verifier observed identity differs from actual fixture bytes"
            raise ValueError(message)


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
    cli = json.loads((ROOT / "spec/cli-contract.json").read_bytes())
    expected = [
        catalog_projection(method)
        for method in catalog_entries(contract["families"], merged_options(cli, contract))
    ]
    if version["methods"] != expected or version["input_support"] != support_matrix(cli["formats"]):
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
