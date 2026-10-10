# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Authoritative retained product scenarios, separate from complete native conformance suites."""

from __future__ import annotations

import json
from dataclasses import asdict
from typing import Any

from audit_paths import ROOT
from matrix_fixtures import tiff_sources
from matrix_rejections import rejection_cases
from matrix_types import IDENTITIES, STAGES, Case, stage_options


def discovery_cases() -> list[Case]:
    """Exercise every supported command and implemented/planned method distinction."""
    cases = [
        Case("version", "discovery", "", command="version"),
        Case("methods", "discovery", "", command="methods"),
    ]
    cases.extend(
        Case(f"help-{command}", "discovery", "", ("--help",), command=command)
        for command in ("root", "process", "verify", "methods", "version")
    )
    contract = json.loads((ROOT / "spec/method-contract.json").read_text())
    cases.extend(
        Case(
            f"method-{method['id']}",
            "discovery",
            "",
            (method["id"],),
            exit_code=0 if method["status"] == "implemented" else 4,
            error="" if method["status"] == "implemented" else "E_NOT_IMPLEMENTED",
            command="methods",
        )
        for method in contract["methods"]
    )
    return cases


def representation_cases() -> list[Case]:
    """Cover admitted containers, precision, output categories and alpha choices."""
    exact = ("gray1", "gray2", "gray4", "gray8", "gray16", "rgb8", "rgb16", "adam7")
    cases = [
        Case(f"representation-{name}", "representation", name, check="samples") for name in exact
    ]
    cases += [Case("palette", "representation", "palette", check="palette")]
    for source in ("rgb8", "rgb16", "rgba8", "rgba16", "gray-alpha8", "gray-alpha16"):
        for mode in ("preserve", "gray"):
            cases.append(
                Case(
                    f"{source}-{mode}",
                    "representation",
                    source,
                    ("--output-mode", mode),
                    check="alpha-or-gray",
                )
            )
    for source in ("rgba8", "rgba16"):
        cases.append(
            Case(
                f"{source}-black",
                "representation",
                source,
                ("--alpha", "black"),
                check="alpha-or-gray",
            )
        )
        cases.append(
            Case(
                f"{source}-reject",
                "representation",
                source,
                ("--alpha", "reject"),
                exit_code=3,
                error="E_INPUT",
                publication="not_published",
            )
        )
    for depth in ("8", "16"):
        cases.append(
            Case(
                f"precision-to-{depth}",
                "representation",
                "gray16",
                ("--bit-depth", depth),
                check="depth",
            )
        )
    for name in tiff_sources():
        cases.append(Case(name, "tiff", name, check="tiff"))
    for name in (
        "gray-document-baseline",
        "gray-document-progressive",
        "chroma-patch-baseline",
        "chroma-patch-progressive",
    ):
        for mode in ("preserve", "gray"):
            cases.append(
                Case(f"jpeg-{name}-{mode}", "jpeg", f"jpeg-{name}", ("--output-mode", mode))
            )
    for name in ("jpeg-1.tif", "jpeg-1-progressive.tif"):
        cases.append(Case("tiff-" + name, "tiff", name))
    return cases


def photometric_cases() -> list[Case]:
    """Separate active synthetic gains, exact identities and output protection."""
    cases = [
        Case("baseline-shaded", "baseline", "shaded", check="samples"),
        Case("baseline-noisy", "baseline", "noisy", check="samples"),
    ]
    sources = {
        "I01": "shaded",
        "I02": "shaded",
        "D01": "noisy",
        "D02": "noisy",
        "C01": "low-contrast",
        "C02": "gray8",
        "C03": "low-contrast",
        "S01": "soft-edge",
        "R01": "soft-edge",
    }
    checks = {
        "I01": "paper",
        "I02": "paper",
        "D01": "noise",
        "D02": "noise",
        "C01": "levels",
        "C02": "gamma",
        "C03": "local-contrast",
        "S01": "edge",
        "R01": "edge",
    }
    for method in STAGES:
        options = stage_options(method)
        cases.append(
            Case(
                "active-" + method,
                "benefit",
                sources[method],
                options,
                check=checks[method],
                methods=(method,),
            )
        )
        identity = IDENTITIES[method]
        identity_options = tuple(
            value
            for pair in zip(options[::2], options[1::2], strict=True)
            if pair[0] != identity[0]
            for value in pair
        )
        cases.append(
            Case(
                "identity-" + method,
                "identity",
                "shaded",
                (*identity_options, *identity),
                check="same",
                reference="baseline-shaded",
                methods=(method,),
            )
        )
        cases.append(
            Case(
                "protect-" + method,
                "protection",
                "shaded",
                (*options, "--protect-mask", "{mask}"),
                check="protected",
                reference="baseline-shaded",
                methods=(method,),
            )
        )
        cases.append(
            Case(
                "all-protected-" + method,
                "protection",
                "shaded",
                (*options, "--protect-mask", "{mask-all}"),
                check="same",
                reference="baseline-shaded",
                methods=(method,),
            )
        )
    cases.extend(
        (
            Case(
                "active-D02-default",
                "benefit",
                "noisy",
                ("--denoise", "tvl1"),
                check="noise",
                methods=("D02",),
            ),
            Case(
                "surface-auto-flat",
                "applicability",
                "flat",
                ("--illumination", "auto"),
                check="auto-skip",
            ),
            Case(
                "surface-black",
                "applicability",
                "black",
                ("--illumination", "surface"),
                exit_code=4,
                error="E_METHOD_INAPPLICABLE",
            ),
            Case(
                "morph-black",
                "identity",
                "black",
                ("--illumination", "morph"),
                check="samples",
                methods=("I02",),
            ),
        )
    )
    for kind, private in (("motion", ("--psf-angle", "35")), ("kernel", ("--psf-file", "{psf}"))):
        cases.append(
            Case(
                "restoration-" + kind,
                "restoration",
                "soft-edge",
                ("--deblur", "wiener", "--psf", kind, *private),
                methods=("R01",),
            )
        )
    return cases


def geometry_cases() -> list[Case]:
    """Asymmetry detects every mirror/turn pair at each continuous precision."""
    cases: list[Case] = []
    for depth in (8, 16):
        for orientation in range(1, 9):
            cases.extend(
                Case(
                    f"geometry-{depth}-o{orientation}-r{turn}",
                    "geometry",
                    f"geometry-{depth}-{orientation}",
                    ("--rotate", str(turn)),
                    check="geometry",
                    methods=("G02",),
                )
                for turn in (0, 90, 180, 270)
            )
    for method, selector in (("B01", "otsu"), ("B02", "sauvola"), ("B03", "fixed")):
        cases.extend(
            Case(
                f"binary-{method}-r{turn}",
                "binarization",
                "gray8",
                ("--output-mode", "bw", "--binarize", selector, "--rotate", str(turn)),
                check="binary",
                methods=(method, "G02"),
            )
            for turn in (0, 90, 180, 270)
        )
    return cases


def composition_cases() -> list[Case]:
    """Exercise fixed pipeline alternatives together and a representative page-sized workload."""
    cases: list[Case] = []
    for illumination in ("I01", "I02"):
        for denoise in ("D01", "D02"):
            for contrast in ("C01", "C02", "C03"):
                methods = (illumination, denoise, "R01", contrast, "S01", "G02")
                options = tuple(value for method in methods[:-1] for value in stage_options(method))
                options += ("--rotate", "90", "--protect-mask", "{mask}")
                cases.append(
                    Case(
                        "compose-" + "-".join((*methods[:2], contrast)),
                        "composition",
                        "shaded",
                        options,
                        check="composition",
                        reference="rotated-baseline",
                        methods=methods,
                    )
                )
    cases.insert(0, Case("rotated-baseline", "baseline", "shaded", ("--rotate", "90")))
    options = (
        *stage_options("I01"),
        *stage_options("D01"),
        *stage_options("C01"),
        *stage_options("S01"),
    )
    cases.append(
        Case(
            "stress-1536x1024",
            "stress",
            "stress",
            options,
            check="composition",
            methods=("I01", "D01", "C01", "S01"),
        )
    )
    return cases


def matrix() -> list[Case]:
    """Keep one ordered scenario source; the checked-in JSON is its reviewable projection."""
    return [
        *discovery_cases(),
        *representation_cases(),
        *photometric_cases(),
        *geometry_cases(),
        *composition_cases(),
        *rejection_cases(),
    ]


def catalog() -> list[dict[str, Any]]:
    """Serialize portable case expectations without absolute execution paths."""
    return [asdict(case) for case in matrix()]


def active_coverage(cases: list[Case]) -> dict[str, list[str]]:
    """Refuse a matrix that discovers a supported method but never exercises its active path."""
    reviewed = json.loads((ROOT / "spec/method-contract.json").read_text())
    coverage: dict[str, list[str]] = {
        method["id"]: [] for method in reviewed["methods"] if method["status"] == "implemented"
    }
    for case in cases:
        if case.group in ("benefit", "binarization", "geometry") and not case.exit_code:
            for method in case.methods:
                if method != "G02" or case.options[case.options.index("--rotate") + 1] != "0":
                    coverage[method].append(case.id)
    missing = [method for method, identifiers in coverage.items() if not identifiers]
    if missing:
        message = f"Implemented methods lack active scenarios: {', '.join(missing)}"
        raise ValueError(message)
    return coverage
