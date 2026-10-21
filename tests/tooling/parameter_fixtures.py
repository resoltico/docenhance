# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Small independent parameter authorities and real generated-header compiler fixtures."""

from __future__ import annotations

import platform
import subprocess
from pathlib import Path
from typing import Any

from tools_path import ROOT

from compiler_probes import fixture_compiler

import architecture_api


def contracts() -> tuple[dict[str, Any], dict[str, Any]]:
    """Small independent schema3/13 fixture, including conditional required raw presence."""
    cli = {
        "schema_version": 13,
        "formats": [{"format": "png", "modes": ["preserve", "gray"]}],
        "commands": {
            "root": {"usage": "docenhance", "operands": 0},
            "process": {"usage": "docenhance process INPUT", "operands": 1},
            "methods": {"usage": "docenhance methods [ID]", "operands": [0, 1]},
        },
        "options": [
            {
                "name": "--json",
                "kind": "flag",
                "metavar": "",
                "scope": ["root", "process"],
                "group": "Presentation",
                "summary": "JSON",
            }
        ],
    }
    methods = {
        "schema_version": 3,
        "families": [
            {
                "family": "restoration",
                "order": 1,
                "group": "Restoration",
                "applies_to": ["preserve", "gray"],
                "selector": {"option": "--deblur", "metavar": "METHOD", "default": "off"},
                "shared": [],
                "methods": [
                    {
                        "id": "R01",
                        "version": 1,
                        "selectors": {"wiener": {}},
                        "parameters": [
                            {
                                "option": "--psf",
                                "field": "psf",
                                "kind": "choice",
                                "metavar": "KIND",
                                "values": ["gaussian", "kernel"],
                                "required": True,
                                "summary": "PSF",
                            },
                            {
                                "option": "--psf-file",
                                "field": "kernel",
                                "kind": "path",
                                "metavar": "PATH",
                                "required": True,
                                "when": {"--psf": "kernel"},
                                "summary": "Kernel",
                            },
                        ],
                    }
                ],
            }
        ],
    }
    return cli, methods


def numeric(**updates: object) -> dict[str, Any]:
    """An ordinary bounded decimal, changed by one independent counterexample at a time."""
    return {
        "option": "--value",
        "kind": "decimal",
        "metavar": "N",
        "min": "1/255",
        "max": "1",
        "default": "0.5",
        **updates,
    }


def compile_and_run(
    root: Path, source: str, *, sources: tuple[Path, ...] = ()
) -> subprocess.CompletedProcess[str]:
    """Compile with the declared native compiler role and execute the generated value controls."""
    cpp = root / "probe.cpp"
    cpp.write_text(source, encoding="utf-8")
    query = Path(architecture_api.find_clang_query()).resolve()
    compiler = fixture_compiler(platform.system(), query)
    binary = root / ("probe.exe" if platform.system() == "Windows" else "probe")
    command = [
        str(compiler),
        "-std=c++23",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I" + str(root / "include"),
        "-I" + str(ROOT / "include"),
        str(cpp),
        *(str(path) for path in sources),
        "-o",
        str(binary),
    ]
    try:
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=30)
        result = subprocess.run(
            [str(binary)], check=True, capture_output=True, text=True, timeout=10
        )
    except subprocess.CalledProcessError as error:
        if error.stdout:
            error.add_note(f"Controlled parameter fixture stdout:\n{error.stdout}")
        if error.stderr:
            error.add_note(f"Controlled parameter fixture stderr:\n{error.stderr}")
        raise
    return result
