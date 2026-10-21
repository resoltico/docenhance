# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Reviewed CI execution coverage shared by workflow and release admission."""

from __future__ import annotations

from typing import Any

WORKFLOW_PATH = ".github/workflows/ci.yml"
NATIVE_RUNNERS = {
    "linux-x86_64": "ubuntu-24.04",
    "linux-arm64": "ubuntu-24.04-arm",
    "macos-arm64": "macos-26",
    "macos-x86_64": "macos-26-intel",
    "macos-15-arm64": "macos-15",
    "macos-15-x86_64": "macos-15-intel",
    "windows-x86_64": "windows-2025-vs2026",
}
JOB_TITLES = {
    "structural": "Structural and reference gates",
    "native": "Native / ${{ matrix.platform }}",
    "fuzz": "Strict fuzzing / ${{ matrix.engine }} (ASan + UBSan)",
    "sanitize": "Sanitized / ${{ matrix.preset }}",
    "gate": "Required quality gate",
}
SANITIZERS = ("sanitize", "tsan")
FUZZ_ENGINES = ("libfuzzer", "afl")


def job_names() -> set[str]:
    """Expand only the reviewed required execution matrices."""
    return {
        JOB_TITLES["structural"],
        JOB_TITLES["gate"],
        *(
            JOB_TITLES["native"].replace("${{ matrix.platform }}", platform)
            for platform in NATIVE_RUNNERS
        ),
        *(JOB_TITLES["sanitize"].replace("${{ matrix.preset }}", preset) for preset in SANITIZERS),
        *(JOB_TITLES["fuzz"].replace("${{ matrix.engine }}", engine) for engine in FUZZ_ENGINES),
    }


def require_workflow_coverage(document: dict[str, Any]) -> None:
    """The reviewed roles name the same execution and select immutable event checkouts."""
    for name, title in JOB_TITLES.items():
        job = document["jobs"][name]
        if job.get("name") != title:
            message = "Required CI job title differs from reviewed coverage"
            raise ValueError(message)
        if name != "gate":
            checkouts = [
                step
                for step in job["steps"]
                if step.get("uses", "").startswith("actions/checkout@")
            ]
            if (
                len(checkouts) != 1
                or checkouts[0].get("with", {}).get("ref") != "${{ github.sha }}"
            ):
                message = "Required CI checkout must select the immutable event SHA"
                raise ValueError(message)


NATIVE_DIAGNOSTIC_FUZZ_CONDITION = (
    "github.event_name != 'workflow_dispatch' || !inputs.native_diagnostic"
)


def require_native_diagnostic_input(document: dict[str, Any]) -> None:
    """Only an explicit manual boolean opt-in may omit fuzz campaign execution."""
    expected = {
        "native_diagnostic": {
            "description": "Run native diagnostics without fuzz campaigns (not qualification)",
            "type": "boolean",
            "required": "false",
            "default": "false",
        }
    }
    if document.get("on", {}).get("workflow_dispatch") != {"inputs": expected}:
        message = "Manual native diagnostic input must be an explicit default-false boolean"
        raise ValueError(message)


def fuzz_matrix(major: str) -> dict[str, Any]:
    """The two required engines and their actual compiler selections."""
    return {
        "include": [
            {
                "engine": "libfuzzer",
                "preset": "fuzz",
                "cc": f"clang-{major}",
                "cxx": f"clang++-{major}",
            },
            {
                "engine": "afl",
                "preset": "fuzz-afl",
                "cc": "afl-clang-fast",
                "cxx": "afl-clang-fast++",
            },
        ]
    }
