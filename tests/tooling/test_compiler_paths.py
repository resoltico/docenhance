# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Real compiler observation must preserve path identity and forbidden include detection."""

from __future__ import annotations

import json
import shlex
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import architecture_build
import compile_db
from architecture import ArchitectureError, load_manifest


class CompilerPathTests(unittest.TestCase):
    """Aliases and Make-special characters cannot erase first-party or package ownership."""

    def test_real_include_reach_survives_path_spelling(self) -> None:
        """A real forbidden include is detected through spaces/dollars and traversal aliases."""
        major = json.loads((ROOT / "deps/tools.json").read_text())["clang_tidy"]["version"].split(
            "."
        )[0]
        compiler = (
            shutil.which(f"clang++-{major}") or shutil.which("clang++") or shutil.which("g++")
        )
        self.assertIsNotNone(compiler, "Compiler-path verification requires a native C++ driver")
        manifest = load_manifest()
        with tempfile.TemporaryDirectory(prefix="compiler-path-control-") as directory:
            root = Path(directory) / "source $ space"
            source = root / "src/core/control.cpp"
            source.parent.mkdir(parents=True)
            header = root / "include/docenhance/app/control.hpp"
            header.parent.mkdir(parents=True)
            header.write_text("inline constexpr int borrowed = 7;\n")
            source.write_text(
                "#include <docenhance/app/control.hpp>\nint result() { return borrowed; }\n"
            )
            include = root / "include"
            entry = {
                "file": str(source),
                "directory": str(root),
                "command": shlex.join(
                    [
                        str(compiler),
                        "-std=c++23",
                        "-I" + str(include),
                        "-c",
                        str(source),
                        "-o",
                        "control.o",
                    ]
                ),
            }
            with patch.object(compile_db, "ROOT", root):
                self.assertIsNone(compile_db.layer_of(manifest, str(ROOT / "src/core/memory.cpp")))
                self.assertEqual(
                    compile_db.layer_of(manifest, str(root / "src/core/../core/control.cpp")),
                    "core",
                )
                self.assertIsNone(
                    compile_db.layer_of(manifest, str(root.parent / "foreign/src/core/control.cpp"))
                )
                self.assertIn(str(header.resolve()), compile_db.included_headers(entry))
                failures, reached = architecture_build.reach_violations(manifest, entry, root)
                self.assertIn("app", reached)
                self.assertTrue(any("core reaches app" in error for error in failures))
                prefix = root / "out/build/prefix/include"
                prefix.mkdir(parents=True)
                system_entry = dict(
                    entry,
                    command=entry["command"]
                    + " -isystem "
                    + shlex.quote(str(prefix / "../include")),
                )
                self.assertEqual(
                    compile_db.package_roots(system_entry, root / "out/build/app"),
                    [str(prefix.resolve())],
                )
                header.unlink()
                with self.assertRaises(ArchitectureError):
                    compile_db.included_headers(entry)
