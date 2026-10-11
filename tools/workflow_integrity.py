# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Enforce executable source gates, complete CI coverage and truthful aggregate results."""

from __future__ import annotations

import json
from typing import TYPE_CHECKING, Any

import yaml

from ci_contract import NATIVE_RUNNERS, SANITIZERS, require_workflow_coverage
from workflow_assertions import action_errors, aggregate_job, require_evidence_upload
from workflow_cache import source_cache
from workflow_config import python_errors, workflow

if TYPE_CHECKING:
    from pathlib import Path

SOURCE_GATE = "python tools/check_all.py"
MATRIX_WORKFLOW = "cmake --workflow --preset ${{ matrix.preset }}"
NATIVE_SMOKE = "python tools/package_smoke.py --build out/release"


def required_step(
    job: dict[str, Any], command: str, condition: str | None = None
) -> dict[str, Any]:
    """A required command occupies a complete step, with only its reviewed route condition."""
    candidates: list[dict[str, Any]] = [
        step for step in job.get("steps", []) if step.get("run", "").strip() == command
    ]
    if len(candidates) != 1:
        msg = f"Required executable step missing or duplicated: {command}"
        raise ValueError(msg)
    step = candidates[0]
    if step.get("if") != condition or step.get("continue-on-error", "false") != "false":
        msg = f"Required executable step is conditional or optional: {command}"
        raise ValueError(msg)
    if step.get("shell", "bash") not in {"bash", "pwsh"} or "working-directory" in step:
        msg = f"Required executable step changes shell or working directory: {command}"
        raise ValueError(msg)
    return step


def required_job(document: dict[str, Any], name: str) -> dict[str, Any]:
    """Required execution jobs cannot be conditional, optional or remapped by defaults."""
    job: dict[str, Any] = document["jobs"][name]
    if "if" in job or job.get("continue-on-error", "false") != "false" or "defaults" in job:
        msg = f"Required job is conditional, optional or changes execution defaults: {name}"
        raise ValueError(msg)
    if "needs" in job:
        msg = f"Required execution jobs must start independently: {name}"
        raise ValueError(msg)
    return job


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


def require_strategy(job: dict[str, Any], matrix: dict[str, Any]) -> None:
    """A complete reviewed matrix has no exclusion, fail-fast or dynamic substitution."""
    if job.get("strategy") != {"fail-fast": "false", "matrix": matrix}:
        msg = "Required matrix coverage or independent execution differs"
        raise ValueError(msg)


def sanitizer_job(document: dict[str, Any], major: str) -> None:
    """Both whole-suite sanitizer modes use actual pinned build-step environment."""
    job = required_job(document, "sanitize")
    require_strategy(job, {"preset": list(SANITIZERS)})
    required_step(job, "python tools/install_build_tools.py")
    required_step(job, "python tools/install_llvm.py --fuzzing")
    source_cache(job, required_step(job, "cmake -P cmake/AcquireDependencies.cmake"))
    step = required_step(job, MATRIX_WORKFLOW)
    if step.get("env") != {"CC": f"clang-{major}", "CXX": f"clang++-{major}"}:
        msg = "Sanitizer build step does not select the pinned compiler"
        raise ValueError(msg)


def fuzz_job(document: dict[str, Any], major: str, *, nightly: bool) -> None:
    """Both engines execute the complete campaign; comments cannot pin their compiler."""
    job = required_job(document, "campaign" if nightly else "fuzz")
    require_strategy(job, fuzz_matrix(major))
    required_step(job, "python tools/install_build_tools.py")
    seconds = '"$SECONDS_PER_TARGET"' if nightly else "60"
    command = (
        "cmake --preset ${{ matrix.preset }} "
        f"-DDE_FUZZ_SECONDS={seconds} -DDE_FUZZ_JOBS=4\n"
        "cmake --build --preset ${{ matrix.preset }}"
    )
    step = required_step(job, command)
    if step.get("env") != {"CC": "${{ matrix.cc }}", "CXX": "${{ matrix.cxx }}"}:
        msg = "Fuzz build step does not select its reviewed engine compiler"
        raise ValueError(msg)
    source_cache(job, required_step(job, "python tools/deps.py fetch"))
    required_step(job, "python tools/install_llvm.py --fuzzing")
    required_step(
        job,
        "python tools/install_aflplusplus.py\necho core | sudo tee /proc/sys/kernel/core_pattern",
        "matrix.engine == 'afl'",
    )
    budget = "19800" if nightly else "2400"
    required_step(
        job,
        f"python tools/run_fuzz_campaign.py --plan --seconds {seconds} "
        f"--jobs 4 --job-budget {budget}",
    )
    campaign_step = required_step(job, "ctest --preset ${{ matrix.preset }} --output-on-failure")
    triage_step = required_step(
        job,
        "python tools/triage_fuzz_findings.py --build out/${{ matrix.preset }}/app "
        "--engine ${{ matrix.engine }}",
        "always()",
    )
    archive_step = required_step(
        job,
        "python tools/package_fuzz_evidence.py --build out/${{ matrix.preset }}/app "
        "--engine ${{ matrix.engine }} --commit ${{ github.sha }} "
        "--output out/${{ matrix.preset }}/fuzz-evidence.tar.gz",
        "always()",
    )
    upload = require_evidence_upload(
        job,
        name="Upload campaign evidence" if nightly else "Upload fuzz evidence",
        path="out/${{ matrix.preset }}/fuzz-evidence.tar.gz",
        artifact=(
            "campaign-${{ matrix.engine }}-${{ github.run_id }}-${{ github.run_attempt }}"
            if nightly
            else "fuzz-${{ matrix.engine }}-${{ github.run_id }}-${{ github.run_attempt }}"
        ),
        days=30 if nightly else 7,
    )
    steps = job["steps"]
    if not (
        steps.index(campaign_step) < steps.index(triage_step)
        < steps.index(archive_step) < steps.index(upload)
    ):
        raise ValueError("Fuzz evidence steps must follow campaign execution")


def quality_workflow(document: dict[str, Any], major: str) -> None:
    """Required PR/main checks cover actual source, platform, engine and sanitizer routes."""
    if document.get("on", {}).get("pull_request") not in ("", {}) or document["on"].get("push") != {
        "branches": ["main"]
    }:
        msg = "Required quality workflow must run on every PR and main push"
        raise ValueError(msg)
    require_workflow_coverage(document)
    structural = required_job(document, "structural")
    for command in (
        SOURCE_GATE,
        "python tools/install_build_tools.py",
        "python tools/install_build_tools.py --lint",
        "python tools/install_llvm.py --compiler",
        'python tools/check_reference_suite.py --compiler g++ --jobs "$DE_BUILD_JOBS"',
        (
            f"python tools/check_reference_suite.py --compiler clang++-{major} "
            '--sanitize --jobs "$DE_BUILD_JOBS"'
        ),
    ):
        required_step(structural, command)
    native = required_job(document, "native")
    require_strategy(
        native,
        {"include": [{"platform": name, "os": runner} for name, runner in NATIVE_RUNNERS.items()]},
    )
    required_step(native, "python tools/install_build_tools.py")
    required_step(native, "python tools/install_llvm.py")
    source_cache(native, required_step(native, "cmake -P cmake/AcquireDependencies.cmake"))
    required_step(native, "cmake --workflow --preset release", "runner.os != 'Windows'")
    required_step(native, "./tools/ci_windows.ps1", "runner.os == 'Windows'")
    required_step(native, NATIVE_SMOKE, "runner.os != 'Windows'")
    fuzz_job(document, major, nightly=False)
    sanitizer_job(document, major)
    aggregate_job(document)


def hook_errors(root: Path) -> list[str]:
    """The actual local hook invokes the aggregate without optional stages or arguments."""
    hooks = workflow(root / ".pre-commit-config.yaml")
    candidates = [
        hook
        for repo in hooks["repos"]
        if repo.get("repo") == "local"
        for hook in repo.get("hooks", [])
        if hook.get("entry") == SOURCE_GATE
    ]
    if len(candidates) != 1:
        return ["Local hook must execute the authoritative source gate"]
    hook = candidates[0]
    if (
        hook.get("language") != "system"
        or hook.get("pass_filenames") != "false"
        or hook.get("always_run") != "true"
        or "stages" in hook
        or "args" in hook
    ):
        return ["Local aggregate hook must always run with its complete default checks"]
    return []


def integrity_errors(root: Path) -> list[str]:
    """Run semantic checks with file-specific diagnostics and no false success on parse failure."""
    try:
        errors = hook_errors(root)
    except (yaml.YAMLError, ValueError, TypeError, KeyError, AttributeError) as error:
        errors = [f".pre-commit-config.yaml: {error}"]
    major = str(json.loads((root / "deps/tools.json").read_text())["clang_tidy"]["version"]).split(
        "."
    )[0]
    directory = root / ".github/workflows"
    paths = sorted([*directory.glob("*.yml"), *directory.glob("*.yaml")])
    if {path.name for path in paths} != {"ci.yml", "nightly.yml", "source.yml"}:
        errors.append("Workflow inventory must retain the three reviewed execution contracts")
    for path in paths:
        try:
            document = workflow(path)
            errors.extend(f"{path.name}: {error}" for error in action_errors(document))
            if path.name == "ci.yml":
                quality_workflow(document, major)
            elif path.name == "nightly.yml":
                if document.get("concurrency") != {
                    "group": "nightly-${{ github.workflow }}-${{ github.ref }}",
                    "cancel-in-progress": "false",
                    "queue": "max",
                }:
                    raise ValueError("Nightly concurrency must retain complete pending runs")
                advisories = required_job(document, "advisories")
                source_cache(advisories, required_step(advisories, "python tools/deps.py fetch"))
                scan = required_step(
                    advisories,
                    "python tools/check_advisories.py --report out/advisories/report.json",
                )
                upload = require_evidence_upload(
                    advisories,
                    name="Upload source-bound advisory observations",
                    path="out/advisories/report.json",
                    artifact="advisory-${{ github.run_id }}-${{ github.run_attempt }}",
                    days=30,
                )
                if advisories["steps"].index(upload) <= advisories["steps"].index(scan):
                    raise ValueError("Advisory evidence must be uploaded after observation")
                sanitizer_job(document, major)
                fuzz_job(document, major, nightly=True)
            elif path.name == "source.yml":
                required_step(required_job(document, "source"), SOURCE_GATE)
                source = document["jobs"]["source"]
                required_step(
                    source,
                    "python tools/install_build_tools.py\n"
                    "python tools/install_build_tools.py --lint",
                )
                required_step(source, "python tools/install_llvm.py --compiler")
                required_step(source, "python tools/package_source.py")
                publish = document["jobs"]["publish"]
                errors.extend(
                    ["Publication requires contents-write and Actions-read only"]
                    if publish.get("permissions") != {"contents": "write", "actions": "read"}
                    else []
                )
                required_step(publish, "python tools/publish_source_release.py --check")
        except (yaml.YAMLError, ValueError, TypeError, KeyError, AttributeError) as error:
            errors.append(f"{path.name}: {error}")
    if not errors:
        errors.extend(python_errors(root))
    return errors
