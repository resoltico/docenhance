# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Reusable human/JSON discovery, recovery and warning scenarios for the real CLI."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Probe:
    """One public command with portable path placeholders and independent outcome facts."""

    id: str
    arguments: tuple[str, ...]
    exit_code: int = 0
    output: str = ""
    error: str = ""
    publication: str = ""
    working_directory: str = ""
    json_response: bool | None = None


INVALID_VALUES = (
    ("gamma-high", ("--contrast", "gamma", "--gamma", "5")),
    ("gamma-nan", ("--contrast", "gamma", "--gamma", "NaN")),
    ("gamma-leading-plus", ("--contrast", "gamma", "--gamma", "+1.2")),
    ("gamma-locale", ("--contrast", "gamma", "--gamma", "1,2")),
    ("gamma-overflow", ("--contrast", "gamma", "--gamma", "1e999")),
    ("gamma-underflow", ("--contrast", "gamma", "--gamma", "1e-999")),
    ("gamma-whitespace", ("--contrast", "gamma", "--gamma", " 1.2")),
    ("nlm-nan", ("--denoise", "nlm", "--nlm-h", "NaN")),
    ("nlm-integer", ("--denoise", "nlm", "--nlm-patch", "abc")),
    ("nlm-even", ("--denoise", "nlm", "--nlm-patch", "4")),
    ("tv-lambda-low", ("--denoise", "tvl1", "--tv-lambda", "0")),
    ("tv-iterations-low", ("--denoise", "tvl1", "--tv-iterations", "9")),
    ("tv-tolerance-high", ("--denoise", "tvl1", "--tv-tolerance", "1")),
    (
        "fixed-byte-units",
        ("--output-mode", "bw", "--binarize", "fixed", "--fixed-threshold", "128"),
    ),
    ("sauvola-even", ("--output-mode", "bw", "--sauvola-window", "4")),
    ("sauvola-r-zero", ("--output-mode", "bw", "--sauvola-r", "0")),
    ("depth-policy", ("--bit-depth", "12")),
    ("alpha-policy", ("--alpha", "noop")),
    ("profile-policy", ("--profile-policy", "assume")),
    ("gamma-empty", ("--contrast", "gamma", "--gamma", "")),
    ("output-mode-empty", ("--output-mode", "")),
    ("unknown-contrast", ("--contrast", "magic")),
    ("unknown-illumination", ("--illumination", "magic")),
    ("unknown-denoise", ("--denoise", "NLM")),
    ("wrong-contrast-options", ("--contrast", "gamma", "--levels-low", "0.5")),
    ("wrong-illumination-options", ("--illumination", "morph", "--background-cell", "16")),
    ("binary-explicit-off", ("--output-mode", "bw", "--denoise", "off")),
    ("binary-selector-needs-mode", ("--binarize", "fixed")),
    ("missing-psf", ("--deblur", "wiener")),
    ("rotate-invalid", ("--rotate", "45")),
    ("clahe-grid-malformed", ("--contrast", "clahe", "--clahe-grid", "8X8")),
    ("sharpen-amount-high", ("--sharpen", "unsharp", "--sharpen-amount", "3")),
)
SYNTAX = (
    ("unknown-command", ("inspect",)),
    ("unknown-option", ("version", "--wat")),
    ("repeated-json", ("version", "--json", "--json")),
    ("misplaced-json", ("--json", "version")),
    ("missing-input", ("process",)),
    ("missing-output", ("process", "{gray}")),
)
SOURCE_REFUSALS = (
    ("corrupt-input", "{corrupt}", (), 3, "E_INPUT", "{output}"),
    ("missing-source", "{missing}", (), 3, "E_INPUT", "{output}"),
    ("alpha-reject", "{rgba}", ("--alpha", "reject"), 3, "E_INPUT", "{output}"),
    ("binary-color-refusal", "{rgba}", ("--output-mode", "bw"), 3, "E_INPUT", "{output}"),
    ("missing-parent", "{gray}", (), 5, "E_OUTPUT", "{absent-parent}"),
    ("occupied-output", "{gray}", (), 5, "E_OUTPUT", "{occupied}"),
)
SUCCESS = (
    ("conversion-warnings", "{rgba}", ("--bit-depth", "8")),
    ("sharpen-warning", "{gray}", ("--sharpen", "unsharp")),
    ("tv-warning", "{gray}", ("--denoise", "tvl1", "--tv-iterations", "10")),
    ("binary-success", "{gray}", ("--output-mode", "bw", "--binarize", "otsu")),
    ("surface-solver", "{gray}", ("--illumination", "surface")),
)


def paired(name: str, arguments: tuple[str, ...], exit_code: int = 0) -> tuple[Probe, ...]:
    """Keep text/JSON requests adjacent, without adding a second --json to syntax probes."""
    if "--json" in arguments:
        return (Probe(name + "-json", arguments, exit_code),)
    return (
        Probe(name + "-text", arguments, exit_code),
        Probe(name + "-json", (*arguments, "--json"), exit_code),
    )


def discovery_probes() -> list[Probe]:
    """Exercise discovery and deliberately malformed command syntax on both transports."""
    cases: list[Probe] = []
    for command in ((), ("process",), ("verify",), ("methods",), ("version",)):
        cases.extend(paired("help-" + (command[0] if command else "root"), (*command, "--help")))
    cases.extend((Probe("bare-root", ()), Probe("root-json", ("--json",))))
    cases.append(Probe("version-alias", ("--version",)))
    for name in ("version", "methods"):
        cases.extend(paired(name, (name,)))
    for method in ("B01", "D02", "G02"):
        cases.extend(paired("method-" + method, ("methods", method)))
    for name, arguments in SYNTAX:
        cases.extend(paired(name, arguments, 2))
    cases.extend(
        (
            Probe("missing-option-value-text", ("process", "{gray}", "--out-dir"), 2),
            Probe("missing-option-value-json", ("process", "{gray}", "--json", "--out-dir"), 2),
        )
    )
    for method in ("G01", "B99", "b01"):
        cases.extend(paired("unavailable-" + method, ("methods", method), 4))
    return cases


def probes() -> tuple[Probe, ...]:
    """Enumerate retained scenarios, without implying human UX acceptance is satisfied."""
    cases = discovery_probes()
    for name, options in INVALID_VALUES:
        arguments = ("process", "{gray}", "--out-dir", "{output}", *options)
        cases.extend(
            Probe(case.id, case.arguments, 2, "{output}", "E_ARGUMENT", "not_started")
            for case in paired(name, arguments, 2)
        )
    for name, source, source_options, code, error, output in SOURCE_REFUSALS:
        arguments = ("process", source, "--out-dir", output, *source_options)
        publication = "not_published" if name == "alpha-reject" else "not_started"
        cases.extend(
            Probe(case.id, case.arguments, code, output, error, publication)
            for case in paired(name, arguments, code)
        )
    for name, source, success_options in SUCCESS:
        arguments = ("process", source, "--out-dir", "{output}", *success_options)
        for case in paired(name, arguments):
            cases.append(Probe(case.id, case.arguments, output="{output}", publication="completed"))
            cases.extend(paired("verify-" + case.id, ("verify", "{bundle:" + case.id + "}")))
    for name, directory in (("verify-missing", "{missing}"), ("verify-empty", "{empty}")):
        cases.extend(
            Probe(case.id, case.arguments, 3, error="E_INPUT", publication="not_started")
            for case in paired(name, ("verify", directory), 3)
        )
    cases.append(
        Probe(
            "option-looking-output-text",
            ("process", "{gray}", "--out-dir", "--json"),
            output="{option-looking-output}",
            publication="completed",
            json_response=False,
        )
    )
    cases.extend(paired("verify-option-looking-output", ("verify", "{option-looking-output}")))
    cases.append(
        Probe(
            "explicit-option-looking-output-text",
            ("process", "{gray}", "--out-dir=--json"),
            output="{explicit-option-looking-output}",
            publication="completed",
            working_directory="explicit-value",
        )
    )
    cases.extend(
        paired(
            "verify-explicit-option-looking-output", ("verify", "{explicit-option-looking-output}")
        )
    )
    return tuple(cases)
