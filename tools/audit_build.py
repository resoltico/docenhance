#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Audit configured dependency feature values and OpenCV's actual module closure."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FALSE = {"0", "OFF", "FALSE", "NO", "N", "IGNORE", "NOTFOUND", ""}


OPENCV_MODULES = {
    "opencv_core",
    "opencv_flann",
    "opencv_geometry",
    "opencv_imgproc",
    "opencv_photo",
}


def read_cache(path: Path) -> dict[str, str]:
    """Parse KEY:TYPE=VALUE entries of a CMakeCache.txt."""
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith(("#", "//")) or ":" not in line or "=" not in line:
            continue
        left, value = line.split("=", 1)
        values[left.split(":", 1)[0]] = value
    return values


def feature_failures(
    name: str, settings: dict[str, bool | str], cache: dict[str, str]
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
        elif "<BINARY>" not in expected and actual != expected:
            failures.append(f"{name}: unexpected {key}={actual}")
    if name == "opencv":
        modules = set(cache.get("OPENCV_MODULES_BUILD", "").split(";"))
        if modules != OPENCV_MODULES:
            failures.append(f"OpenCV module closure differs: {sorted(modules)}")
    if cache.get("BUILD_SHARED_LIBS", "").upper() not in FALSE:
        failures.append(f"{name}: unexpected shared-library build")
    return failures


def audit(build: Path) -> list[str]:
    """Audit every configured dependency in a superbuild tree."""
    features = json.loads((ROOT / "deps/features.json").read_text(encoding="utf-8"))["dependencies"]
    failures = []
    for name, settings in features.items():
        if name == "picosha2":
            continue
        path = build / "deps" / name / "CMakeCache.txt"
        if name == "catch2" and not path.exists():
            continue
        if not path.exists():
            failures.append(f"Missing configured dependency cache: {name}")
            continue
        failures.extend(feature_failures(name, settings, read_cache(path)))
    return failures


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    errors = audit(args.build)
    print("\n".join(errors) if errors else "PASS: dependency feature and module-closure audit")
    raise SystemExit(bool(errors))
