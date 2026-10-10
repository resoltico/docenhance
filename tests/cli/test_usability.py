#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent human-presentation and argv-role regressions against the real executable."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

from continuous_fixtures import RGBA, Fixture
from test_cli import call_json, expect

HELP_WIDTH = 96

# Each oracle names the user-visible option and its reviewed domain, independently of parsers.
VALUE_CASES = (
    (("--contrast", "gamma"), "--gamma", "5", "[0.25,4]"),
    (("--contrast", "gamma"), "--gamma", "NaN", "[0.25,4]"),
    (("--contrast", "gamma"), "--gamma", "", "[0.25,4]"),
    (("--contrast", "gamma"), "--gamma", "1,2", "[0.25,4]"),
    (("--contrast", "gamma"), "--gamma", "+1.2", "[0.25,4]"),
    (("--contrast", "gamma"), "--gamma", "1e999", "[0.25,4]"),
    (("--contrast", "gamma"), "--gamma", "1e-999", "[0.25,4]"),
    (("--denoise", "nlm"), "--nlm-h", "inf", "[0.1,25]"),
    (("--denoise", "nlm"), "--nlm-patch", "4", "[3,15]"),
    (("--denoise", "nlm"), "--nlm-search", "3.0", "[7,41]"),
    (("--denoise", "tvl1"), "--tv-iterations", "9", "[10,1000]"),
    (("--denoise", "tvl1"), "--tv-tolerance", "0", "[1e-8,1e-3]"),
    (("--illumination", "surface"), "--background-quantile", "0.5", "[0.75,0.99]"),
    (("--illumination", "surface"), "--background-cell", "7", "[8,512]"),
    (("--illumination", "morph"), "--background-radius", "0", "[1,256]"),
    (("--illumination", "morph"), "--background-target", "0", "[0.1,1]"),
    (("--deblur", "wiener", "--psf", "gaussian"), "--psf-sigma", "6", "[0.3,5]"),
    (("--sharpen", "unsharp"), "--sharpen-amount", "3", "[0,2]"),
    (("--output-mode", "bw", "--binarize", "fixed"), "--fixed-threshold", "128", "[0,1]"),
    (("--output-mode", "bw"), "--sauvola-window", "4", "[3,4095]"),
    ((), "--bit-depth", "12", "auto|8|16"),
    ((), "--alpha", "noop", "white|black|reject"),
    ((), "--profile-policy", "assume", "embedded|srgb"),
    (("--contrast", "gamma"), "--levels-low", "0.5", "--contrast levels"),
    (("--illumination", "morph"), "--background-cell", "16", "surface or auto"),
)


def invoke(exe: Path, args: list[str], root: Path, code: int) -> subprocess.CompletedProcess[str]:
    """Observe both streams and process status without inferring effects from missing responses."""
    result = subprocess.run(
        [str(exe), *args],
        cwd=root,
        text=True,
        encoding="utf-8",
        capture_output=True,
        timeout=30,
        check=False,
    )
    expect(
        result.returncode == code, f"{args}: {result.returncode}: {result.stdout} {result.stderr}"
    )
    return result


def help_cases(exe: Path, root: Path) -> None:
    """Read-only help exposes complete reviewed defaults/domains with readable wrapping."""
    before = set(root.iterdir())
    result = invoke(exe, ["process", "missing-input.png", "--help"], root, 0)
    expect(not result.stderr and set(root.iterdir()) == before, "help stays read-only")
    text = result.stdout
    for name, tokens in (
        ("--sauvola-window", ("31", "odd integer", "[3,4095]")),
        ("--gamma", ("1.2", "finite", "[0.25,4]")),
        ("--alpha", ("white; white|black|reject",)),
        ("--out-dir", ("Required; a new result directory",)),
    ):
        entry = text.split("\n" + name + " ", 1)[1].split("\n--", 1)[0]
        entry = " ".join(entry.split())
        for token in tokens:
            expect(token in entry, f"{name}: help omitted {token}")
    expect(
        all(len(line) <= HELP_WIDTH for line in text.splitlines() if line.startswith("    ")),
        "help descriptions/domain lines wrap within 96 columns",
    )
    for flag in ("--json=true", "--json=", "--json={}"):
        call_json(exe, ["version", flag])
        call_json(exe, ["process", "not-opened.png", flag], 2)
        refusal = invoke(exe, ["version", flag, "--wat"], root, 2)
        expect(
            not refusal.stdout and "E_ARGUMENT:" in refusal.stderr,
            "syntax fallback requires the exact token, even with a default-valued flag",
        )
    response = call_json(exe, ["process", "--help", "--json"])
    mode = next(o for o in response["options"] if o["name"] == "--output-mode")
    expect("TIFF/BigTIFF" in mode["description"], "JSON help advertises implemented TIFF sources")
    expect(
        "restoration, contrast, sharpening and final quantization" in mode["description"],
        "JSON help reports current stage order",
    )


def value_cases(exe: Path, root: Path, source: Path) -> None:
    """Each real admission failure identifies the option, safely rendered value and correction."""
    for index, (selected, name, value, domain) in enumerate(VALUE_CASES):
        target = root / f"refused-{index}"
        args = ["process", str(source), "--out-dir", str(target), *selected, name, value]
        result = invoke(exe, args, root, 2)
        expect(not result.stdout, "human errors select stderr")
        for token in (name, domain, "Publication: not_started", f'"{value}"'):
            expect(token in result.stderr, f"{name} diagnostic omitted {token}: {result.stderr}")
        response = call_json(exe, [*args, "--json"], 2)
        expect(
            response["error"]["code"] == "E_ARGUMENT" and response["publication"] == "not_started",
            "strict admission before effects",
        )
        for token in (name, domain, f'"{value}"'):
            expect(token in response["error"]["message"], f"JSON diagnostic omitted {token}")
        expect(not target.exists(), "invalid values have no publication")
    for value in ("1\n2", '1"2', "1\\2"):
        response = call_json(
            exe,
            [
                "process",
                str(source),
                "--out-dir",
                str(root / "escaped"),
                "--contrast",
                "gamma",
                "--gamma",
                value,
                "--json",
            ],
            2,
        )
        message = response["error"]["message"]
        expect("--gamma" in message and "[0.25,4]" in message, "escaped value keeps context")
        expect("\n" not in message, "control byte cannot inject a diagnostic line")


def role_cases(exe: Path, root: Path, source: Path) -> None:
    """Option-looking values, flags, delimiter operands and failed syntax have distinct roles."""
    for name, options, json_mode in (
        ("separate", ["--out-dir", "--json"], False),
        ("joined", ["--out-dir=--json"], False),
        ("flag", ["--out-dir", "--json", "--json"], True),
        ("joined-flag", ["--out-dir=--json", "--json"], True),
    ):
        cwd = root / name
        cwd.mkdir()
        result = invoke(exe, ["process", str(source), *options], cwd, 0)
        expect(not result.stderr, "successful role case has one stdout response")
        expect(result.stdout.startswith("{") == json_mode, "only parsed flag selects JSON")
        target = cwd / "--json"
        expect((target / "result.png").is_file(), "literal path retained")
        if json_mode:
            response = json.loads(result.stdout)
            expect(
                response["output"] == str(Path("--json") / "result.png"),
                "reported identity unchanged",
            )
        call_json(exe, ["verify", str(target), "--json"])
    # Successful parsing followed by admission failure must also use the parsed flag role.
    for index, options in enumerate((["--out-dir", "--json"], ["--out-dir=--json"])):
        cwd = root / f"admission-{index}"
        cwd.mkdir()
        result = invoke(exe, ["process", str(source), *options, "--output-mode", "invalid"], cwd, 2)
        expect(
            not result.stdout and "Publication: not_started" in result.stderr,
            "value token must not enable JSON after successful syntax parsing",
        )
        expect(not list(cwd.iterdir()), "failed admission never performs path effects")
    for name, args, json_mode in (
        ("missing", ["process", str(source), "--out-dir"], False),
        ("missing-json", ["process", str(source), "--json", "--out-dir"], True),
        ("syntax-fallback", ["process", str(source), "--out-dir", "--json", "--wat"], True),
        ("delimiter-failure", ["version", "--", "--json"], False),
        ("delimiter-extra", ["process", str(source), "--out-dir", "result", "--", "--json"], False),
        ("repeat", ["version", "--json", "--json"], True),
    ):
        cwd = root / name
        cwd.mkdir()
        result = invoke(exe, args, cwd, 2)
        expect(result.stdout.startswith("{") == json_mode, "syntax fallback honors delimiter")
        expect(bool(result.stderr) != json_mode, "syntax refusal routes one response")
        expect(not list(cwd.iterdir()), "syntax refusal performs no filesystem work")
    for json_mode in (False, True):
        cwd = root / f"operand-{json_mode}"
        cwd.mkdir()
        (cwd / "--json").write_bytes(source.read_bytes())
        args = [
            "process",
            "--out-dir",
            "output",
            *(["--json"] if json_mode else []),
            "--",
            "--json",
        ]
        result = invoke(exe, args, cwd, 0)
        expect(result.stdout.startswith("{") == json_mode, "delimiter path is an operand")
        expect((cwd / "--json").read_bytes() == source.read_bytes(), "operand bytes preserved")
        call_json(exe, ["verify", str(cwd / "output"), "--json"])


def warning_cases(exe: Path, root: Path, gray: Path) -> None:
    """Human explanations derive from actual counts/policy and retain machine warning arrays."""
    alpha = root / "alpha.png"
    alpha.write_bytes(
        Fixture(
            2, 1, ((65535, 0, 0, 32768), (30000, 30000, 30000, 65535)), color=RGBA, depth=16
        ).encoded()
    )
    for matte in ("white", "black"):
        result = invoke(
            exe,
            [
                "process",
                str(alpha),
                "--out-dir",
                str(root / matte),
                "--alpha",
                matte,
                "--bit-depth",
                "8",
            ],
            root,
            0,
        )
        for token in (
            "W_PROFILE_ASSUMED:",
            "transfer=assumed",
            "W_ALPHA_FLATTENED:",
            f"1 non-opaque pixels composited over {matte}",
            "original alpha cannot be recovered",
            "W_DEPTH_REDUCED:",
            "16-bit decoded samples reduced to 8 bits",
        ):
            expect(token in result.stdout, f"warning omitted {token}")
        response = call_json(
            exe,
            [
                "process",
                str(alpha),
                "--out-dir",
                str(root / (matte + "-json")),
                "--alpha",
                matte,
                "--bit-depth",
                "8",
                "--json",
            ],
        )
        expect(
            response["conversion"]["warnings"]
            == ["W_PROFILE_ASSUMED", "W_ALPHA_FLATTENED", "W_DEPTH_REDUCED"],
            "machine warning vocabulary/observations remain coherent",
        )
    result = invoke(
        exe,
        ["process", str(gray), "--out-dir", str(root / "overridden"), "--profile-policy", "srgb"],
        root,
        0,
    )
    expect(
        "W_PROFILE_OVERRIDDEN: The selected sRGB policy overrides" in result.stdout,
        "override explanation",
    )
    result = invoke(
        exe,
        [
            "process",
            str(gray),
            "--out-dir",
            str(root / "tv"),
            "--denoise",
            "tvl1",
            "--tv-iterations",
            "10",
        ],
        root,
        0,
    )
    for token in (
        "W_TV_ITERATION_LIMIT:",
        "usable iterate",
        "tolerance was not met",
        "not a convergence claim",
    ):
        expect(token in result.stdout, f"TV-L1 explanation omitted {token}")
    for amount in ("0", "0.5"):
        result = invoke(
            exe,
            [
                "process",
                str(gray),
                "--out-dir",
                str(root / ("sharp-" + amount)),
                "--sharpen",
                "unsharp",
                "--sharpen-amount",
                amount,
            ],
            root,
            0,
        )
        expect(
            ("W_SHARPENING:" in result.stdout) == (amount != "0"), "warning honors amount identity"
        )
        if amount != "0":
            for token in ("faint marks", "below zero=", "above one="):
                expect(token in result.stdout, f"sharpening explanation omitted {token}")
    result = invoke(
        exe,
        [
            "process",
            str(gray),
            "--out-dir",
            str(root / "restoration"),
            "--deblur",
            "wiener",
            "--psf",
            "gaussian",
            "--deblur-blend",
            "0",
        ],
        root,
        0,
    )
    expect(
        "W_RESTORATION_INFERENCE" in result.stdout and "inference" in result.stdout,
        "identity restoration retains inference warning",
    )


def main() -> int:
    """Exercise every independent presentation oracle without audit reports as test inputs."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source = root / "source.png"
        source.write_bytes(
            Fixture(
                32, 32, tuple((80 + (5 * x + 3 * y) % 175,) for y in range(32) for x in range(32))
            ).encoded()
        )
        original = source.read_bytes()
        help_cases(exe, root)
        value_cases(exe, root, source)
        role_cases(exe, root, source)
        warning_cases(exe, root, source)
        expect(source.read_bytes() == original, "presentation regressions preserve source")
    print("PASS: human presentation and token roles")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
