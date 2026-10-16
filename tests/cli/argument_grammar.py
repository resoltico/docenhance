# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent owned-grammar oracle for executable argv tests and saved harness findings."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from collections.abc import Iterator, Mapping, Sequence


CLI_EDITION = 13
METHOD_EDITION = 3


@dataclass(frozen=True)
class OptionSyntax:
    """Only syntax facts from the reviewed authorities, without generated parser code."""

    flag: bool
    commands: frozenset[str]


@dataclass(frozen=True)
class TokenRoles:
    """Syntax and transport prediction; method/path admission belongs to the application."""

    accepted: bool
    command: str
    json: bool
    operands: tuple[str, ...]
    values: Mapping[str, str]
    error: str = ""


def reviewed_syntax(
    cli: dict[str, Any], methods: dict[str, Any]
) -> tuple[dict[str, OptionSyntax], dict[str, int]]:
    """Read syntax facts directly from authored JSON, without the production generation merger."""
    if cli["schema_version"] != CLI_EDITION or methods["schema_version"] != METHOD_EDITION:
        message = "The grammar oracle requires the current authoring editions"
        raise ValueError(message)
    options = {
        option["name"]: OptionSyntax(
            flag=option["kind"] == "flag", commands=frozenset(option["scope"])
        )
        for option in cli["options"]
    }
    process = frozenset({"process"})
    for family in methods["families"]:
        options[family["selector"]["option"]] = OptionSyntax(flag=False, commands=process)
        parameters = list(family["shared"])
        for method in family["methods"]:
            parameters.extend(method["parameters"])
        for parameter in parameters:
            options[parameter["option"]] = OptionSyntax(flag=False, commands=process)
    maximum = {}
    for name, command in cli["commands"].items():
        bounds = command["operands"]
        maximum[name] = bounds if type(bounds) is int else bounds[-1]
    return options, maximum


def syntax_json(arguments: Sequence[str]) -> bool:
    """Exact raw tokens before the first delimiter select presentation only on syntax failure."""
    for argument in arguments:
        if argument == "--":
            return False
        if argument == "--json":
            return True
    return False


def analyze(
    arguments: Sequence[str],
    options: Mapping[str, OptionSyntax],
    maximum_operands: Mapping[str, int],
) -> TokenRoles:
    """Predict syntax roles independently; missing required operands are app admission failures."""
    command = "root"
    operands: list[str] = []
    values: dict[str, str] = {}

    def refused(reason: str) -> TokenRoles:
        return TokenRoles(
            accepted=False,
            command=command,
            json=syntax_json(arguments),
            operands=tuple(operands),
            values=values,
            error=reason,
        )

    if not valid_arguments_utf8(arguments):
        return refused("utf8")
    offset = 0
    if arguments and arguments[0] in maximum_operands and arguments[0] != "root":
        command = arguments[0]
        offset = 1
    tokens = iter(arguments[offset:])
    ended = False
    try:
        for argument in tokens:
            if argument == "--" and not ended:
                ended = True
                continue
            if ended or not argument.startswith("--"):
                operands.append(argument)
                if len(operands) > maximum_operands[command]:
                    return refused("extra operand")
                continue
            name, value = read_option(argument, tokens, command, options, values)
            values[name] = value
    except GrammarError as error:
        return refused(str(error))
    if command == "root" and "--version" in values and len(values) != 1:
        return refused("version with root flags")
    return TokenRoles(
        accepted=True,
        command=command,
        json="--json" in values,
        operands=tuple(operands),
        values=values,
    )


class GrammarError(ValueError):
    """A syntax refusal distinct from application domain admission."""


def read_option(
    argument: str,
    remaining: Iterator[str],
    command: str,
    options: Mapping[str, OptionSyntax],
    seen: Mapping[str, str],
) -> tuple[str, str]:
    """Consume exactly the role assigned by one declared long option."""
    name, separator, joined = argument.partition("=")
    option = options.get(name)
    if option is None or command not in option.commands:
        message = "unknown option"
        raise GrammarError(message)
    if name in seen:
        message = "repeated option"
        raise GrammarError(message)
    if option.flag:
        if separator:
            message = "flag with value"
            raise GrammarError(message)
        return name, ""
    if separator:
        return name, joined
    following = next(remaining, None)
    if following is None:
        message = "missing value"
        raise GrammarError(message)
    return name, following


def valid_arguments_utf8(arguments: Sequence[str]) -> bool:
    """Refuse surrogate-escaped or malformed argv text without repairing identity bytes."""
    try:
        for argument in arguments:
            argument.encode("utf-8", errors="strict")
    except UnicodeEncodeError:
        return False
    return True
