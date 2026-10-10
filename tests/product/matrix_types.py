# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Portable product scenario values and modest explicit processing selections."""

from __future__ import annotations

from dataclasses import dataclass

STAGES = {
    "I01": ("illumination", "surface", ("--background-cell", "16")),
    "I02": ("illumination", "morph", ("--background-radius", "8")),
    "D01": ("denoise", "nlm", ("--nlm-patch", "3", "--nlm-search", "7")),
    "D02": ("denoise", "tvl1", ("--tv-lambda", "0.5", "--tv-iterations", "100")),
    "C01": ("contrast", "levels", ()),
    "C02": ("contrast", "gamma", ("--gamma", "2")),
    "C03": ("contrast", "clahe", ("--clahe-grid", "2x2")),
    "S01": ("sharpen", "unsharp", ("--sharpen-amount", "1", "--sharpen-threshold", "0")),
    "R01": ("deblur", "wiener", ("--psf", "gaussian", "--psf-sigma", "1.5")),
}
IDENTITIES = {
    "I01": ("--background-strength", "0"),
    "I02": ("--background-max-gain", "1"),
    "D01": ("--denoise-blend", "0"),
    "D02": ("--denoise-blend", "0"),
    "C01": ("--contrast-blend", "0"),
    "C02": ("--gamma", "1"),
    "C03": ("--contrast-blend", "0"),
    "S01": ("--sharpen-amount", "0"),
    "R01": ("--deblur-blend", "0"),
}


@dataclass(frozen=True)
class Case:
    """One replayable command, its independent acceptance rule and honest quality scope."""

    id: str
    group: str
    source: str
    options: tuple[str, ...] = ()
    check: str = "structure"
    reference: str = ""
    methods: tuple[str, ...] = ()
    exit_code: int = 0
    error: str = ""
    command: str = "process"
    publication: str = "not_started"


def stage_options(method: str) -> tuple[str, ...]:
    """Use intentionally modest active settings, preserving each method's exact selector."""
    option, selector, private = STAGES[method]
    return ("--" + option, selector, *private)
