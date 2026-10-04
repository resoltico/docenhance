# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Reconcile complete test discovery with fresh CTest and Catch execution evidence."""

from __future__ import annotations

import json
import subprocess
import xml.etree.ElementTree as ET
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from pathlib import Path


class EvidenceError(ValueError):
    """Discovery or execution cannot establish complete passing verification."""


def discovery(ctest: str, build: Path) -> list[dict[str, Any]]:
    """Read actual configured tests, refusing disabled and skippable registrations."""
    result = subprocess.run(
        [ctest, "--test-dir", str(build), "--show-only=json-v1"],
        capture_output=True,
        text=True,
        check=True,
        timeout=30,
    )
    tests: list[dict[str, Any]] = json.loads(result.stdout)["tests"]
    names = [test["name"] for test in tests]
    if not names or len(names) != len(set(names)):
        msg = "Test discovery must be nonempty and unique"
        raise EvidenceError(msg)
    for test in tests:
        properties = {item["name"]: item["value"] for item in test.get("properties", [])}
        if (
            not test.get("command")
            or properties.get("DISABLED")
            or any(
                key in properties
                for key in (
                    "SKIP_RETURN_CODE",
                    "SKIP_REGULAR_EXPRESSION",
                    "WILL_FAIL",
                    "PASS_REGULAR_EXPRESSION",
                )
            )
        ):
            msg = f"Test is absent, disabled or skippable: {test['name']}"
            raise EvidenceError(msg)
    return tests


def report_xml(text: str) -> ET.Element:
    """Parse only UTF-8 generated execution reports, refusing entity/DOCTYPE declarations."""
    if "<!DOCTYPE" in text or "<!ENTITY" in text:
        msg = "Execution evidence may not declare a DTD or entities"
        raise EvidenceError(msg)
    try:
        return ET.fromstring(text)  # noqa: S314 -- DTD/entity declarations are rejected above.
    except ET.ParseError as error:
        msg = "Execution evidence is not complete well-formed XML"
        raise EvidenceError(msg) from error


def catch_result(text: str, name: str) -> None:
    """A passing Catch process must execute its selected case and nonzero assertions."""
    document = report_xml(text)
    cases = document.findall("TestCase")
    results = document.find("OverallResults")
    if document.tag != "Catch2TestRun" or len(cases) != 1 or cases[0].get("name") != name:
        msg = f"Catch did not execute the selected case: {name}"
        raise EvidenceError(msg)
    if (
        results is None
        or int(results.get("successes", "0")) <= 0
        or any(results.get(key) != "0" for key in ("failures", "expectedFailures", "skips"))
    ):
        msg = f"Catch case failed, skipped or performed no assertions: {name}"
        raise EvidenceError(msg)


def complete_junit(path: Path, expected: set[str], units: set[str] | None = None) -> None:
    """Require exactly one successful executed result per discovered test, with no skips."""
    document = report_xml(path.read_text(encoding="utf-8"))
    if document.tag != "testsuite" or not expected:
        msg = "JUnit must describe a nonempty complete suite"
        raise EvidenceError(msg)
    cases = document.findall("testcase")
    names = [case.get("name") for case in cases]
    if len(names) != len(expected) or set(names) != expected:
        msg = "JUnit result identities differ from complete discovery"
        raise EvidenceError(msg)
    for case in cases:
        name = str(case.get("name"))
        if case.get("status") != "run" or any(
            case.find(key) is not None for key in ("failure", "error", "skipped")
        ):
            msg = f"Test did not pass through execution: {name}"
            raise EvidenceError(msg)
        if units is not None and name in units:
            catch_result(case.findtext("system-out", ""), name)
