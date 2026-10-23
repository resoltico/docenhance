# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Admit actual FFT/restoration CTest producer commands and contiguous shard roles."""

from __future__ import annotations

from pathlib import Path
from typing import Any

PREFIX = "restoration-native-resources-"
CONTROLS = "restoration-native-controls"
# Select the complete families so malformed roles cannot disappear before admission.
RESOURCE_PATTERN = r"^(fft-native|restoration-native-)"


def resource_cases(tests: list[dict[str, Any]], executables: dict[str, str]) -> set[str]:
    """Bind contiguous actual shard discovery to one EXE and exact role arguments."""
    selected = [t for t in tests if t["name"].startswith("restoration-native-")]
    names = [t["name"] for t in selected]
    count = len(selected) - 1
    expected = {CONTROLS, *(PREFIX + str(i) for i in range(count))}
    if not (count > 1 and len(names) == len(set(names)) and (set(names) == expected)):
        message = "Restoration discovery is missing, duplicate or noncontiguous"
        raise ValueError(message)
    if set(executables) != {"de_fft_resources", "de_restoration_resources"}:
        message = "Both selected resource producer paths are required"
        raise ValueError(message)
    executable = Path(executables["de_restoration_resources"]).resolve()
    for test in selected:
        name = test["name"]
        suffix = ["--controls"] if name == CONTROLS else ["--shard", name.removeprefix(PREFIX)]
        if not (Path(test["command"][0]).resolve() == executable and test["command"][1:] == suffix):
            message = "Resource producer or role command differs"
            raise ValueError(message)
    fft = [t for t in tests if t["name"].startswith("fft-native")]
    if not (
        len(fft) == 1
        and fft[0]["name"] == "fft-native-resources"
        and (len(fft[0]["command"]) == 1)
        and (
            Path(fft[0]["command"][0]).resolve() == Path(executables["de_fft_resources"]).resolve()
        )
    ):
        message = "Missing actual FFT resource case"
        raise ValueError(message)
    return expected | {"fft-native-resources"}
