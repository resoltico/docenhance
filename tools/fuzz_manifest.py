#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Authoritative harness declarations and bounded campaign policy, shared with CMake."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SECONDS = 60
DEFAULT_JOBS = 2
MAX_SECONDS = 1800
MAX_JOBS = 2
ENGINE_GRACE = 120
CTEST_GRACE = 30
CAMPAIGN_GRACE = 60
MAX_INPUT_BYTES = 65536
IDENTIFIER = re.compile(r"[a-z][a-z0-9_]*")
LINK = re.compile(r"[A-Za-z_][A-Za-z0-9_:]*")


class FuzzError(ValueError):
    """A fuzz declaration, input, or execution result cannot establish a valid campaign."""


@dataclass(frozen=True)
class Target:
    """One harness, compiled and replayed from the same source and direct dependencies."""

    name: str
    source: str
    links: tuple[str, ...]
    max_len: int
    full_length: bool = False


def positive(value: int, maximum: int, label: str) -> int:
    """Reject booleans, zero/unbounded values, negatives and excessive requests."""
    if type(value) is not int or not 1 <= value <= maximum:
        msg = f"{label} must be an integer in [1,{maximum}]"
        raise FuzzError(msg)
    return value


def duration(value: str) -> int:
    """Parse the bounded engine time for argparse."""
    return positive(int(value), MAX_SECONDS, "seconds")


def concurrency(value: str) -> int:
    """Parse actual child-test parallelism for argparse."""
    return positive(int(value), MAX_JOBS, "jobs")


def target_timeout(seconds: int) -> int:
    """Leave space for the engine watchdog and report writing inside each CTest timeout."""
    return positive(seconds, MAX_SECONDS, "seconds") + ENGINE_GRACE + CTEST_GRACE


def campaign_timeout(count: int, seconds: int, jobs: int) -> int:
    """Bound complete waves of child harnesses, not a hand-maintained target count."""
    positive(count, MAX_INPUT_BYTES, "target count")
    positive(jobs, MAX_JOBS, "jobs")
    return ((count + jobs - 1) // jobs) * target_timeout(seconds) + CAMPAIGN_GRACE


def unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    """Reject duplicate JSON keys rather than silently dropping a harness or setting."""
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            msg = f"Duplicate fuzz manifest key: {key}"
            raise FuzzError(msg)
        result[key] = value
    return result


def target_record(name: str, value: dict[str, Any]) -> Target:
    """Validate one closed harness declaration; no arbitrary engine flags are accepted."""
    required = {"source", "links", "max_len"}
    if not IDENTIFIER.fullmatch(name) or not isinstance(value, dict):
        msg = f"Invalid target declaration: {name}"
        raise FuzzError(msg)
    if not required <= value.keys() or value.keys() - required - {"full_length"}:
        msg = f"Invalid fields for target: {name}"
        raise FuzzError(msg)
    if value["source"] != f"{name}.cpp":
        msg = f"Target {name} must own exactly {name}.cpp"
        raise FuzzError(msg)
    links = value["links"]
    if (
        not isinstance(links, list)
        or not links
        or not all(isinstance(link, str) and LINK.fullmatch(link) for link in links)
        or len(set(links)) != len(links)
    ):
        msg = f"Target {name} needs unique, valid CMake links"
        raise FuzzError(msg)
    full_length = value.get("full_length", False)
    if type(full_length) is not bool:
        msg = f"Target {name}.full_length must be boolean"
        raise FuzzError(msg)
    return Target(
        name,
        value["source"],
        tuple(links),
        positive(value["max_len"], MAX_INPUT_BYTES, "max_len"),
        full_length,
    )


def settings(root: Path = ROOT) -> dict[str, Any]:
    """Load the closed manifest with duplicate-key rejection."""
    data = json.loads(
        (root / "fuzz/targets.json").read_text(encoding="utf-8"), object_pairs_hook=unique_object
    )
    if not isinstance(data, dict) or set(data) - {"$comment", "targets", "sanitizer_options"}:
        msg = "Invalid fuzz manifest object"
        raise FuzzError(msg)
    records = data.get("targets")
    options = data.get("sanitizer_options")
    if not isinstance(records, dict) or not records or not isinstance(options, dict):
        msg = "Fuzz manifest requires targets and sanitizer_options objects"
        raise FuzzError(msg)
    if set(options) != {"ASAN_OPTIONS", "UBSAN_OPTIONS", "TSAN_OPTIONS"} or not all(
        isinstance(value, str) and value for value in options.values()
    ):
        msg = "Invalid sanitizer options"
        raise FuzzError(msg)
    for name, value in records.items():
        target_record(name, value)
    return data


def targets(root: Path = ROOT) -> tuple[Target, ...]:
    """Return every declared harness in deterministic manifest order."""
    return tuple(target_record(name, value) for name, value in settings(root)["targets"].items())


def inventory_errors(root: Path = ROOT) -> list[str]:
    """Require exact harness/corpus ownership; reject symlinks and absent regression inputs."""
    declared = targets(root)
    expected = {target.source for target in declared}
    actual = {path.name for path in (root / "fuzz").glob("*.cpp")}
    errors = [] if actual == expected else ["Harness sources differ from fuzz/targets.json"]
    for target in declared:
        source = root / "fuzz" / target.source
        if source.is_symlink() or not source.is_file():
            errors.append(f"Harness source is not a regular owned file: {source}")
        for kind in ("corpus", "regressions"):
            directory = root / "fuzz" / kind / target.name
            if directory.is_symlink() or not directory.is_dir() or not any(directory.iterdir()):
                errors.append(f"Missing regular {kind} directory for {target.name}")
                continue
            errors.extend(
                f"Invalid or oversized {kind} input: {path}"
                for path in directory.iterdir()
                if path.is_symlink() or not path.is_file() or path.stat().st_size > target.max_len
            )
    for kind in ("corpus", "regressions"):
        actual_names = {path.name for path in (root / "fuzz" / kind).iterdir()}
        if actual_names != {target.name for target in declared}:
            errors.append(f"Unowned or missing {kind} entries")
    return errors


def main() -> int:
    """Print validated timeouts for CMake, or reject an incomplete source inventory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=duration, default=DEFAULT_SECONDS)
    parser.add_argument("--jobs", type=concurrency, default=DEFAULT_JOBS)
    args = parser.parse_args()
    try:
        errors = inventory_errors()
        if errors:
            raise FuzzError("; ".join(errors))
        print(
            json.dumps(
                {
                    "target_timeout": target_timeout(args.seconds),
                    "campaign_timeout": campaign_timeout(len(targets()), args.seconds, args.jobs),
                }
            )
        )
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"Invalid fuzz manifest: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
