# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual pre-effect native evidence acquisition and retained selected executable admission."""

from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from project_version import version_from_text
from quality_acquisition import (
    FileIdentity,
    acquire_file,
    acquire_payload,
    acquire_reference,
    confined,
    fingerprint,
)
from quality_attempt import MAX_RESPONSE_BYTES
from quality_json import ROOT, canonical, read_json
from quality_native_body import validate_native_body
from quality_native_components import native_graph
from quality_native_contract import compilation_values, need, object_snapshot
from quality_native_headers import header_snapshots, identified_headers
from quality_native_inventory import JSON_SNAPSHOTS, _physical_files, _source_files


def _same_snapshot(
    root: Path, value: dict[str, Any], acquired: dict[tuple[str, str], bytes], *, text: bool = False
) -> None:
    """Compare embedded values with the same once-acquired identified metadata bytes."""
    reference = value["reference"]
    key = (reference["path"], reference["sha256"])
    if key not in acquired:
        acquired[key] = acquire_payload(root, reference)
    actual: object
    if text:
        actual = acquired[key].decode("utf-8", "strict")
    else:
        # Only compile_commands has an array value in the admitted closed native body.
        actual = (
            compilation_values(acquired[key])
            if isinstance(value["value"], list)
            else read_json(acquired[key])
        )
    need(
        canonical(actual) == canonical(value["value"]),
        "Native snapshot differs from SAME identified bytes",
    )


def _source_admission(body: dict[str, Any], root: Path) -> None:
    """Bind the actual selected source epoch and every implementation-bearing input."""
    declared = object_snapshot(body["source_epoch"])
    actual = acquire_reference(root, body["source_epoch"]["reference"])
    need(
        canonical({key: actual[key] for key in declared}) == canonical(declared),
        "Native source projection differs from actual producer input evidence",
    )
    files = declared["files"]
    config = object_snapshot(body["configuration"])
    need(config.get(str(ROOT)) == str(ROOT), "Actual configured source root differs")
    for name in native_graph(body)[0]:
        relative = Path(name).relative_to(ROOT).as_posix()
        need(
            relative in files, "Actual selected native source is absent from the dirty source epoch"
        )
    for name, value in config.items():
        if name in files:
            need(value == files[name], "Actual configured native recipe differs from source epoch")
    for directory in ("src", "include", "cmake", "deps", "spec", "schemas", "tools"):
        expected = {
            path.relative_to(ROOT).as_posix()
            for path in (ROOT / directory).rglob("*")
            if path.is_file() and "__pycache__" not in path.parts
        }
        need(
            expected <= set(files), "Native source epoch omits current implementation-bearing files"
        )
    raw = acquire_payload(ROOT, {"path": "CMakeLists.txt", "sha256": files["CMakeLists.txt"]})
    need(
        version_from_text(raw.decode("utf-8")) == body["build"]["application_version"],
        "Native application version differs from actual source",
    )
    for key, name in (
        ("architecture", "spec/architecture.json"),
        ("dependency_lock", "deps/lock.json"),
        ("dependency_features", "deps/features.json"),
        ("wire_schema", "schemas/command-response.schema.json"),
    ):
        need(
            body[key]["reference"]["sha256"] == files[name],
            "Native contract/dependency identity differs",
        )
    need(
        body["build"]["dependency_lock_sha256"] == files["deps/lock.json"],
        "Native BuildFacts lock identity differs",
    )


def _raw_producer(body: dict[str, Any], root: Path) -> None:
    """Match complete raw stdout and empty stderr using the retained acquired payloads."""
    producer = body["producer"]
    values: dict[str, bytes] = {}
    for name in ("stdout", "stderr"):
        ref = {**producer["execution"][name], "bytes": producer["raw_bytes"][name]}
        acquired = acquire_file(root, ref, MAX_RESPONSE_BYTES)
        need(
            not acquired.failures
            and acquired.identity_bound
            and acquired.stable
            and acquired.read_state == "eof"
            and acquired.extent == "exact"
            and acquired.closed is True
            and acquired.bytes == producer["raw_bytes"][name],
            "Actual producer raw stream is incomplete, unbound or has different length",
        )
        payload = acquired.payload
        if payload is None:
            message = "Actual producer raw stream is incomplete, unbound or has different length"
            raise ValueError(message)
        values[name] = payload
    need(
        values["stderr"] == b""
        and canonical(read_json(values["stdout"])) == canonical(producer["execution"]["response"]),
        "Native producer response differs from the SAME complete stdout bytes",
    )


@dataclass(frozen=True)
class NativeAdmission:
    """Retained concrete selected executable identity after pre-effect correspondence."""

    executable: Path
    executable_identity: FileIdentity
    system_directory: str | None
    target_platform: str

    def before_launch(self) -> None:
        """Recheck retained implementation identities before another launch."""
        need(
            fingerprint(self.executable) == self.executable_identity,
            "Selected executable changed after concrete native admission",
        )


def admit_native(
    body: dict[str, Any], plan: dict[str, Any], root: Path, exe: Path, build: dict[str, Any]
) -> NativeAdmission:
    """Bind one independently reviewed concrete native body before any journal/child effect."""
    validate_native_body(body)
    protocol = plan["protocol"]
    need(
        canonical(body["build"]) == canonical(build)
        and body["source_epoch"]["value"]["head"] == protocol["source_commit"]
        and body["executable"]["sha256"] == protocol["executable_sha256"],
        "Native body differs from frozen protocol",
    )
    config = object_snapshot(body["configuration"])
    need(
        config["CMAKE_SYSTEM_NAME"]
        == {"darwin": "Darwin", "linux": "Linux", "win32": "Windows"}.get(sys.platform),
        "Selected native execution platform differs from this evaluator",
    )
    _source_admission(body, root)
    acquired: dict[tuple[str, str], bytes] = {}
    for key in JSON_SNAPSHOTS:
        _same_snapshot(root, body[key], acquired)
    _same_snapshot(root, body["link_commands"], acquired, text=True)
    for item in body["dependencies"]:
        _same_snapshot(root, item["source"], acquired)
        _same_snapshot(root, item["configuration"], acquired, text=True)
    for name in ("stdout", "stderr"):
        _same_snapshot(root, body["imports"][name], acquired, text=True)
    for value in header_snapshots(body):
        _same_snapshot(root, value, acquired, text=True)
    identified_headers(body)
    for item in _physical_files(body):
        actual = fingerprint(Path(item["path"]).resolve(strict=True))
        need(
            canonical(actual) == canonical({key: item[key] for key in ("sha256", "bytes")}),
            "Native generated/header/toolchain/provider/import identity changed",
        )
    for item in _source_files(body):
        if item["path"] == str(ROOT / "CMakeLists.txt"):
            continue  # The SAME bounded CMake payload above supplied both hash and version.
        need(
            fingerprint(Path(item["path"]).resolve(strict=True))["sha256"] == item["sha256"],
            "Selected native source/dependency/SDK bytes differ",
        )
    selected = fingerprint(exe)
    need(
        selected == {key: body["executable"][key] for key in ("sha256", "bytes")},
        "Selected CLI body differs from actual producer",
    )
    argument = body["producer"]["execution"]["argv"][2]
    source = Path(argument) if Path(argument).is_absolute() else confined(root, argument)
    need(
        source == Path(body["producer"]["source"]["path"]),
        "Native producer argv names another actual source",
    )
    _raw_producer(body, root)
    return NativeAdmission(
        exe, selected, body["imports"]["system_directory"], config["CMAKE_SYSTEM_NAME"]
    )
