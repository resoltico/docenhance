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


FULL_QUALITY_CONDITION = "github.event_name != 'workflow_dispatch' || !inputs.tooling_diagnostic"
TOOLING_DIAGNOSTIC_CONDITION = (
    "github.event_name == 'workflow_dispatch' && inputs.tooling_diagnostic"
)


NATIVE_DIAGNOSTIC_FUZZ_CONDITION = (
    "(github.event_name != 'workflow_dispatch' || !inputs.native_diagnostic) && "
    "(github.event_name != 'workflow_dispatch' || !inputs.tooling_diagnostic)"
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
    expected["tooling_diagnostic"] = {
        "description": (
            "Run complete tooling suites on three OSes without product builds (not qualification)"
        ),
        "type": "boolean",
        "required": "false",
        "default": "false",
    }
    require_tooling_diagnostic(document)
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
    "if": "github.event_name == 'workflow_dispatch' && inputs.tooling_diagnostic",
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
