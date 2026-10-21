# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual reachable native target, dependency and OS import relationships."""

from __future__ import annotations

from typing import Any

from architecture import Manifest
from audit_build import cache_values
from package_inspection import native_import_command, native_import_values, windows_api_contract
from quality_json import canonical
from quality_native_contract import (
    DISABLED,
    native_path,
    need,
    object_snapshot,
    physical,
    text_snapshot,
)
from quality_provenance_contract import exact, sha256_text


def native_graph(body: dict[str, Any]) -> tuple[set[str], set[str]]:
    """Follow actual selected first-party targets and their declared external packages."""
    manifest = Manifest(object_snapshot(body["architecture"]))
    targets = object_snapshot(body["targets"])
    need(
        "docenhance" in targets,
        "Actual native target evidence is absent",
    )
    pending, seen, files, external = ["docenhance"], set(), set(), set()
    while pending:
        target = pending.pop()
        if target in seen:
            continue
        seen.add(target)
        node = targets[target]
        exact(node, {"files", "layer", "links"})
        need(manifest.target_layer.get(target) == node["layer"], "Native target layer differs")
        need(
            isinstance(node["files"], list)
            and node["files"]
            and all(isinstance(path, str) for path in node["files"]),
            "Native source inventory is absent",
        )
        need(
            isinstance(node["links"], list)
            and all(isinstance(link, str) for link in node["links"])
            and len(set(node["links"])) == len(node["links"]),
            "Native link inventory is invalid",
        )
        layer = manifest.layers[node["layer"]]
        native = {link for link in node["links"] if link in manifest.target_layer}
        packages = {
            manifest.package_target[link]
            for link in node["links"]
            if link in manifest.package_target
        }
        need(
            set(node["links"])
            == native | {link for link in node["links"] if link in manifest.package_target}
            and {manifest.target_layer[item] for item in native} == set(layer["uses"])
            and packages == set(layer["external"]),
            "Actual direct native links differ from source architecture",
        )
        files.update(node["files"])
        external.update(packages)
        pending.extend(native)
    return files, external


def dependency_scope(body: dict[str, Any], expected_names: set[str]) -> None:
    """Bind selected dependency receipts, feature caches and static file identities."""
    lock = object_snapshot(body["dependency_lock"])
    features = object_snapshot(body["dependency_features"])
    locked = {item["name"]: item for item in lock["dependencies"]}
    dependencies = body["dependencies"]
    need(
        isinstance(dependencies, list)
        and {item["name"] for item in dependencies} == expected_names
        and len(dependencies) == len(expected_names),
        "Actual native dependency closure differs",
    )
    system = object_snapshot(body["configuration"])["CMAKE_SYSTEM_NAME"]
    for item in dependencies:
        exact(item, {"name", "source", "configuration", "installed", "archives"})
        receipt = object_snapshot(item["source"])
        exact(receipt, {"dependency", "resolved_commit", "files"})
        name = item["name"]
        need(
            canonical(receipt["dependency"]) == canonical(locked[name])
            and isinstance(receipt["files"], dict)
            and receipt["files"],
            "Selected native source receipt differs from the immutable lock",
        )
        for path, digest in receipt["files"].items():
            relative = native_path(path, "Linux")
            need(
                not relative.is_absolute() and ".." not in relative.parts,
                "Native dependency receipt path escapes its source",
            )
            sha256_text(digest)
        if locked[name].get("object_type") == "commit":
            need(
                receipt["resolved_commit"] == locked[name]["object"],
                "Native dependency commit differs",
            )
        cache_text = text_snapshot(item["configuration"])
        cache = cache_values(cache_text)
        source = (
            cache["DE_UPSTREAM_SOURCE"]
            if "DE_UPSTREAM_SOURCE" in cache
            else cache["CMAKE_HOME_DIRECTORY"]
        )
        need(
            native_path(source, system).is_absolute(),
            "Actual dependency source root is not absolute",
        )
        settings = dict(features["dependencies"][name])
        if system in {"Darwin", "Linux"}:
            settings.update(features.get("unix_dependencies", {}).get(name, {}))
        for key, expected in settings.items():
            need(key in cache, "Actual dependency feature is unavailable")
            if type(expected) is bool:
                actual_bool = cache[key].upper() not in DISABLED and not cache[key].endswith(
                    "-NOTFOUND"
                )
                need(actual_bool is expected, "Actual dependency feature differs")
            else:
                actual_text = cache[key].replace("\\", "/")
                expected_text = expected.replace(
                    "<BINARY>", cache["CMAKE_CACHEFILE_DIR"].replace("\\", "/")
                )
                need(actual_text == expected_text, "Actual dependency feature value differs")
        need(
            cache.get("BUILD_SHARED_LIBS", "").upper() in DISABLED,
            "Native dependency is not statically configured",
        )
        for key in ("installed", "archives"):
            need(isinstance(item[key], list), "Native dependency file evidence is unavailable")
            for identity in item[key]:
                physical(identity, system)
        need(item["installed"], "Native provider header/adapter identity is absent")
        need(
            all(
                native_path(value["path"], system).suffix.lower() in {".a", ".lib"}
                for value in item["archives"]
            ),
            "Nonstatic native provider archive is unsupported",
        )


def import_scope(body: dict[str, Any]) -> None:
    """Correlate the actual inspected executable with the existing OS import policy."""
    observed = body["imports"]
    exact(
        observed,
        {
            "argv",
            "tool",
            "stdout",
            "stderr",
            "exit_code",
            "timed_out",
            "system_directory",
            "resolved",
        },
    )
    text, errors = text_snapshot(observed["stdout"]), text_snapshot(observed["stderr"])
    need(
        type(observed["exit_code"]) is int
        and observed["exit_code"] == 0
        and observed["timed_out"] is False
        and errors == "",
        "Actual native import inspection did not complete",
    )
    system = object_snapshot(body["configuration"])["CMAKE_SYSTEM_NAME"]
    physical(observed["tool"], system)
    tool, arguments = native_import_command(system)
    need(
        native_path(observed["tool"]["path"], system).stem.lower() == tool
        and observed["argv"] == [observed["tool"]["path"], *arguments, body["executable"]["path"]],
        "Actual import inspection differs from selected executable/tool",
    )
    names = native_import_values(system, text)
    need(isinstance(observed["resolved"], dict), "Native import resolution evidence is unavailable")
    if system != "Windows":
        need(
            observed["system_directory"] is None and not observed["resolved"],
            "POSIX import policy uses actual inspected OS names and the trusted OS boundary",
        )
        return
    need(
        set(observed["resolved"]) == set(names),
        "Native Windows import resolution evidence is incomplete",
    )
    for name, item in observed["resolved"].items():
        physical(item, system)
        directory = native_path(observed["system_directory"], system)
        need(
            directory.is_absolute()
            and directory.name.lower() == "system32"
            and native_path(item["path"], system).parent == directory
            and (
                native_path(item["path"], system).name.lower() == name.lower()
                or windows_api_contract(name)
            ),
            "Native imports are not bound to the selected Windows OS directory/name",
        )
