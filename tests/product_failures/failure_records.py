# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent read-only bundle disagreement probes with retained mutated artifacts."""

from __future__ import annotations

import hashlib
import json
import shutil
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path

    from failure_audit import Audit


def snapshot(directory: Path) -> dict[str, str]:
    """Record exact bytes/link targets; verification must leave both unchanged."""
    result = {}
    for path in directory.rglob("*"):
        if path.is_symlink():
            result[str(path.relative_to(directory))] = f"link:{path.readlink()}"
        elif path.is_file():
            result[str(path.relative_to(directory))] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def record_mutations(original: bytes) -> dict[str, bytes]:
    """Break supported version, grammar, inventory and truthful output metadata independently."""
    record = json.loads(original)
    version = json.loads(original)
    version["record"]["version"] = 1
    unknown = json.loads(original)
    unknown["unreviewed"] = True
    dimensions = json.loads(original)
    dimensions["output"]["width"] += 1
    request = json.loads(original)
    request["request"]["operation"]["parameters"]["depth"] = "obsolete"
    first_key = next(iter(record))
    duplicate = json.dumps(record).encode()
    duplicate = b"{" + json.dumps(first_key).encode() + b":null," + duplicate[1:]
    return {
        "record-truncated": original.rstrip()[:-1],
        "record-invalid-utf8": b'{"a":"\xff"}',
        "record-duplicate-key": duplicate,
        "record-duplicate-decoded-key": rb'{"a":1,"\u0061":2}',
        "record-obsolete-version": json.dumps(version).encode(),
        "record-unknown-field": json.dumps(unknown).encode(),
        "record-wrong-dimensions": json.dumps(dimensions).encode(),
        "record-obsolete-depth": json.dumps(request).encode(),
        "record-depth-ceiling": b"[" * 65 + b"0" + b"]" * 65,
    }


def refuse_bundle(audit: Audit, name: str, directory: Path) -> None:
    """Check both the error and the real read-only effects, including symlink inventory."""
    before = snapshot(directory)
    audit.invoke(name, ["verify", str(directory), "--json"], (3, "E_INPUT", "not_started"))
    audit.check(f"{name}-effects", snapshot(directory) == before, "Verification preserved bundle")


def bundle_refusals(audit: Audit, source: Path) -> None:
    """Mutations must fail verification; no recorded request may be executed on reading."""
    original = audit.workspace / "bundle-original"
    response = audit.invoke("bundle-create", audit.process(source, original), (0, "", "completed"))
    record_bytes = (original / "run.json").read_bytes()
    audit.check(
        "bundle-identities",
        response["record"]["sha256"] == hashlib.sha256(record_bytes).hexdigest()
        and json.loads(record_bytes)["source"]["sha256"]
        == hashlib.sha256(source.read_bytes()).hexdigest(),
        "Independent source and record digests agree with the delivered identities",
    )
    for name, data in record_mutations(record_bytes).items():
        directory = audit.workspace / name
        shutil.copytree(original, directory)
        (directory / "run.json").write_bytes(data)
        refuse_bundle(audit, name, directory)
    for name in ("artifact-corrupt", "artifact-missing", "artifact-extra", "artifact-link"):
        directory = audit.workspace / name
        shutil.copytree(original, directory)
        image = directory / "result.png"
        if name == "artifact-corrupt":
            image.write_bytes(image.read_bytes()[:-1])
        elif name == "artifact-extra":
            (directory / "foreign.txt").write_bytes(b"foreign")
        else:
            image.unlink()
            if name == "artifact-link":
                image.symlink_to(original / "result.png")
        refuse_bundle(audit, name, directory)
