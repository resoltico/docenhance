# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Reviewed CI execution coverage shared by workflow and release admission."""

from __future__ import annotations

import json
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


FULL_QUALITY_CONDITION = (
    "github.event_name != 'workflow_dispatch' || inputs.diagnostic == 'none' || "
    "inputs.diagnostic == 'native'"
)
NATIVE_QUALITY_CONDITION = FULL_QUALITY_CONDITION + " || inputs.diagnostic == 'intel'"
FUZZ_QUALITY_CONDITION = "github.event_name != 'workflow_dispatch' || inputs.diagnostic == 'none'"
TOOLING_DIAGNOSTIC_CONDITION = (
    "github.event_name == 'workflow_dispatch' && inputs.diagnostic == 'tooling'"
)
PAIRED_CAPABILITY_CONDITION = (
    "github.event_name == 'workflow_dispatch' && inputs.diagnostic == 'paired_capability'"
)
NATIVE_DIAGNOSTIC_ENV = (
    "${{ github.event_name == 'workflow_dispatch' && "
    "(inputs.diagnostic == 'native' || inputs.diagnostic == 'intel') }}"
)
NATIVE_MATRIX_INCLUDE = (
    "${{ fromJSON(github.event_name == 'workflow_dispatch' && inputs.diagnostic == 'intel' && '"
    + json.dumps(
        [{"platform": "macos-x86_64", "os": NATIVE_RUNNERS["macos-x86_64"]}], separators=(",", ":")
    )
    + "' || '"
    + json.dumps(
        [{"platform": name, "os": runner} for name, runner in NATIVE_RUNNERS.items()],
        separators=(",", ":"),
    )
    + "') }}"
)


def require_diagnostic_input(document: dict[str, Any]) -> None:
    """A closed manual choice admits one concrete scope; unknown/obsolete inputs are refused."""
    expected = {
        "diagnostic": {
            "description": "Select one diagnostic scope; only none runs all qualification gates",
            "type": "choice",
            "required": "false",
            "default": "none",
            "options": ["none", "native", "tooling", "intel", "paired_capability"],
        }
    }
    if document.get("on", {}).get("workflow_dispatch") != {"inputs": expected}:
        message = "Manual diagnostic must be the closed default-none choice"
        raise ValueError(message)
    require_tooling_diagnostic(document)
    if document["jobs"].get("paired_capability") != PAIRED_CAPABILITY_JOB:
        message = (
            "Paired capability must retain its exact manual read-only route and fatal evidence"
        )
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


WINDOWS_TOOLING_COMMAND = r"""$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $installation) { throw 'A current Visual Studio C++ toolchain is required' }
$devshell = Join-Path $installation 'Common7\Tools\Launch-VsDevShell.ps1'
& $devshell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'MSVC developer environment was not activated'
}
python tools/run_tooling_tests.py --jobs 2
exit $LASTEXITCODE
"""


TOOLING_DIAGNOSTIC_JOB: dict[str, Any] = {
    "name": "Tooling diagnostics / ${{ matrix.os }} (not qualification)",
    "if": TOOLING_DIAGNOSTIC_CONDITION,
    "strategy": {
        "fail-fast": "false",
        "matrix": {"os": ["ubuntu-24.04", "macos-26", "windows-2025-vs2026"]},
    },
    "runs-on": "${{ matrix.os }}",
    "timeout-minutes": "30",
    "steps": [
        {
            "uses": "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1",
            "with": {"persist-credentials": "false", "ref": "${{ github.sha }}"},
        },
        {
            "name": "Normalize Windows checkout",
            "if": "runner.os == 'Windows'",
            "shell": "pwsh",
            "run": "git config --local core.autocrlf false\ngit reset --hard",
        },
        {
            "uses": "actions/setup-python@5fda3b95a4ea91299a34e894583c3862153e4b97",
            "with": {"python-version": "3.12"},
        },
        {"run": "python tools/install_build_tools.py"},
        {"run": "python tools/install_build_tools.py --lint"},
        {
            "name": "Install pinned Linux compiler and analysis tools",
            "if": "runner.os == 'Linux'",
            "run": "python tools/install_llvm.py --compiler",
        },
        {
            "name": "Install pinned native analysis tools",
            "if": "runner.os != 'Linux'",
            "run": "python tools/install_llvm.py",
        },
        {
            "name": "Execute complete tooling suites",
            "if": "runner.os != 'Windows'",
            "run": "python tools/run_tooling_tests.py --jobs 2",
        },
        {
            "name": "Execute complete tooling suites in the selected MSVC environment",
            "if": "runner.os == 'Windows'",
            "shell": "pwsh",
            "run": WINDOWS_TOOLING_COMMAND,
        },
    ],
}


def require_tooling_diagnostic(document: dict[str, Any]) -> None:
    """The manual-only diagnostic runs real complete suites without earning required coverage."""
    if document["jobs"].get("tooling") != TOOLING_DIAGNOSTIC_JOB:
        message = (
            "Tooling diagnostic must retain exact manual route, "
            "native environments and complete suites"
        )
        raise ValueError(message)


PAIRED_CAPABILITY_JOB: dict[str, Any] = {
    "name": "Paired performance capability / macos-15-intel (not qualification)",
    "if": PAIRED_CAPABILITY_CONDITION,
    "runs-on": "macos-15-intel",
    "timeout-minutes": "5",
    "steps": [
        {
            "uses": "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1",
            "with": {"persist-credentials": "false", "ref": "${{ github.sha }}"},
        },
        {
            "uses": "actions/setup-python@5fda3b95a4ea91299a34e894583c3862153e4b97",
            "with": {"python-version": "3.12"},
        },
        {
            "name": "Observe host capability without builds or timing",
            "run": (
                "python tools/check_performance_host.py --output "
                '"${{ runner.temp }}/paired-capability"'
            ),
        },
        {
            "name": "Retain complete capability observations",
            "if": "always()",
            "uses": "actions/upload-artifact@cf430e030ddbb5b0abf93d22962f4752f3646cd9",
            "with": {
                "name": "paired-capability-${{ github.run_id }}-${{ github.run_attempt }}",
                "path": "${{ runner.temp }}/paired-capability",
                "if-no-files-found": "error",
                "compression-level": "6",
                "retention-days": "7",
            },
        },
    ],
}
