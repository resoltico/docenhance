#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Validate actual C++ descriptor output using an independent Draft2020-12 implementation."""

from __future__ import annotations

import json
import subprocess
import sys

from jsonschema import Draft202012Validator

EXPECTED = {
    "PNG source precision 1",
    "PNG source precision 2",
    "PNG source precision 4",
    "PNG source precision 8",
    "PNG source precision 16",
    "PNG source color 0",
    "PNG source color 2",
    "PNG source color 3",
    "PNG source color 4",
    "PNG source color 6",
    "string domains",
    "LF dot suffix",
    "LF component",
    "LF only",
    "three dots",
    "LF dot prefix",
    "128 UTF8 byte path",
    "empty literal",
    "packet",
    "numeric packet",
    "png",
    "jpeg",
    "text",
    "light",
    "noise",
    "stages",
    "completed",
    "empty object",
    "nullable variant",
    "levels facts",
    "active levels facts",
    "gamma facts",
    "clahe facts",
    "unsharp facts",
    "otsu facts",
    "sauvola facts",
    "fixed facts",
    "nlm facts",
    "nlm no-effect facts",
    "tvl1 active facts",
    "tvl1 bypass facts",
    "morphology active facts",
    "morphology bypass facts",
    "morphology no-effect facts",
    "surface active facts",
    "surface no-effect facts",
    "surface bypass facts",
    "wiener active facts",
    "wiener bypass facts",
    "wiener no-effect facts",
    "wiener kernel edge facts",
    "stage reports",
    "completed stages",
    "empty completed stages",
}


def main() -> int:
    """Reconcile every compiled fixture and require healthy and negative schema execution."""
    result = subprocess.run([sys.argv[1]], capture_output=True, text=True, check=True, timeout=30)
    examples = json.loads(result.stdout)
    if len(examples) != len(EXPECTED) or {case["name"] for case in examples} != EXPECTED:
        message = "Descriptor schema examples are missing, repeated or disconnected"
        raise AssertionError(message)
    refused = 0
    for case in examples:
        Draft202012Validator.check_schema(case["schema"])
        validator = Draft202012Validator(case["schema"])
        validator.validate(case["value"])
        for value in case["invalid"]:
            if validator.is_valid(value):
                message = f"{case['name']}: independently invalid value admitted"
                raise AssertionError(message)
            refused += 1
    strings = next(case for case in examples if case["name"] == "string domains")
    validator = Draft202012Validator(strings["schema"])
    # JSON Schema counts codepoints; the native domain still rejects these 130 UTF8 bytes.
    character_only = strings["value"] | {"path": "é" * 65}
    validator.validate(character_only)
    # format is an annotation here; actual Gregorian calendar admission belongs to native code.
    validator.validate(strings["value"] | {"instant": "2023-02-29T23:59:59Z"})
    if not refused:
        message = "No schema refusal controls executed"
        raise AssertionError(message)
    print(f"PASS: {len(examples)} C++ wire/schema round trips; {refused} independent refusals")
    return 0


if __name__ == "__main__":
    sys.exit(main())
