# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real compiler observation must preserve path identity and forbidden include detection."""

from __future__ import annotations

import hashlib
import json
import os
import shlex
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

from compiler_probes import available_compiler, compiler_probe

import architecture_build
import compile_db
from architecture import ArchitectureError, load_manifest


class CompilerPathTests(unittest.TestCase):
    """Aliases and Make-special characters cannot erase first-party or package ownership."""

    def test_consumed_gnu_pch_trace_is_opaque_and_refused(self) -> None:
        """A real GNU trace marker cannot erase unobserved transitive package dependencies."""
        with tempfile.TemporaryDirectory(prefix="compiler-pch-control-") as directory:
            root = Path(directory)
            pch = root / "generated $ space/bridge.h.gch"
            pch.parent.mkdir()
            pch.write_bytes(b"opaque precompiled input")
            with self.assertRaises(ArchitectureError) as raised:
                compile_db.header_trace(f"! {pch}\n", directory, "control.cpp", "GNU")
            self.assertIn("consumed precompiled header", str(raised.exception))
            self.assertIn("control.cpp", str(raised.exception))
            self.assertIn(str(pch.resolve()), str(raised.exception))

    def test_real_include_reach_survives_path_spelling(self) -> None:
        """A real forbidden include is detected through spaces/dollars and traversal aliases."""
        compiler, compiler_id = available_compiler()
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
                "compiler_id": compiler_id,
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
                self.assertIn(
                    str(header.resolve()),
                    [resolved for _, resolved in compile_db.included_headers(entry)],
                )
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


class HeaderTransportTests(unittest.TestCase):
    """Compiler transport decoding never substitutes another literal filesystem identity."""

    def test_clang_decodes_once_and_gnu_keeps_literal_names(self) -> None:
        """Fixed mixed paths and literal escapes have distinct explicit emitter semantics."""
        pairs = (
            (r"C:/root/space $[x]\\probe.hpp", r"C:/root/space $[x]\probe.hpp"),
            (r"/case/literal\\n.hpp", r"/case/literal\n.hpp"),
            (r"/case/\"quote\".hpp", '/case/"quote".hpp'),
            ("/ré/あ.hpp", "/ré/あ.hpp"),
        )
        for encoded, literal in pairs:
            for kind in ("Clang", "AppleClang"):
                with self.subTest(encoded=encoded, kind=kind):
                    self.assertEqual(
                        compile_db.header_trace_values(". " + encoded + "\n", kind),
                        (None, [literal]),
                    )
            self.assertEqual(
                compile_db.header_trace_values(". " + encoded + "\n", "GNU"),
                (None, [encoded]),
            )
        for malformed in (
            r"/case/line\n.hpp",
            r"/case/tab\t.hpp",
            r"/case/\u0061.hpp",
            '/case/quote".hpp',
            "/case/end\\",
        ):
            with self.subTest(malformed=malformed), self.assertRaises(ArchitectureError):
                compile_db.header_trace_values(". " + malformed + "\n", "Clang")
        for unknown in ("", "MSVC", "unknown"):
            with self.subTest(kind=unknown), self.assertRaises(ArchitectureError):
                compile_db.header_trace_values(". ordinary.hpp\n", unknown)

    def test_raw_and_escaped_posix_names_remain_distinguishable(self) -> None:
        """Even when both potential names exist, compiler kind alone selects exact bytes."""
        one, two = r"literal\n.hpp", r"literal\\n.hpp"
        encoded = ". " + two + "\n"
        self.assertEqual(compile_db.header_trace_values(encoded, "Clang")[1], [one])
        self.assertEqual(compile_db.header_trace_values(encoded, "GNU")[1], [two])
        if os.name == "posix":
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root / one).write_text("// one literal backslash\n", encoding="utf-8")
                (root / two).write_text("// two literal backslashes\n", encoding="utf-8")
                (root / 'quote".hpp').write_text("// literal quote\n", encoding="utf-8")
                self.assertEqual(
                    compile_db.header_trace(encoded, temporary, "probe.cpp", "Clang")[0][1],
                    str((root / one).resolve()),
                )
                self.assertEqual(
                    compile_db.header_trace(encoded, temporary, "probe.cpp", "GNU")[0][1],
                    str((root / two).resolve()),
                )
                self.assertEqual(
                    compile_db.header_trace(
                        r". quote\".hpp" + "\n", temporary, "probe.cpp", "Clang"
                    )[0][1],
                    str((root / 'quote".hpp').resolve()),
                )
                entry = compiler_probe(root, root / "probe.cpp", '#include "' + one + '"\n')
                observed = compile_db.included_headers(entry)
                self.assertEqual(
                    [item for item in observed if Path(item[1]).parent == root.resolve()],
                    [(str((root / one).resolve()), str((root / one).resolve()))],
                )

    def test_configured_cdb_identity_is_local_exact_and_driver_bound(self) -> None:
        """Parent identity, wrong driver/hash or unsupported kind cannot describe this CDB."""
        compiler, kind = available_compiler()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            build.mkdir()
            entry = {
                "directory": temporary,
                "file": str(root / "probe.cpp"),
                "command": shlex.join([compiler, "-c", str(root / "probe.cpp")]),
                "compiler_id": "untrusted CDB metadata",
            }
            database = build / "compile_commands.json"
            database.write_text(json.dumps([entry]), encoding="utf-8")
            identity = {
                "CMAKE_CXX_COMPILER": str(Path(compiler).resolve()),
                "CMAKE_CXX_COMPILER_ID": kind,
                "CXX_compiler_sha256": hashlib.sha256(Path(compiler).read_bytes()).hexdigest(),
            }
            (root / "build-identity.json").write_text(json.dumps(identity), encoding="utf-8")
            with self.assertRaises(FileNotFoundError):
                compile_db.compiler_entries(build)
            owner = build / "build-identity.json"
            owner.write_text(json.dumps(identity), encoding="utf-8")
            self.assertEqual(compile_db.compiler_entries(build)[0]["compiler_id"], kind)
            for field, value in (
                ("CXX_compiler_sha256", "0" * 64),
                ("CMAKE_CXX_COMPILER_ID", "MSVC"),
            ):
                with self.subTest(field=field):
                    owner.write_text(json.dumps({**identity, field: value}), encoding="utf-8")
                    with self.assertRaises(ArchitectureError):
                        compile_db.compiler_entries(build)
            owner.write_text(json.dumps(identity), encoding="utf-8")
            foreign = root / "different-driver"
            foreign.write_bytes(b"different executable identity")
            database.write_text(
                json.dumps([{**entry, "command": shlex.join([str(foreign), "-c", entry["file"]])}]),
                encoding="utf-8",
            )
            with self.assertRaises(ArchitectureError):
                compile_db.compiler_entries(build)
