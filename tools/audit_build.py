#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Audit configured dependency feature values and OpenCV's actual module closure."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from hardening_compilation import errors as hardening_errors

ROOT = Path(__file__).resolve().parents[1]
FALSE = {"0", "OFF", "FALSE", "NO", "N", "IGNORE", "NOTFOUND", ""}
# Independent reference for the reviewed corrected 3.12.0 single header. This proves identity;
# native allocation-refusal tests separately prove cleanup behavior.
TIFF_COMPLETE_ZIP_SHA256 = "551b35ed562b1ebbb5e4577afc5fc57d195667b1294b6a31123fac0ec2810275"
TIFF_CHARGED_JPEG_SHA256 = "c51749f755cf1fe0fcca8bfd02be1dedeaa5c8affceac22c89b43cd6acd741b7"
# Identity of reviewed DFT ownership and typed dispatch; native probes establish behavior.
OPENCV_OWNED_DXT_SHA256 = "0d2828dede76738b8b23f882ac0af3a00653ee3e50bec664027ed750af835c50"
JSON_BOUNDED_HEADER_SHA256 = "f504f6fa07b84e3e264a1f7757f15da1502bb539285c90e908c1cabab47b572e"


def read_cache(path: Path) -> dict[str, str]:
    """Parse KEY:TYPE=VALUE entries of a CMakeCache.txt."""
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith(("#", "//")) or ":" not in line or "=" not in line:
            continue
        left, value = line.split("=", 1)
        values[left.split(":", 1)[0]] = value
    return values


def json_header_failures(binary: Path) -> list[str]:
    """Compare installed JSON bytes with the actual corrected dependency build input."""
    owned = binary / "owned-source/single_include/nlohmann/json.hpp"
    installed = binary.parents[1] / "prefix" / "include" / "nlohmann" / "json.hpp"
    if not owned.is_file() or not installed.is_file():
        return ["JSON: missing reviewed or installed bounded-destruction header"]
    if owned.read_bytes() != installed.read_bytes():
        return ["JSON: installed header differs from the reviewed private copy"]
    actual = hashlib.sha256(installed.read_bytes()).hexdigest()
    if actual != JSON_BOUNDED_HEADER_SHA256:
        return [f"JSON: header differs from reviewed output digest: {actual}"]
    return []


def zlib_stream_failures(binary: Path) -> list[str]:
    """The private compressor/decompressor never compiles the unused gzip-file adapter."""
    commands = json.loads((binary / "compile_commands.json").read_text(encoding="utf-8"))
    excluded = {"gzclose.c", "gzlib.c", "gzread.c", "gzwrite.c"}
    return (
        ["zlib: excluded gzip-file code is compiled"]
        if any(Path(entry["file"]).name in excluded for entry in commands)
        else []
    )


def jpeg_turbo_failures(binary: Path) -> list[str]:
    """Reject the TurboJPEG compression entry point in actual compiled/installable code.

    OSV-2026-1068's reported path is tj3Compress8 -> jpeg_abort. The classic
    decompressor also links jpeg_abort, so checking that symbol alone is NOT
    an adequate test; the entire TurboJPEG API translation unit must be absent.
    """
    commands = json.loads((binary / "compile_commands.json").read_text(encoding="utf-8"))
    turbo_sources = {"turbojpeg.c", "turbojpeg-mp.c"}
    if any(Path(entry["file"]).name in turbo_sources for entry in commands):
        return ["jpeg: TurboJPEG compression translation unit is compiled"]
    library_directory = binary.parents[1] / "prefix" / "lib"
    if not library_directory.is_dir():
        return ["jpeg: private installed library directory is missing"]
    if any(
        path.is_file() and path.name.lower().lstrip("lib").startswith("turbojpeg")
        for path in library_directory.iterdir()
    ):
        return ["jpeg: TurboJPEG API library is installed"]
    return []


def tiff_recipe_failures(binary: Path) -> list[str]:
    """The installed per-handle ABI and actual JPEG source must be the reviewed adaptation."""
    owned = binary / "owned-source/libtiff/tif_jpeg.c"
    header = binary / "owned-source/libtiff/docenhance_tiff.h"
    installed = binary.parents[1] / "prefix/include/docenhance_tiff.h"
    if not owned.is_file() or not header.is_file() or not installed.is_file():
        return ["TIFF: missing charged JPEG source or installed installer ABI"]
    failures = []
    if header.read_bytes() != installed.read_bytes():
        failures.append("TIFF: installed installer ABI differs from its build input")
    if hashlib.sha256(owned.read_bytes()).hexdigest() != TIFF_CHARGED_JPEG_SHA256:
        failures.append("TIFF: JPEG source differs from the reviewed adaptation")
    commands = json.loads((binary / "compile_commands.json").read_text(encoding="utf-8"))
    compiled = [
        Path(entry["file"]).resolve()
        for entry in commands
        if Path(entry["file"]).name == "tif_jpeg.c"
    ]
    if compiled != [owned.resolve()]:
        failures.append("TIFF: JPEG compilation does not use exactly the reviewed source")
    source = owned.read_text(encoding="utf-8")
    creation = source.find("static int TIFFjpeg_create_decompress(JPEGState *sp)")
    end = source.find("static int TIFFjpeg_set_defaults", creation)
    if creation < 0 or end < 0:
        return [*failures, "TIFF: JPEG bootstrap framing is absent"]
    bootstrap = source[creation:end]
    if bootstrap.count("memory->install(memory->context, &sp->cinfo.d)") != 1:
        failures.append("TIFF: charged JPEG bootstrap is absent or duplicated")
    return failures


def tiff_zip_failures(binary: Path) -> list[str]:
    """Deflate pixel counts do not replace the strict complete-strile decoder source."""
    source = binary / "owned-source/libtiff/tif_zip.c"
    if not source.is_file():
        return ["TIFF: missing complete Deflate decoder source"]
    failures = []
    if hashlib.sha256(source.read_bytes()).hexdigest() != TIFF_COMPLETE_ZIP_SHA256:
        failures.append("TIFF: Deflate source differs from the reviewed adaptation")
    commands = json.loads((binary / "compile_commands.json").read_text(encoding="utf-8"))
    compiled = [
        Path(entry["file"]).resolve()
        for entry in commands
        if Path(entry["file"]).name == "tif_zip.c"
    ]
    if compiled != [source.resolve()]:
        failures.append("TIFF: Deflate compilation does not use exactly the reviewed source")
    return failures


def opencv_dft_failures(binary: Path, source_directory: Path, original_digest: str) -> list[str]:
    """Bind the unchanged locked FFT source and the actual corrected compilation input."""
    original = source_directory / "modules/core/src" / "dxt.cpp"
    owned = binary / "owned-source/dxt.cpp"
    if not original.is_file() or not owned.is_file():
        return ["OpenCV: missing locked or corrected DFT context source"]
    failures = []
    if hashlib.sha256(original.read_bytes()).hexdigest() != original_digest:
        failures.append("OpenCV: original FFT source differs from its reviewed lock binding")
    if hashlib.sha256(owned.read_bytes()).hexdigest() != OPENCV_OWNED_DXT_SHA256:
        failures.append("OpenCV: FFT source differs from reviewed ownership and typed dispatch")
    commands = json.loads((binary / "compile_commands.json").read_text(encoding="utf-8"))
    compiled = [
        Path(entry["file"]).resolve() for entry in commands if Path(entry["file"]).name == "dxt.cpp"
    ]
    if compiled != [owned.resolve()]:
        failures.append("OpenCV: FFT compilation does not use exactly the corrected private source")
    return failures


def source_recipe_failures(name: str, binary: Path) -> list[str]:
    """Inspect private upstream adaptations independently of feature cache values."""
    if name == "tiff":
        return [*tiff_recipe_failures(binary), *tiff_zip_failures(binary)]
    if name == "json":
        return json_header_failures(binary)
    if name == "zlib":
        return zlib_stream_failures(binary)
    if name == "jpeg":
        return jpeg_turbo_failures(binary)
    return []


def feature_failures(
    name: str, settings: dict[str, bool | str], cache: dict[str, str], binary: Path
) -> list[str]:
    """Compare one dependency's configured cache with its declared feature settings."""
    failures = []
    for key, expected in settings.items():
        if key not in cache:
            failures.append(f"{name}: missing declared feature {key}")
            continue
        actual = cache[key]
        if isinstance(expected, bool):
            actual_bool = actual.upper() not in FALSE and not actual.endswith("-NOTFOUND")
            if actual_bool != expected:
                failures.append(f"{name}: unexpected {key}={actual}")
        else:
            resolved = expected.replace("<BINARY>", binary.as_posix())
            if actual.replace("\\", "/") != resolved:
                failures.append(f"{name}: unexpected {key}={actual}; expected {resolved}")
    if name == "opencv":
        modules = set(cache.get("OPENCV_MODULES_BUILD", "").split(";"))
        expected_modules = {"opencv_" + part for part in str(settings["BUILD_LIST"]).split(",")}
        failures.extend(
            opencv_dft_failures(
                binary,
                Path(cache.get("CMAKE_HOME_DIRECTORY", "")),
                str(settings["DOCENHANCE_OPENCV_DXT_SHA256"]),
            )
        )
        if modules != expected_modules:
            failures.append(f"OpenCV module closure differs: {sorted(modules)}")
    failures.extend(source_recipe_failures(name, binary))
    if cache.get("BUILD_SHARED_LIBS", "").upper() not in FALSE:
        failures.append(f"{name}: unexpected shared-library build")
    return failures


def audit(build: Path) -> list[str]:
    """Audit every configured dependency in a superbuild tree."""
    policy = json.loads((ROOT / "deps/features.json").read_text(encoding="utf-8"))
    features = policy["dependencies"]
    binding = json.loads((build / "build-identity.json").read_text(encoding="utf-8"))
    if binding["CMAKE_SYSTEM_NAME"] in {"Darwin", "Linux"}:
        for name, settings in policy["unix_dependencies"].items():
            features[name].update(settings)
    selected = json.loads((build / "dependency-plan.json").read_text(encoding="utf-8"))
    if not isinstance(selected, list) or not selected or len(set(selected)) != len(selected):
        return ["Invalid or empty configured dependency plan"]
    failures = []
    for name in selected:
        if name not in features:
            failures.append(f"Unknown configured dependency: {name}")
            continue
        binary = build / "deps" / name
        path = binary / "CMakeCache.txt"
        if not path.exists():
            failures.append(f"Missing configured dependency cache: {name}")
            continue
        cache = read_cache(path)
        if name not in {"cli11", "json", "picosha2"}:
            failures.extend(hardening_errors(binary, build / "hardening-policy.json"))
        failures.extend(feature_failures(name, features[name], cache, binary))
        for key in (
            "ZLIB_LIBRARY",
            "ZLIB_LIBRARY_RELEASE",
            "ZLIB_LIBRARY_DEBUG",
            "ZLIB_INCLUDE_DIR",
            "JPEG_LIBRARY",
            "JPEG_LIBRARY_RELEASE",
            "JPEG_LIBRARY_DEBUG",
            "JPEG_INCLUDE_DIR",
        ):
            value = cache.get(key, "")
            if (
                value
                and not value.endswith("-NOTFOUND")
                and not Path(value).resolve().is_relative_to((build / "prefix").resolve())
            ):
                failures.append(f"{name}: foreign dependency provider {key}={value}")
    return failures


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    errors = audit(args.build)
    print("\n".join(errors) if errors else "PASS: dependency feature and module-closure audit")
    raise SystemExit(bool(errors))
