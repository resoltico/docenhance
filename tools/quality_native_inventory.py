# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Concrete selected native metadata and implementation file inventories."""

from __future__ import annotations

from typing import Any

from audit_build import cache_values
from quality_json import ROOT
from quality_native_contract import native_path, object_snapshot, text_snapshot
from quality_native_headers import header_files, header_snapshots
from quality_provenance_contract import sha256_text

JSON_SNAPSHOTS = (
    "configuration",
    "targets",
    "architecture",
    "compile_commands",
    "dependency_lock",
    "dependency_features",
    "headers",
    "wire_schema",
)


def _snapshots(body: dict[str, Any]) -> list[dict[str, Any]]:
    """List embedded native metadata and actual captured tool-output snapshots."""
    result = [body[key] for key in JSON_SNAPSHOTS]
    result.append(body["link_commands"])
    for item in body["dependencies"]:
        result.extend((item["source"], item["configuration"]))
    result.extend((body["imports"]["stdout"], body["imports"]["stderr"]))
    result.extend(header_snapshots(body))
    return result


def _physical_files(body: dict[str, Any]) -> list[dict[str, Any]]:
    """List actual executable, producer, tool, header and static-provider identities."""
    result = [
        body["executable"],
        body["producer"]["source"],
        *body["generated"],
        *body["toolchain"].values(),
        body["imports"]["tool"],
        *body["imports"]["resolved"].values(),
    ]
    result.extend(header_files(body))
    for item in body["dependencies"]:
        result.extend((*item["installed"], *item["archives"]))
    return result


def _source_files(body: dict[str, Any]) -> list[dict[str, str]]:
    """Derive physical source and SDK identities from their owning frozen metadata."""
    result = [
        {"path": str(ROOT / name), "sha256": digest}
        for name, digest in object_snapshot(body["source_epoch"])["files"].items()
    ]
    system = object_snapshot(body["configuration"])["CMAKE_SYSTEM_NAME"]
    for item in body["dependencies"]:
        cache = cache_values(text_snapshot(item["configuration"]))
        source = native_path(
            cache["DE_UPSTREAM_SOURCE"]
            if "DE_UPSTREAM_SOURCE" in cache
            else cache["CMAKE_HOME_DIRECTORY"],
            system,
        )
        result.extend(
            {"path": str(source / name), "sha256": digest}
            for name, digest in object_snapshot(item["source"])["files"].items()
        )
    config = object_snapshot(body["configuration"])
    if system == "Darwin":
        sha256_text(config["sdk_settings_sha256"])
        result.append(
            {
                "path": str(native_path(config["sdk"], system) / "SDKSettings.json"),
                "sha256": config["sdk_settings_sha256"],
            }
        )
    return result
