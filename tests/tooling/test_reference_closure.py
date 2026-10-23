# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real compiler controls for independent reference ownership, linking and assertion work."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from functools import partial
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import check_reference_suite
import reference_closure
from architecture import ArchitectureError, Manifest
from compile_db import header_trace

SUITES = (
    "parser_cases",
    "decimal_cases",
    "numerical_cases",
    "capabilities_cases",
    "memory_cases",
    "architecture_cases",
    "schedule_cases",
    "box_mean_cases",
    "sharpening_cases",
    "otsu_cases",
)
SUCCESS_LINE = "PASS: parser, numerical and capability reference suites"
REFUSALS = (RuntimeError, ArchitectureError)


class ReferenceClosureTests(unittest.TestCase):
    """Package-free observation cannot replace actual callbacks or complete object links."""

    def compiler(self) -> str:
        """Require an actual available GNU-style C++ driver rather than skipping the proof."""
        major = json.loads((ROOT / "deps/tools.json").read_text())["clang_tidy"]["version"]
        compiler = (
            shutil.which(f"clang++-{major.split('.')[0]}")
            or shutil.which("clang++")
            or shutil.which("g++")
        )
        if compiler is None:
            self.fail("Reference closure controls require a native C++ driver")
        return compiler

    @staticmethod
    def observed_system_roots(trace: str, directory: Path, source: Path) -> list[Path]:
        """Admit only compiler/SDK roots classified by the production policy."""
        return reference_closure.system_roots(
            [canonical for _, canonical in header_trace(trace, str(directory), str(source))]
        )

    def test_transitive_system_package_is_rejected_after_real_compilation(self) -> None:
        """A successful -isystem compile must not launder a transitive registered header."""
        with tempfile.TemporaryDirectory(prefix="reference-system-") as temp:
            directory = Path(temp)
            system = directory / "fake system $ include"
            (system / "nlohmann").mkdir(parents=True)
            (system / "nlohmann/json.hpp").write_text("inline constexpr int native_value = 7;\n")
            (system / "bridge.hpp").write_text("#include <nlohmann/json.hpp>\n")
            source = directory / "control.cpp"
            source.write_text("#include <bridge.hpp>\nint value() { return native_value; }\n")
            result = subprocess.run(
                [
                    self.compiler(),
                    "-std=c++23",
                    "-H",
                    "-isystem",
                    str(system),
                    "-c",
                    str(source),
                    "-o",
                    str(directory / "control.o"),
                ],
                capture_output=True,
                text=True,
                check=True,
            )
            self.assertTrue((directory / "control.o").is_file())
            self.assertIn("nlohmann", result.stderr)
            roots = self.observed_system_roots(result.stderr, directory, source)
            with self.assertRaisesRegex(REFUSALS, "nlohmann"):
                reference_closure.observe_headers(
                    result.stderr, directory, source, [directory, system, *roots]
                )

    def test_real_owned_header_trace_is_accepted(self) -> None:
        """The observer preserves ordinary owned headers with Make-special path bytes."""
        with tempfile.TemporaryDirectory(prefix="reference-owned-") as temp:
            directory = Path(temp)
            includes = directory / "owned $ space"
            includes.mkdir()
            (includes / "owned.hpp").write_text("inline constexpr int own_value = 7;\n")
            source = directory / "control.cpp"
            source.write_text('#include "owned.hpp"\nint value() { return own_value; }\n')
            result = subprocess.run(
                [
                    self.compiler(),
                    "-std=c++23",
                    "-H",
                    "-I",
                    str(includes),
                    "-c",
                    str(source),
                    "-o",
                    str(directory / "control.o"),
                ],
                capture_output=True,
                text=True,
                check=True,
            )
            roots = self.observed_system_roots(result.stderr, directory, source)
            reference_closure.observe_headers(result.stderr, directory, source, [directory, *roots])

    def test_closed_flags_reject_extra_link_inputs_and_discard(self) -> None:
        """No extra archive/object/library, discarded undefined code or macro payload enters."""
        reference_closure.validate_flags(
            ["-std=c++23", "-Werror", "-fno-fast-math", "-ffp-contract=off"]
        )
        for flags in (
            ["-lforeign"],
            ["-L", "/foreign"],
            ["foreign.a"],
            ["extra.o"],
            ["-Wl,--gc-sections"],
            ["-Wl,-dead_strip"],
            ["-flto"],
            ["-Xlinker", "--gc-sections"],
            ["@response"],
            ["-DANSWER=42"],
        ):
            with self.subTest(flags=flags), self.assertRaises(REFUSALS):
                reference_closure.validate_flags(flags)

    def test_strict_warnings_match_production_cmake(self) -> None:
        """Standalone compilation retains the production strict warning list exactly."""
        cmake = (ROOT / "cmake/ProjectOptions.cmake").read_text()
        warnings = cmake.split("set(DE_STRICT_WARNINGS", 1)[1].split(")", 1)[0].split()
        self.assertEqual(reference_closure.STRICT_WARNINGS, warnings)

    def test_toolchain_roots_include_resources_without_admitting_package_prefixes(self) -> None:
        """Clang sanitizer resources share a compiler root; ambient include prefixes do not."""
        paths = [
            "/opt/compiler/lib/clang/23/include",
            "/usr/local/include",
            "/opt/homebrew/include",
            "/foreign/VC/Tools/MSVC/14.51/include",
            "/foreign/Windows Kits/10/Include/10.0.26100.0/ucrt",
        ]
        roots = reference_closure.system_roots(paths)
        self.assertEqual(roots, [Path("/opt/compiler/lib/clang/23").resolve()])

    def test_windows_roots_require_selected_compiler_and_sdk_provenance(self) -> None:
        """A Windows-looking ambient path cannot become a selected compiler/SDK owner."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            vc, sdk = root / "toolset", root / "sdk"
            compiler = vc / "bin/Hostx64/x64/cl.exe"
            compiler.parent.mkdir(parents=True)
            compiler.write_bytes(b"tool identity fixture only")
            include = vc / "include"
            include.mkdir()
            ucrt = sdk / "Include/10.0.26100.0/ucrt"
            ucrt.mkdir(parents=True)
            environment = {
                "VCTOOLSINSTALLDIR": str(vc),
                "WINDOWSSDKDIR": str(sdk),
                "WINDOWSSDKVERSION": "10.0.26100.0\\",
                "INCLUDE": str(root / "foreign/Windows Kits/10/Include/10.0.26100.0/ucrt"),
            }
            paths = [str(include / "expected"), str(ucrt / "stdio.h"), environment["INCLUDE"]]
            with (
                patch("reference_closure.sys.platform", "win32"),
                patch.dict(os.environ, environment),
                patch("reference_closure.shutil.which", return_value=str(compiler)),
            ):
                self.assertEqual(
                    set(reference_closure.system_roots(paths)), {include.resolve(), ucrt.resolve()}
                )
                with (
                    patch(
                        "reference_closure.shutil.which", return_value=str(root / "foreign-cl.exe")
                    ),
                    self.assertRaises(ArchitectureError),
                ):
                    reference_closure.system_roots(paths)
                with (
                    patch.dict(os.environ, {"WINDOWSSDKVERSION": "../foreign"}),
                    self.assertRaises(ArchitectureError),
                ):
                    reference_closure.system_roots(paths)

    def test_real_environment_owner_rejects_injected_flags_and_search(self) -> None:
        """Run the actual production CMake refusal without scrubbing injected inputs."""
        for variable in ("CXXFLAGS", "CPATH"):
            with self.subTest(variable=variable), patch.dict(os.environ, {variable: "foreign"}):
                with (
                    patch.object(
                        subprocess, "run", partial(subprocess.run, capture_output=True, text=True)
                    ),
                    self.assertRaises(subprocess.CalledProcessError) as raised,
                ):
                    check_reference_suite.check_environment()
                self.assertIn(f"{variable} is unsupported", raised.exception.stderr)

    def test_transitive_package_graph_cannot_drop_app_owners(self) -> None:
        """Native reach through another layer cannot remove parser/admission/dispatch owners."""
        with patch.object(Manifest, "package_reach", return_value={"nlohmann_json"}):
            sources = check_reference_suite.reference_sources()
        for name in ("values", "process", "dispatch"):
            self.assertIn(f"src/app/{name}.cpp", sources)

    def test_only_native_restoration_orchestration_leaves_pure_partition(self) -> None:
        """The exact foreign-call owner leaves; every actual pure restoration owner stays."""
        sources = check_reference_suite.reference_sources()
        excluded = "src/methods/restoration_working.cpp"
        self.assertNotIn(excluded, sources)
        owners = {
            path.relative_to(ROOT).as_posix()
            for path in (ROOT / "src/methods").glob("restoration*.cpp")
        }
        self.assertEqual(owners - set(sources), {excluded})
        inventory = [ROOT / source for source in sources]
        inventory.append(ROOT / "tests/support/standalone_main.cpp")
        check_reference_suite.validate_source_inventory(inventory)
        with self.assertRaises(REFUSALS):
            check_reference_suite.validate_source_inventory([*inventory, ROOT / excluded])

    def test_missing_or_duplicated_owner_is_refused_before_compilation(self) -> None:
        """The production source census cannot quietly lose one uncalled implementation."""
        sources = [ROOT / source for source in check_reference_suite.reference_sources()]
        sources.append(ROOT / "tests/support/standalone_main.cpp")
        check_reference_suite.validate_source_inventory(sources)
        owner = ROOT / "src/app/values.cpp"
        self.assertIn(owner, sources)
        for changed in ([source for source in sources if source != owner], [*sources, owner]):
            with self.subTest(count=len(changed)), self.assertRaises(REFUSALS):
                check_reference_suite.validate_source_inventory(changed)

    def test_uncalled_unresolved_owner_fails_real_direct_link(self) -> None:
        """An unused function's native dependency still fails the fresh complete-object link."""
        with tempfile.TemporaryDirectory(prefix="reference-link-") as temp:
            directory = Path(temp)
            source = directory / "unresolved.cpp"
            source.write_text(
                "extern int missing_native_dependency();\n"
                "int uncalled_owner() { return missing_native_dependency(); }\n"
                "int main() { return 0; }\n"
            )
            compiler = self.compiler()
            probe = subprocess.run(
                [compiler, "-std=c++23", "-H", "-fsyntax-only", str(source)],
                capture_output=True,
                text=True,
                check=True,
            )
            roots = self.observed_system_roots(probe.stderr, directory, source)
            (directory / "toolchain-roots.txt").write_text(";".join(map(str, roots)))
            with (
                patch.object(
                    subprocess, "run", partial(subprocess.run, capture_output=True, text=True)
                ),
                self.assertRaises(subprocess.CalledProcessError) as raised,
            ):
                check_reference_suite.build_reference(
                    compiler, ["-std=c++23"], [source], directory, 1
                )
            # A compiled object proves refusal came from the actual final link, not an observer.
            self.assertTrue((directory / "reference-0.o").is_file())
            self.assertNotIn("-c", raised.exception.cmd)
            self.assertIn(
                "missing_native_dependency",
                (raised.exception.stdout or "") + (raised.exception.stderr or ""),
            )

    def test_suite_receipts_require_exact_positive_closed_census(self) -> None:
        """Missing, duplicate, unknown, unordered and noninteger work cannot count as execution."""
        lines = [f"REFERENCE {name} 1" for name in SUITES]
        healthy = "\n".join([*lines, SUCCESS_LINE]) + "\n"
        self.assertEqual(reference_closure.reconcile_suites(healthy), dict.fromkeys(SUITES, 1))
        invalid = [
            "\n".join(lines[:-1]),
            "\n".join([*lines, lines[0]]),
            "\n".join(reversed(lines)),
            healthy.replace("parser_cases", "unknown_cases"),
        ]
        invalid.extend(
            healthy.replace("parser_cases 1", f"parser_cases {count}")
            for count in ("0", "-1", "1.0", "true", "1 extra")
        )
        for output in invalid:
            with self.subTest(output=output), self.assertRaises(REFUSALS):
                reference_closure.reconcile_suites(output)

    def test_actual_runner_requires_live_assertions_and_preserves_failure(self) -> None:
        """Exact runner bytes with real require reject an empty or failing callback."""
        headers = {
            "reference_cases.hpp": SUITES[:4],
            "pipeline_cases.hpp": ("memory_cases", "schedule_cases", "box_mean_cases"),
            "architecture_cases.hpp": ("architecture_cases",),
            "sharpening_cases.hpp": ("sharpening_cases",),
            "otsu_cases.hpp": ("otsu_cases",),
        }
        compiler = self.compiler()
        for mode in ("healthy", "empty", "failed"):
            with (
                self.subTest(mode=mode),
                tempfile.TemporaryDirectory(prefix="reference-runner-") as temp,
            ):
                directory = Path(temp)
                runner = directory / "standalone_main.cpp"
                runner.write_bytes((ROOT / "tests/support/standalone_main.cpp").read_bytes())
                shutil.copyfile(ROOT / "tests/support/require.hpp", directory / "require.hpp")
                for header, suites in headers.items():
                    bodies = []
                    for suite in suites:
                        body = 'require(true, "real assertion");'
                        if suite == "memory_cases" and mode == "empty":
                            body = ""
                        elif suite == "memory_cases" and mode == "failed":
                            body = 'require(false, "fixture assertion failed");'
                        bodies.append(f"inline void {suite}() {{ {body} }}\n")
                    (directory / header).write_text(
                        '#pragma once\n#include "require.hpp"\nnamespace docenhance::tests {\n'
                        + "".join(bodies)
                        + "}\n"
                    )
                executable = directory / ("runner.exe" if sys.platform == "win32" else "runner")
                subprocess.run(
                    [compiler, "-std=c++23", str(runner), "-o", str(executable)],
                    capture_output=True,
                    text=True,
                    check=True,
                )
                result = subprocess.run(
                    [str(executable)], capture_output=True, text=True, check=False
                )
                if mode == "healthy":
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(
                        reference_closure.reconcile_suites(result.stdout), dict.fromkeys(SUITES, 1)
                    )
                    self.assertEqual(result.stdout.splitlines()[-1], SUCCESS_LINE)
                else:
                    self.assertEqual(result.returncode, 1)
                    self.assertNotIn(SUCCESS_LINE, result.stdout)
                    self.assertNotIn("REFERENCE memory_cases", result.stdout)
                    diagnostic = "no assertions" if mode == "empty" else "fixture assertion failed"
                    self.assertIn(diagnostic, result.stderr)

    def test_actual_private_method_header_resolves_without_expanding_admission(self) -> None:
        """The standalone owner finds the same private method header as its real CMake target."""
        with tempfile.TemporaryDirectory(prefix="reference-private-") as temporary:
            directory = Path(temporary)
            source = directory / "private.cpp"
            source.write_text(
                '#include "morphology_working_detail.hpp"\n'
                "int main() { return docenhance::methods::morphology_bands(65) != 64; }\n"
            )
            compiler = self.compiler()
            minimum = json.loads((ROOT / "deps/tools.json").read_text())["cmake"]["minimum"]
            check_reference_suite.generate_build_facts(directory, compiler, minimum)
            executable = check_reference_suite.build_reference(
                compiler, ["-std=c++23"], [source], directory, 1
            )
            self.assertTrue((directory / "reference-0.o").is_file())
            subprocess.run([str(executable)], check=True, timeout=15)

    def test_every_pure_record_owner_stays_and_only_json_owners_leave(self) -> None:
        """Real record validation closes app admission without importing JSON or dropping owners."""
        sources = check_reference_suite.reference_sources()
        owners = {path.relative_to(ROOT).as_posix() for path in (ROOT / "src/record").glob("*.cpp")}
        excluded = {"src/record/json.cpp", "src/record/inventory.cpp"}
        self.assertEqual(owners - set(sources), excluded)
        self.assertIn("src/record/record.cpp", sources)
        self.assertIn("src/app/response_validation.cpp", sources)
        inventory = [ROOT / source for source in sources]
        inventory.append(ROOT / "tests/support/standalone_main.cpp")
        check_reference_suite.validate_source_inventory(inventory)
        for extra in excluded:
            with self.subTest(extra=extra), self.assertRaises(REFUSALS):
                check_reference_suite.validate_source_inventory([*inventory, ROOT / extra])
