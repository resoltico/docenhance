# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Closed selected native evidence body and actual producer response relationships."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

from quality_attempt import MAX_RESPONSE_BYTES, delivered_path
from quality_commands import command_options
from quality_json import canonical
from quality_native_commands import command_scope
from quality_native_components import dependency_scope, import_scope, native_graph
from quality_native_contract import (
    compile_snapshot,
    configuration_values,
    native_path,
    need,
    object_snapshot,
    physical,
)
from quality_native_inventory import JSON_SNAPSHOTS, _physical_files, _snapshots, _source_files
from quality_provenance_contract import build_facts, exact, reference_shape, sha256_text

SOURCE_COMMIT_HEX_LENGTH = 40
MINIMUM_PROCESS_ARGUMENTS = 6

NATIVE_KEYS = {
    "executable",
    "build",
    "source_epoch",
    "generated",
    "configuration",
    "targets",
    "architecture",
    "compile_commands",
    "link_commands",
    "dependency_lock",
    "dependency_features",
    "dependencies",
    "headers",
    "toolchain",
    "imports",
    "producer",
    "wire_schema",
}


def _local_schema(value: object) -> None:
    pending = [value]
    while pending:
        item = pending.pop()
        if isinstance(item, dict):
            for key in ("$ref", "$dynamicRef"):
                if key in item:
                    need(
                        isinstance(item[key], str) and item[key].startswith("#"),
                        "Native wire metadata cannot introduce external resolver effects",
                    )
            pending.extend(item.values())
        elif isinstance(item, list):
            pending.extend(item)


def validate_native_body(body: object) -> None:
    """Offline closed facts; this never claims an old native process ran again."""
    body = exact(body, NATIVE_KEYS)
    for key in JSON_SNAPSHOTS:
        if key == "compile_commands":
            compile_snapshot(body[key])
        else:
            object_snapshot(body[key])
    config = object_snapshot(body["configuration"])
    configuration_values(config)
    system = config["CMAKE_SYSTEM_NAME"]
    physical(body["executable"], system)
    build_facts(body["build"])
    source = object_snapshot(body["source_epoch"])
    exact(source, {"head", "files", "source_map_sha256"})
    need(
        isinstance(source["head"], str)
        and len(source["head"]) == SOURCE_COMMIT_HEX_LENGTH
        and all(char in "0123456789abcdef" for char in source["head"]),
        "Native source commit is unavailable",
    )
    sha256_text(source["source_map_sha256"])
    need(isinstance(source["files"], dict) and source["files"], "Actual dirty source map is absent")
    for name, digest in source["files"].items():
        path = native_path(name, "Linux")
        need(
            not path.is_absolute() and ".." not in path.parts,
            "Native source-map path escapes its owner",
        )
        sha256_text(digest)
    need(
        isinstance(body["generated"], list) and body["generated"],
        "Actual generated inputs are absent",
    )
    for item in body["generated"]:
        physical(item, system)
    exact(body["toolchain"], {"C", "CXX", "cmake", "make"})
    for item in body["toolchain"].values():
        physical(item, system)
    for language in ("C", "CXX"):
        item = body["toolchain"][language]
        need(
            item["path"] == config["CMAKE_" + language + "_COMPILER"]
            and item["sha256"] == config[language + "_compiler_sha256"],
            "Actual compiler identity differs",
        )
    for name, key in (("cmake", "CMAKE_COMMAND_sha256"), ("make", "CMAKE_MAKE_PROGRAM_sha256")):
        need(body["toolchain"][name]["sha256"] == config[key], "Native configuration tool differs")
    build = body["build"]
    need(
        build["platform"] == config["CMAKE_SYSTEM_NAME"] + "-" + config["CMAKE_SYSTEM_PROCESSOR"]
        and build["compiler"]
        == config["CMAKE_CXX_COMPILER_ID"] + " " + config["CMAKE_CXX_COMPILER_VERSION"],
        "Native BuildFacts and actual toolchain/platform disagree",
    )
    files, packages = native_graph(body)
    dependency_scope(body, packages)
    command_scope(body, files)
    import_scope(body)
    _producer_shape(body, system)
    _source_files(body)  # Validate every derived physical source/SDK reference offline too.


def _producer_shape(body: dict[str, Any], system: str) -> None:
    producer = body["producer"]
    exact(producer, {"executable", "source_epoch_sha256", "source", "raw_bytes", "execution"})
    for key in ("executable", "source"):
        physical(producer[key], system)
    need(
        canonical(producer["executable"]) == canonical(body["executable"])
        and producer["source_epoch_sha256"] == body["source_epoch"]["reference"]["sha256"],
        "CLI producer differs from the selected body/source epoch",
    )
    exact(producer["raw_bytes"], {"stdout", "stderr"})
    count = producer["raw_bytes"]["stdout"]
    need(
        type(count) is int
        and 0 < count <= MAX_RESPONSE_BYTES
        and type(producer["raw_bytes"]["stderr"]) is int
        and producer["raw_bytes"]["stderr"] == 0,
        "CLI producer needs complete bounded stdout and empty stderr",
    )
    execution = producer["execution"]
    exact(
        execution, {"argv", "stdout", "stderr", "exit_code", "timed_out", "response", "publication"}
    )
    for stream in ("stdout", "stderr"):
        reference_shape(execution[stream])
    argv = execution["argv"]
    need(
        isinstance(argv, list)
        and all(isinstance(item, str) and item and "\0" not in item for item in argv),
        "Actual producer argv is incomplete",
    )
    for item in argv:
        item.encode("utf-8", "strict")
    need(
        len(argv) >= MINIMUM_PROCESS_ARGUMENTS
        and argv[0] == body["executable"]["path"]
        and argv[1] == "process"
        and argv[-1] == "--json"
        and execution["exit_code"] == 0
        and type(execution["exit_code"]) is int
        and execution["timed_out"] is False
        and execution["publication"] == "completed",
        "Selected CLI producer did not report successful complete processing",
    )
    response = execution["response"]
    schema = object_snapshot(body["wire_schema"])
    _local_schema(schema)
    Draft202012Validator(schema).validate(response)
    destination, _ = command_options(argv, argv[2], Path(argv[0]))
    need(
        response["command"] == "process"
        and response["exit_code"] == 0
        and response["publication"] == "completed"
        and response["output"] == delivered_path(destination, "result.png")
        and canonical(response["record"]["build"]) == canonical(body["build"]),
        "Actual producer response differs from its command/build",
    )
    identity = producer["source"]
    need(
        response["record"]["source"]["sha256"] == identity["sha256"]
        and response["record"]["source"]["bytes"] == identity["bytes"],
        "Producer consumed a different source identity",
    )


def nested_references(body: dict[str, Any]) -> list[dict[str, str]]:
    """List actual corpus-confined metadata/raw Ref2 without opening them."""
    validate_native_body(body)
    refs = [body["source_epoch"]["reference"], *(item["reference"] for item in _snapshots(body))]
    refs.extend(body["producer"]["execution"][name] for name in ("stdout", "stderr"))
    return refs


def physical_references(body: dict[str, Any]) -> list[dict[str, str]]:
    """Inventory absolute trusted implementation files separately from corpus-confined Ref2."""
    validate_native_body(body)
    return [
        {key: item[key] for key in ("path", "sha256")} for item in _physical_files(body)
    ] + _source_files(body)
