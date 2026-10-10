# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Wrong-method, option-presence and active scalar refusal scenarios."""

from __future__ import annotations

import json

from audit_paths import ROOT
from matrix_types import STAGES, Case, stage_options

INVALID = {
    "--fixed-threshold": ("B03", "-0.1", "1.1"),
    "--sauvola-window": ("B02", "2", "4097"),
    "--sauvola-k": ("B02", "-0.1", "1.1"),
    "--sauvola-r": ("B02", "0.001", "1.1"),
    "--background-strength": ("I01", "-0.1", "1.1"),
    "--background-max-gain": ("I01", "0.9", "4.1"),
    "--background-target": ("I01", "0.09", "1.1"),
    "--background-cell": ("I01", "7", "513"),
    "--background-quantile": ("I01", "0.74", "1"),
    "--background-smooth": ("I01", "0.09", "21"),
    "--background-radius": ("I02", "0", "257"),
    "--denoise-blend": ("D01", "-0.1", "1.1"),
    "--nlm-h": ("D01", "0.09", "26"),
    "--nlm-patch": ("D01", "2", "16"),
    "--nlm-search": ("D01", "6", "42"),
    "--tv-lambda": ("D02", "0.04", "21"),
    "--tv-iterations": ("D02", "9", "1001"),
    "--tv-tolerance": ("D02", "1e-9", "0.01"),
    "--contrast-blend": ("C01", "-0.1", "1.1"),
    "--levels-low": ("C01", "-1", "11"),
    "--levels-high": ("C01", "89", "101"),
    "--gamma": ("C02", "0.24", "4.01"),
    "--clahe-grid": ("C03", "1x2", "33x2"),
    "--clahe-clip": ("C03", "0.9", "9"),
    "--sharpen-sigma": ("S01", "0.2", "3.1"),
    "--sharpen-amount": ("S01", "-0.1", "2.1"),
    "--sharpen-threshold": ("S01", "-0.1", "21"),
    "--psf-sigma": ("R01", "0.2", "5.1"),
    "--psf-length": ("R01", "0.9", "31.1"),
    "--psf-angle": ("R01", "-181", "181"),
    "--wiener-k": ("R01", "1e-6", "1.1"),
    "--deblur-blend": ("R01", "-0.1", "1.1"),
}


def scalar_rejections() -> list[Case]:
    """Select the owning method so refusal proves a scalar bound rather than wrong presence."""
    cases: list[Case] = []
    for option, (method, *invalid) in INVALID.items():
        base: tuple[str, ...]
        if method in ("B02", "B03"):
            selector = "sauvola" if method == "B02" else "fixed"
            base = ("--output-mode", "bw", "--binarize", selector)
        elif option in ("--psf-length", "--psf-angle"):
            base = ("--deblur", "wiener", "--psf", "motion")
        else:
            base = stage_options(method)
        base = tuple(
            value
            for pair in zip(base[::2], base[1::2], strict=True)
            if pair[0] != option
            for value in pair
        )
        cases.extend(
            Case(
                f"range-{option[2:]}-{index}",
                "rejection",
                "shaded",
                (*base, option, value),
                exit_code=2,
                error="E_ARGUMENT",
            )
            for index, value in enumerate(invalid)
        )
    for index, bad in enumerate(("nan", "+1", " 1", "0x1", "1e999", "1e-999", "1.0junk")):
        cases.append(
            Case(
                f"decimal-grammar-{index}",
                "rejection",
                "shaded",
                ("--contrast", "gamma", "--gamma", bad),
                exit_code=2,
                error="E_ARGUMENT",
            )
        )
    return cases


def rejection_cases() -> list[Case]:
    """Every public valued option must reject explicit emptiness before any input I/O."""
    contract = json.loads((ROOT / "spec/cli-contract.json").read_text())
    cases = []
    for option in contract["options"]:
        if option["scope"] == "P" and option["metavar"] and option["name"] != "--out-dir":
            name = option["name"].removeprefix("--")
            cases.append(
                Case(
                    "empty-" + name,
                    "rejection",
                    "shaded",
                    (option["name"], ""),
                    exit_code=2,
                    error="E_ARGUMENT",
                )
            )
    for method, (option, _selector, private) in STAGES.items():
        cases.append(
            Case(
                "selector-" + method,
                "rejection",
                "shaded",
                ("--" + option, "unknown"),
                exit_code=2,
                error="E_ARGUMENT",
            )
        )
        if private:
            cases.append(
                Case(
                    "unselected-" + method,
                    "rejection",
                    "shaded",
                    private[:2],
                    exit_code=2,
                    error="E_ARGUMENT",
                )
            )
        cases.append(
            Case(
                "binary-reject-" + method,
                "rejection",
                "gray8",
                ("--output-mode", "bw", *stage_options(method)),
                exit_code=2,
                error="E_ARGUMENT",
            )
        )
    for name, options in (
        ("rotate-45", ("--rotate", "45")),
        ("rotate-leading-zero", ("--rotate", "090")),
        ("sauvola-even-window", ("--output-mode", "bw", "--sauvola-window", "4")),
        ("nlm-even-patch", ("--denoise", "nlm", "--nlm-patch", "4")),
        ("nlm-even-search", ("--denoise", "nlm", "--nlm-search", "8")),
        (
            "nlm-patch-greater-search",
            ("--denoise", "nlm", "--nlm-patch", "15", "--nlm-search", "7"),
        ),
        ("morph-surface-option", ("--illumination", "morph", "--background-cell", "16")),
        ("surface-morph-option", ("--illumination", "surface", "--background-radius", "8")),
        ("nlm-tv-option", ("--denoise", "nlm", "--tv-lambda", "1.5")),
        ("tv-nlm-option", ("--denoise", "tvl1", "--nlm-h", "3")),
        ("levels-gamma-option", ("--contrast", "levels", "--gamma", "1")),
        ("gamma-levels-option", ("--contrast", "gamma", "--levels-low", "0.5")),
        ("gaussian-motion-option", ("--deblur", "wiener", "--psf", "gaussian", "--psf-angle", "0")),
        ("motion-gaussian-option", ("--deblur", "wiener", "--psf", "motion", "--psf-sigma", "1")),
    ):
        cases.append(
            Case(
                "cross-" + name,
                "rejection",
                "shaded",
                options,
                exit_code=2,
                error="E_ARGUMENT",
            )
        )
    cases.append(
        Case(
            "clahe-too-small",
            "applicability",
            "gray8",
            ("--contrast", "clahe"),
            exit_code=2,
            error="E_ARGUMENT",
        )
    )
    cases.append(
        Case(
            "nlm-too-small",
            "applicability",
            "black",
            ("--denoise", "nlm"),
            exit_code=4,
            error="E_METHOD_INAPPLICABLE",
        )
    )
    return [*cases, *scalar_rejections()]
