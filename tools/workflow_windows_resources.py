# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Keep Windows CRT proof mandatory without altering native diagnostic archive policy."""

from __future__ import annotations

from typing import Any

WINDOWS_PATHS = (
    "snapshot.json",
    "proof.json",
    "release/*.map.gz*",
    "debug/*.map.gz*",
    "debug/ctest-output*.gz*",
    "debug/ctest.xml.gz*",
    "debug/native-*.json",
    "debug-configure.log",
    "debug-dependencies.log",
    "debug-targets.log",
    "debug-tests.log",
    "release-de_fft_resources-directives.log",
    "release-de_restoration_resources-directives.log",
    "debug-de_fft_resources-directives.log",
    "debug-de_restoration_resources-directives.log",
)


def require_windows_resource_proof(job: dict[str, Any], owner: dict[str, Any]) -> None:
    """Require the exact bounded evidence upload after the same-shell Windows owner."""
    matches = [
        step for step in job["steps"] if step.get("name") == "Retain Windows CRT resource proof"
    ]
    if len(matches) != 1:
        message = "Required Windows resource proof upload is missing or duplicated"
        raise ValueError(message)
    upload = matches[0]
    expected = {
        "name": "windows-resource-proof-${{ github.run_id }}-${{ github.run_attempt }}",
        "path": "\n".join("out/windows-resource-proof/" + name for name in WINDOWS_PATHS) + "\n",
        "if-no-files-found": "error",
        "compression-level": "0",
        "retention-days": "7",
    }
    if (
        upload.get("if") != "always() && runner.os == 'Windows'"
        or upload.get("uses") != "actions/upload-artifact@cf430e030ddbb5b0abf93d22962f4752f3646cd9"
        or upload.get("with") != expected
        or upload.get("continue-on-error", "false") != "false"
        or job["steps"].index(upload) <= job["steps"].index(owner)
    ):
        message = "Required Windows resource proof upload has unsafe settings or ordering"
        raise ValueError(message)
