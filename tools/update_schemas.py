# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Write or compare schemas emitted by the supplied current native executable."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

DESTINATIONS = {
    "command-response.schema.json": "schemas/command-response.schema.json",
    "run-record.schema.json": "schemas/run-record.schema.json",
    "record.dict": "fuzz/dict/record.dict",
}


def prepare_outputs(stdout: bytes, stderr: bytes) -> tuple[Path, dict[Path, bytes]]:
    """Validate the complete native inventory and prepare every byte before any writes."""
    emitted = json.loads(stdout)
    if not isinstance(emitted, dict) or set(emitted) != set(DESTINATIONS):
        message = "Native emitter returned an unexpected schema inventory"
        raise ValueError(message)
    if stderr:
        message = "Native schema emitter wrote diagnostics on a successful exit"
        raise ValueError(message)
    root = Path(__file__).resolve().parents[1]
    outputs: dict[Path, bytes] = {}
    for name, schema in emitted.items():
        if name == "record.dict":
            if not isinstance(schema, str) or not schema.startswith(
                "# Generated from actual native"
            ):
                message = "Native record dictionary has an unexpected representation"
                raise ValueError(message)
            expected = schema.encode("utf-8")
        else:
            if not isinstance(schema, dict):
                message = f"Native schema is not an object: {name}"
                raise ValueError(message)
            expected = (json.dumps(schema, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
        outputs[root / DESTINATIONS[name]] = expected
    return root, outputs


def main() -> int:
    """Keep schema content owned by compiled typed Fields, with exact byte comparison."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("emitter", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    result = subprocess.run(
        [str(args.emitter.resolve())], capture_output=True, check=True, timeout=30
    )
    root, outputs = prepare_outputs(result.stdout, result.stderr)
    stale = []
    for target, expected in outputs.items():
        actual = target.read_bytes() if target.exists() else None
        if actual == expected:
            continue
        if args.check:
            stale.append(target.relative_to(root).as_posix())
        else:
            target.write_bytes(expected)
    if stale:
        message = "Schemas differ from current native Fields: " + ", ".join(sorted(stale))
        raise ValueError(message)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
