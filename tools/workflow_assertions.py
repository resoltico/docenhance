# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Mandatory CI aggregates, action identity and failure-evidence publication."""

from __future__ import annotations

import re
from typing import Any

from ci_contract import JOB_TITLES


def require_evidence_upload(
    job: dict[str, Any], *, name: str, path: str, artifact: str, days: int
) -> dict[str, Any]:
    """Failed jobs must upload an actual artifact; missing bytes are fatal."""
    steps: list[dict[str, Any]] = [
        item for item in job.get("steps", []) if item.get("name") == name
    ]
    if len(steps) != 1:
        message = f"Required evidence upload missing or duplicated: {name}"
        raise ValueError(message)
    step = steps[0]
    expected = {
        "name": artifact,
        "path": path,
        "if-no-files-found": "error",
        "retention-days": str(days),
    }
    if (
        step.get("uses") != "actions/upload-artifact@cf430e030ddbb5b0abf93d22962f4752f3646cd9"
        or step.get("if") != "always()"
        or step.get("with") != expected
        or step.get("continue-on-error", "false") != "false"
    ):
        message = f"Required evidence upload has unsafe settings: {name}"
        raise ValueError(message)
    return step


def require_native_diagnostics(
    job: dict[str, Any], preset: str, identity: str, after: dict[str, Any]
) -> None:
    """Retain only bounded compressed native diagnostics after the owning build."""
    name = "Retain complete native diagnostics"
    matches = [item for item in job.get("steps", []) if item.get("name") == name]
    if len(matches) != 1:
        msg = "Required native diagnostic upload is missing or duplicated"
        raise ValueError(msg)
    step = matches[0]
    prefix = f"out/{preset}/app/native-*/"
    expected = {
        "name": f"native-${{{{ matrix.{identity} }}}}"
        "-${{ github.run_id }}-${{ github.run_attempt }}",
        "path": "\n".join(
            prefix + suffix for suffix in ("ctest-output*.gz*", "ctest.xml.gz*", "native-*.json")
        )
        + "\n",
        "if-no-files-found": "error",
        "compression-level": "0",
        "retention-days": "7",
    }
    if (
        step.get("uses") != "actions/upload-artifact@cf430e030ddbb5b0abf93d22962f4752f3646cd9"
        or step.get("if") != "always()"
        or step.get("with") != expected
        or step.get("continue-on-error", "false") != "false"
        or job["steps"].index(step) <= job["steps"].index(after)
    ):
        msg = "Required native diagnostic upload has unsafe settings or ordering"
        raise ValueError(msg)


def aggregate_job(document: dict[str, Any]) -> None:
    """The always-running aggregate refuses every non-success result from every required job."""
    names = set(JOB_TITLES) - {"gate"}
    gate = document["jobs"]["gate"]
    if (
        set(document["jobs"]) != names | {"gate", "tooling", "paired_capability"}
        or set(gate.get("needs", [])) != names
    ):
        msg = "Aggregate must depend on every required quality job"
        raise ValueError(msg)
    if gate.get("if") != "always()" or gate.get("continue-on-error", "false") != "false":
        msg = "Aggregate must run and refuse non-success results"
        raise ValueError(msg)
    variables = {f"{name.upper()}_RESULT": f"${{{{ needs.{name}.result }}}}" for name in names}
    lines = {f'test "${variable}" = success' for variable in variables}
    steps = gate.get("steps", [])
    if len(steps) != 1 or set(steps[0].get("run", "").strip().splitlines()) != lines:
        msg = "Aggregate must execute each actual result assertion"
        raise ValueError(msg)
    step = steps[0]
    if (
        step.get("env") != variables
        or step.get("shell") != "bash"
        or "if" in step
        or step.get("continue-on-error", "false") != "false"
    ):
        msg = "Aggregate result bindings and fatal Bash execution must agree"
        raise ValueError(msg)


def action_errors(document: dict[str, Any]) -> list[str]:
    """Inspect actual Action fields and trigger keys, excluding comments and decorative strings."""
    actions = [job.get("uses", "") for job in document.get("jobs", {}).values()]
    actions += [
        step["uses"]
        for job in document.get("jobs", {}).values()
        for step in job.get("steps", [])
        if "uses" in step
    ]
    errors = [
        f"Unpinned action: {action}"
        for action in actions
        if action
        and not action.startswith("./")
        and not re.fullmatch(r"[^@]+@[0-9a-f]{40}", action)
    ]
    if "pull_request_target" in document.get("on", {}):
        errors.append("Privileged pull-request trigger prohibited")
    if "defaults" in document:
        errors.append("Workflow execution defaults may not remap required checks")
    return errors


def required_job(
    document: dict[str, Any], name: str, *, condition: str | None = None
) -> dict[str, Any]:
    """Required jobs retain only the exact reviewed route condition and fatal execution."""
    job: dict[str, Any] = document["jobs"][name]
    invalid_condition = "if" in job if condition is None else job.get("if") != condition
    if invalid_condition or job.get("continue-on-error", "false") != "false" or "defaults" in job:
        msg = f"Required job is conditional, optional or changes execution defaults: {name}"
        raise ValueError(msg)
    if "needs" in job:
        msg = f"Required execution jobs must start independently: {name}"
        raise ValueError(msg)
    return job


def require_strategy(job: dict[str, Any], matrix: dict[str, Any]) -> None:
    """A complete reviewed matrix has no exclusion, fail-fast or dynamic substitution."""
    if job.get("strategy") != {"fail-fast": "false", "matrix": matrix}:
        msg = "Required matrix coverage or independent execution differs"
        raise ValueError(msg)
