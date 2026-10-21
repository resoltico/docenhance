# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real admitted defaults compared independently against reviewed CLI declarations."""

from __future__ import annotations

import json
import tempfile
from fractions import Fraction
from pathlib import Path
from typing import Any

import test_cli
from continuous_fixtures import Fixture
from contract_reference import reviewed_options


def default_cases(exe: Path) -> None:
    """Compare actual admitted records with the defaults advertised in the reviewed contract."""
    all_options = reviewed_options()
    defaults = {item["name"]: item["default"] for item in all_options if "default" in item}
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source = root / "source.png"
        side = 32
        source.write_bytes(Fixture(side, side, ((200,),) * (side * side)).encoded())
        for descriptor in all_options:
            if "process" in descriptor["scope"] and descriptor["metavar"]:
                output = root / "refused"
                arguments = ["process", str(source)]
                if descriptor["name"] != "--out-dir":
                    arguments.extend(["--out-dir", str(output)])
                response = test_cli.call_json(
                    exe,
                    [*arguments, descriptor["name"], "", "--json"],
                    test_cli.EXIT_INVOCATION,
                )
                test_cli.expect(
                    response["publication"] == "not_started", "empty value refuses execution"
                )
                test_cli.expect(not output.exists(), "empty value publishes nothing")
        records = []
        for index, options in enumerate(
            (
                [],
                ["--output-mode", "bw"],
                ["--output-mode", "bw", "--binarize", "fixed"],
                ["--illumination", "surface"],
                ["--denoise", "nlm"],
            )
        ):
            out = root / str(index)
            test_cli.call_json(
                exe, ["process", str(source), "--out-dir", str(out), *options, "--json"]
            )
            test_cli.call_json(exe, ["verify", str(out), "--json"])
            records.append(json.loads((out / "run.json").read_text()))
        assert_defaults(records, defaults)


def assert_defaults(records: list[dict[str, Any]], defaults: dict[str, str]) -> None:
    """Compare current typed request defaults, including nullable disabled selections."""
    groups = (
        (
            records[0]["request"]["output"],
            {
                "mode": "--output-mode",
                "depth": "--bit-depth",
                "alpha": "--alpha",
                "profile_policy": "--profile-policy",
            },
        ),
        (
            records[1]["request"]["binarization"]["parameters"],
            {"window": "--sauvola-window", "k": "--sauvola-k", "r": "--sauvola-r"},
        ),
        (records[2]["request"]["binarization"]["parameters"], {"threshold": "--fixed-threshold"}),
        (
            records[3]["request"]["illumination"]["parameters"],
            {
                "strength": "--background-strength",
                "max_gain": "--background-max-gain",
                "target": "--background-target",
                "cell": "--background-cell",
                "quantile": "--background-quantile",
                "smooth": "--background-smooth",
            },
        ),
        (
            records[4]["request"]["denoising"]["parameters"],
            {
                "h": "--nlm-h",
                "patch": "--nlm-patch",
                "search": "--nlm-search",
                "blend": "--denoise-blend",
            },
        ),
    )
    for actual, names in groups:
        for key, option in names.items():
            value = defaults[option]
            expected = float(Fraction(value)) if isinstance(actual[key], (int, float)) else value
            test_cli.expect(
                actual[key] == expected, f"{option}: executed default matches reviewed default"
            )
    test_cli.expect(records[1]["request"]["binarization"]["method"] == "B02", "default bw is B02")
    test_cli.expect(defaults["--binarize"] == "sauvola", "reviewed default binary selector")
    for stage, option in (("illumination", "--illumination"), ("denoising", "--denoise")):
        test_cli.expect(defaults[option] == "off", f"{stage} defaults to off")
        test_cli.expect(
            records[0]["execution"][stage] is None and records[0]["request"][stage] is None,
            f"{stage} remains disabled",
        )
    test_cli.expect(records[0]["request"]["protection"] is None, "default has no mask")


def execution_cases(exe: Path) -> None:
    """Exercise real process-only settings and truthful response-only diagnostics."""
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source = root / "source.png"
        source.write_bytes(Fixture(8, 8, ((127,),) * 64).encoded())
        for index, (threads, memory) in enumerate(
            (("1", "64"), ("007", "4096"), ("256", "262144"), ("auto", "1024"))
        ):
            output = root / f"result-{index}"
            response = test_cli.call_json(
                exe,
                [
                    "process",
                    str(source),
                    "--out-dir",
                    str(output),
                    "--threads",
                    threads,
                    "--memory-limit",
                    memory,
                    "--json",
                ],
            )
            actual = response["diagnostics"]
            test_cli.expect(
                actual["memory_limit_mib"] == int(memory), "actual selected memory limit"
            )
            test_cli.expect(
                1 <= actual["threads"] <= (256 if threads == "auto" else int(threads)),
                "actual resolved worker bound",
            )
            test_cli.expect(
                0 < actual["charged_peak_bytes"] <= int(memory) * 1024 * 1024,
                "actual charged high water stays within selected budget",
            )
            record = json.loads((output / "run.json").read_text())
            test_cli.expect(
                not {"threads", "memory_limit_mib", "charged_peak_bytes"} & record.keys(),
                "execution settings remain response diagnostics",
            )
            test_cli.call_json(exe, ["verify", str(output), "--json"])
        for option, values in (
            ("--threads", ("0", "257", "+1", "1.0", "1e2", "AUTO")),
            ("--memory-limit", ("63", "262145", "+64", "64.0", "64e0", "auto")),
        ):
            for index, value in enumerate(values):
                output = root / f"refused-{option}-{index}"
                response = test_cli.call_json(
                    exe,
                    ["process", str(source), "--out-dir", str(output), option, value, "--json"],
                    test_cli.EXIT_INVOCATION,
                )
                test_cli.expect(
                    response["publication"] == "not_started" and not output.exists(),
                    "invalid execution settings refuse before effects",
                )
            test_cli.call_json(
                exe,
                ["verify", str(root / "result-0"), option, "1", "--json"],
                test_cli.EXIT_INVOCATION,
            )
