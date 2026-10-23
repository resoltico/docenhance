# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Bind the existing actual compiler -H output to its observed physical header identities."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from architecture import GENERATED_INCLUDES
from compile_db import compiler_arguments, header_trace_values
from quality_json import canonical
from quality_native_contract import (
    compile_snapshot,
    native_path,
    need,
    object_snapshot,
    physical,
    text_snapshot,
)
from quality_provenance_contract import exact


def header_scope(body: dict[str, Any], selected: list[dict[str, str]], files: set[str]) -> None:
    """Bind actual compiler trace spellings to selected physical implementation headers."""
    headers = object_snapshot(body["headers"])
    required = {entry["file"] for entry in selected}
    need(
        required == set(headers),
        "Compiler header observations differ from selected native TUs",
    )
    system = object_snapshot(body["configuration"])["CMAKE_SYSTEM_NAME"]
    observed_files = set(files)
    observed_identities = set()
    for entry in selected:
        observed = headers[entry["file"]]
        exact(observed, {"argv", "exit_code", "timed_out", "stdout", "stderr", "files"})
        need(
            type(observed["exit_code"]) is int
            and observed["exit_code"] == 0
            and observed["timed_out"] is False
            and text_snapshot(observed["stdout"]) == "",
            "Actual compiler header observation did not complete",
        )
        # This is the existing POSIX compiler producer, not a new MSVC adapter.
        need(
            system != "Windows",
            "Actual reviewed MSVC header capture is an unmet operational prerequisite",
        )
        base = [*compiler_arguments(entry), entry["file"], "-H"]
        need(
            observed["argv"] in ([*base, "-fsyntax-only"], [*base, "-E", "-o", "/dev/null"]),
            "Header observation differs from the actual selected compile command",
        )
        text = text_snapshot(observed["stderr"])
        consumed, spellings = header_trace_values(
            text, object_snapshot(body["configuration"])["CMAKE_CXX_COMPILER_ID"]
        )
        need(consumed is None, "Consumed precompiled headers lack actual reviewed closure")
        need(
            isinstance(observed["files"], list)
            and observed["files"]
            and [item["spelling"] for item in observed["files"]] == spellings,
            "Physical headers differ from SAME actual compiler trace spellings",
        )
        for item in observed["files"]:
            exact(item, {"spelling", "identity"})
            native_path(item["spelling"], system)
            physical(item["identity"], system)
            observed_files.add(item["identity"]["path"])
            observed_identities.add(canonical(item["identity"]))
    for dependency in body["dependencies"]:
        need(
            all(canonical(item) in observed_identities for item in dependency["installed"]),
            "Declared provider headers/adapters were not observed "
            "in actual selected compiler traces",
        )
    declared = {item["path"] for item in body["generated"]}
    need(
        declared <= observed_files
        and all(
            any(path.replace("\\", "/").endswith("/" + suffix) for path in declared)
            for suffix in GENERATED_INCLUDES
        ),
        "Generated native implementation inputs lack compiler correspondence",
    )


def header_snapshots(body: dict[str, Any]) -> list[dict[str, Any]]:
    """List only the existing actual compiler trace stdout/stderr snapshots."""
    return [
        observed[name]
        for observed in object_snapshot(body["headers"]).values()
        for name in ("stdout", "stderr")
    ]


def header_files(body: dict[str, Any]) -> list[dict[str, Any]]:
    """List the physical headers observed in the actual compiler traces."""
    return [
        item["identity"]
        for observed in object_snapshot(body["headers"]).values()
        for item in observed["files"]
    ]


def identified_headers(body: dict[str, Any]) -> None:
    """Resolve observed spellings at pre-effect admission against their actual build owner."""
    headers = object_snapshot(body["headers"])
    directories = {
        entry["file"]: entry["directory"]
        for entry in compile_snapshot(body["compile_commands"])
        if entry.get("file") in headers
    }
    for source, observed in headers.items():
        for item in observed["files"]:
            actual = (Path(directories[source]) / item["spelling"]).resolve(strict=True)
            need(
                actual == Path(item["identity"]["path"]),
                "Actual compiler header spelling resolves to a different physical implementation",
            )
