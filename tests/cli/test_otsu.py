#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent global Otsu references through actual admission and published bundles."""

from __future__ import annotations

import copy
import json
import sys
import tempfile
from collections import Counter
from pathlib import Path

from continuous_fixtures import GRAY_ALPHA, RGB, Fixture
from grayscale import read_image, write_image
from test_cli import call_json, expect
from test_sauvola import command

BINS = 4096
WHITE = 255
FALLBACK = 2047
TOLERANCE = 1e-12
PLATEAU_THRESHOLD = 161


def oracle(rows: list[bytes]) -> tuple[int, bool, list[bytes]]:
    """Use independently enumerated classes and exact integer moments before float64 scoring."""
    quantized = [[round((BINS - 1) * sample / WHITE) for sample in row] for row in rows]
    counts = Counter(value for row in quantized for value in row)
    fallback = len(counts) == 1
    threshold = FALLBACK
    if not fallback:
        total = sum(counts.values())
        scores = []
        # Every candidate forms its own populations, rather than production's prefix recurrence.
        for candidate in range(min(counts), max(counts)):
            low = [(value, count) for value, count in counts.items() if value <= candidate]
            high = [(value, count) for value, count in counts.items() if value > candidate]
            n0, n1 = sum(count for _, count in low), sum(count for _, count in high)
            mean0 = sum(value * count for value, count in low) / n0
            mean1 = sum(value * count for value, count in high) / n1
            score = (n0 / total) * (n1 / total) * (mean0 - mean1) ** 2
            scores.append((candidate, score))
        best = max(score for _, score in scores)
        threshold = next(t for t, score in scores if best - score <= TOLERANCE * max(1, best))
    output = [bytes(0 if q <= threshold else WHITE for q in row) for row in quantized]
    return threshold, fallback, output


def check_result(exe: Path, source: Path, output: Path, rows: list[bytes]) -> None:
    """Require exact pixels, fitted observations, frozen record facts and source preservation."""
    original = source.read_bytes()
    response = call_json(exe, command(source, output, "otsu"))
    threshold, fallback, expected = oracle(rows)
    expect(response["method"] == "B01" and response["method_version"] == 1, "B01 identity")
    expect(response["publication"] == "completed", "B01 complete publication")
    observations = {"threshold_bin": threshold, "single_bin_fallback": fallback}
    expect(response["binarization"] == observations, "independent fitted observations")
    expect(read_image(output / "result.png") == expected, "exact binary polarity and split")
    expect(source.read_bytes() == original, "source preserved")
    record = json.loads((output / "run.json").read_bytes())
    expect(record["request"]["operation"]["parameters"] == {}, "parameter-free Otsu")
    expect(record["execution"]["binarization"] == observations, "record fitted observations")
    call_json(exe, ["verify", str(output), "--json"])


def sample_cases(exe: Path, root: Path) -> Path:
    """Check all byte bins, empty-bin tie plateaus, skewed populations and fallback endpoints."""
    cases = [
        [bytes(range(256))],
        [bytes((10, 200))],
        [bytes((0, 0, 1, 1, 2, 70, 130, 250, 255))],
        [bytes((0, 64, 128, 192, 255)), bytes((255, 192, 128, 64, 0))],
        *[[bytes((sample,))] for sample in (0, 127, 128, 255)],
    ]
    for number, rows in enumerate(cases):
        source, output = root / f"case-{number}.png", root / f"case-{number}"
        write_image(source, rows)
        check_result(exe, source, output, rows)
    for depth in (1, 2, 4):
        maximum = (1 << depth) - 1
        samples = tuple((p,) for p in range(maximum + 1))
        source = root / f"packed-{depth}.png"
        source.write_bytes(Fixture(len(samples), 1, samples, depth=depth).encoded())
        rows = [bytes(p * WHITE // maximum for (p,) in samples)]
        check_result(exe, source, root / f"packed-{depth}", rows)
    expect(
        oracle([bytes((10, 200))])[0] == PLATEAU_THRESHOLD,
        "smallest empty-bin plateau threshold",
    )
    return root / "case-7"


def rejection_cases(exe: Path, root: Path) -> None:
    """Wrong method options and broader pipeline declarations are refused before effects."""
    output = root / "refused"
    for option, default in (
        ("--fixed-threshold", "0.5"),
        ("--sauvola-window", "31"),
        ("--sauvola-k", "0.2"),
        ("--sauvola-r", "0.5"),
        ("--illumination", "off"),
        ("--protect-mask", "absent.png"),
    ):
        for value in (default, ""):
            response = call_json(
                exe, [*command(root / "absent.png", output, "otsu"), option, value], 2
            )
            expect(response["publication"] == "not_started", "invalid options precede input I/O")
            expect(not output.exists(), "no refused output")
    fixtures = (
        Fixture(1, 1, ((128,),), depth=16),
        Fixture(1, 1, ((10, 20, 30),), color=RGB),
        Fixture(1, 1, ((128, 255),), color=GRAY_ALPHA),
        Fixture(1, 1, ((128,),), metadata=((b"tRNS", b"\0\x80"),)),
    )
    for number, fixture in enumerate(fixtures):
        source = root / f"unsupported-{number}.png"
        source.write_bytes(fixture.encoded())
        response = call_json(exe, command(source, output, "otsu"), 3)
        expect(response["publication"] == "not_started", "unchanged binary input domain")
        expect(not output.exists(), "unsupported source unpublished")


def malformed_records(exe: Path, directory: Path) -> None:
    """Reject obsolete versions, wrong alternatives, unknown fields and impossible fallback."""
    path = directory / "run.json"
    original = path.read_bytes()
    record = json.loads(original)
    for field, value in (
        ("threshold_bin", -1),
        ("threshold_bin", BINS - 1),
        ("threshold_bin", True),
        ("threshold_bin", 0),
        ("single_bin_fallback", "true"),
        ("unknown", 1),
    ):
        changed = copy.deepcopy(record)
        changed["execution"]["binarization"][field] = value
        path.write_text(json.dumps(changed), encoding="utf-8")
        call_json(exe, ["verify", str(directory), "--json"], 3)
    for section, key, altered_value in (
        ("record", "version", 9),
        ("execution", "binarization", None),
    ):
        changed = copy.deepcopy(record)
        changed[section][key] = altered_value
        path.write_text(json.dumps(changed), encoding="utf-8")
        call_json(exe, ["verify", str(directory), "--json"], 3)
    for altered_parameters in ({"threshold": 0.5}, {"unknown": 1}):
        changed = copy.deepcopy(record)
        changed["request"]["operation"]["parameters"] = altered_parameters
        path.write_text(json.dumps(changed), encoding="utf-8")
        call_json(exe, ["verify", str(directory), "--json"], 3)
    changed = copy.deepcopy(record)
    changed["request"]["operation"]["method"]["id"] = "B03"
    changed["request"]["operation"]["parameters"] = {"threshold": 0.5}
    path.write_text(json.dumps(changed), encoding="utf-8")
    call_json(exe, ["verify", str(directory), "--json"], 3)
    path.write_bytes(original)
    call_json(exe, ["verify", str(directory), "--json"])


def main() -> None:
    """Execute independent references and contract failures against the built production binary."""
    exe = Path(sys.argv[1]).resolve()
    response = call_json(exe, ["methods", "B01", "--json"])
    expect(response["methods"] == [{"id": "B01", "method_version": 1}], "B01 discovery")
    with tempfile.TemporaryDirectory(prefix="docenhance-otsu-") as temporary:
        root = Path(temporary)
        fallback = sample_cases(exe, root)
        rejection_cases(exe, root)
        malformed_records(exe, fallback)
    print("PASS: B01 Otsu references, deterministic splits, fallback, domain and record admission")


if __name__ == "__main__":
    main()
