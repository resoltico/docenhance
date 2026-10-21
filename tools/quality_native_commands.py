# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual selected native source, object and transitive link command correspondence."""

from __future__ import annotations

import ntpath
import posixpath
from typing import TYPE_CHECKING, Any

from command_arguments import command_arguments
from quality_native_contract import (
    compile_snapshot,
    instrumented,
    native_path,
    need,
    object_snapshot,
    text_snapshot,
)
from quality_native_headers import header_scope
from quality_provenance_contract import exact

if TYPE_CHECKING:
    from pathlib import PurePosixPath, PureWindowsPath


def _posix_compile_identity(entry: dict[str, str], args: list[str], system: str) -> None:
    """Match the existing ordinary compile source/output operands, without replaying a compiler."""
    directory = native_path(entry["directory"], system)
    need(directory.is_absolute(), "Native compilation directory is not absolute")
    paired = {
        "-o",
        "-I",
        "-isystem",
        "-iquote",
        "-include",
        "-imacros",
        "-isysroot",
        "--sysroot",
        "-target",
        "--target",
        "-arch",
        "-D",
        "-U",
        "-idirafter",
        "-iprefix",
        "-iwithprefix",
        "-iwithprefixbefore",
        "-isystem-after",
        "-MF",
        "-MT",
        "-MQ",
        "-MJ",
        "-x",
        "-Xclang",
        "-Xpreprocessor",
        "-Xassembler",
        "-mllvm",
    }
    inputs, outputs, compile_only, index = [], [], 0, 1
    while index < len(args):
        argument = args[index]
        index += 1
        if argument in paired:
            need(index < len(args), "Native compiler option lacks its actual operand")
            if argument == "-o":
                outputs.append(args[index])
            index += 1
        elif argument == "-c":
            compile_only += 1
        elif argument == "--":
            inputs.extend(args[index:])
            break
        elif not argument.startswith("-"):
            inputs.append(argument)
        else:
            need(
                not argument.startswith(("-o", "--output")),
                "Native output spelling is outside the existing compile/header route",
            )
    need(
        compile_only == 1 and len(inputs) == 1 and len(outputs) == 1,
        "Native command must compile exactly one source to its one declared output",
    )
    source = command_path(entry["file"], directory, system)
    output = command_path(entry["output"], directory, system)
    need(
        command_path(inputs[0], directory, system) == source,
        "Native command compiles another source",
    )
    need(
        command_path(outputs[0], directory, system) == output,
        "Native command writes another output",
    )


def command_scope(body: dict[str, Any], files: set[str]) -> None:
    """Correlate actual selected compilation, header and link records."""
    config = object_snapshot(body["configuration"])
    system = config["CMAKE_SYSTEM_NAME"]
    entries = compile_snapshot(body["compile_commands"])
    need(
        bool(entries),
        "Actual native compilation is absent",
    )
    required = {path for path in files if native_path(path, system).suffix == ".cpp"}
    selected = [entry for entry in entries if entry.get("file") in required]
    need(
        {entry["file"] for entry in selected} == required and len(selected) == len(required),
        "Selected native sources lack unique actual compilation",
    )
    for entry in selected:
        exact(entry, {"directory", "command", "file", "output"})
        need(
            all(isinstance(item, str) and item for item in entry.values()),
            "Native compile record is incomplete",
        )
        need(not instrumented(entry["command"], system), "Actual native command is instrumented")
        args = command_arguments(entry["command"], windows=system == "Windows")
        need(
            args
            and args[0] == config["CMAKE_CXX_COMPILER"]
            and not any(arg.startswith("@") for arg in args),
            "Native compiler/response input is unbound",
        )
        if system != "Windows":
            _posix_compile_identity(entry, args, system)
    link_scope(body, selected, system)

    header_scope(body, selected, files)


def command_path(
    value: str, directory: PurePosixPath | PureWindowsPath, system: str
) -> PurePosixPath | PureWindowsPath:
    """Normalize only actual compiler/link path operands against their build directory."""
    path = native_path(value, system)
    path = path if path.is_absolute() else directory / path
    normalize = ntpath.normpath if system == "Windows" else posixpath.normpath
    return native_path(normalize(str(path)), system)


def link_scope(body: dict[str, Any], selected: list[dict[str, str]], system: str) -> None:
    """Require selected objects and static archives in the actual link transcript."""
    text = text_snapshot(body["link_commands"])
    need(
        text and not instrumented(text, system),
        "Actual native link commands are absent/instrumented",
    )
    args = [
        arg
        for line in text.splitlines()
        for arg in command_arguments(line, windows=system == "Windows")
    ]
    need(
        not any(arg.startswith("@") for arg in args),
        "Native link response inputs are not expanded/bound",
    )
    directories = {entry["directory"] for entry in selected}
    need(
        len(directories) == 1, "Selected native commands do not share their actual build directory"
    )
    directory = native_path(next(iter(directories)), system)
    need(directory.is_absolute(), "Native build directory is not absolute")
    paths = {command_path(arg, directory, system) for arg in args if arg}
    outputs = [args[index + 1] for index, arg in enumerate(args[:-1]) if arg == "-o"]
    outputs.extend(arg[5:] for arg in args if arg.lower().startswith("/out:"))
    output_paths = {command_path(arg, directory, system) for arg in outputs}
    need(
        native_path(body["executable"]["path"], system) in output_paths,
        "Actual link transcript does not produce the selected CLI output",
    )
    for entry in selected:
        output = command_path(entry["output"], directory, system)
        need(output in paths, "Actual transitive link transcript omits selected native objects")
    for dependency in body["dependencies"]:
        for archive in dependency["archives"]:
            need(
                native_path(archive["path"], system) in paths,
                "Actual link transcript omits a selected static dependency",
            )
