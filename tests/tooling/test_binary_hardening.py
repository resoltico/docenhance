# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Protection marker negative controls and real native compiler propagation."""

from __future__ import annotations

import json
import platform
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

from binary_hardening import errors, inspect, markers
from hardening_compilation import command_errors
from hardening_compilation import errors as compilation_errors


class BinaryHardeningTests(unittest.TestCase):
    """A successful build or one mitigation cannot conceal another missing protection."""

    def test_missing_and_disabled_compiler_flags_refused(self) -> None:
        """Actual argument tokens, rather than command substrings, establish propagation."""
        required = ["-fstack-protector-strong", "-D_FORTIFY_SOURCE=2"]
        command = "clang++ -fstack-protector-strong -D_FORTIFY_SOURCE=2 -c main.cpp"
        self.assertEqual(command_errors(command, required), [])
        for flag in required:
            self.assertTrue(command_errors(command.replace(flag, ""), required))
            self.assertTrue(command_errors(command.replace(flag, "prefix" + flag), required))
        for flag in (
            "-fno-stack-protector",
            "-fstack-protector",
            "-fstack-protector-explicit",
            "-U_FORTIFY_SOURCE",
            "-D_FORTIFY_SOURCE=0",
            "-D_FORTIFY_SOURCE=1",
            "/GS-",
            "/guard:cf-",
        ):
            self.assertTrue(command_errors(command + " " + flag, required))

    def test_each_marker_is_required(self) -> None:
        """Independent tool-output fixtures lose one protection at a time."""
        fixtures = {
            "Linux": (
                "Type: DYN\nGNU_STACK 0 0 0 0 0 RW 0x10\nGNU_RELRO\nFLAGS BIND_NOW\nFLAGS_1 PIE",
                "__stack_chk_fail",
                ("DYN", "RW", "GNU_RELRO", "BIND_NOW", "PIE"),
            ),
            "Darwin": ("ARM64 EXECUTE PIE", "___stack_chk_fail", ("EXECUTE", "PIE")),
            "Windows": (
                (
                    "Dynamic base\nHigh Entropy Virtual Addresses\nNX compatible\nGuard\n"
                    "CF Instrumented\nFID table present\n0000140123456780 Security Cookie"
                ),
                "",
                (
                    "Dynamic base",
                    "High Entropy Virtual Addresses",
                    "NX compatible",
                    "CF Instrumented",
                    "FID table present",
                    "0000140123456780",
                ),
            ),
        }
        for system, (headers, symbols, required) in fixtures.items():
            self.assertEqual(markers(system, headers, symbols), [])
            for marker in required:
                with self.subTest(system=system, marker=marker):
                    self.assertTrue(markers(system, headers.replace(marker, ""), symbols))
            if symbols:
                self.assertTrue(markers(system, headers, ""))
        self.assertTrue(
            markers("Darwin", "ARM64 EXECUTE PIE ALLOW_STACK_EXECUTION", "___stack_chk_fail")
        )
        self.assertTrue(
            markers("Linux", fixtures["Linux"][0].replace(" RW ", " RWE "), "__stack_chk_fail")
        )
        with self.assertRaises(ValueError):
            markers("unknown", "", "")

    def test_real_native_flags_and_binary(self) -> None:
        """The production CMake interface compiles and links an actual protected executable."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        with tempfile.TemporaryDirectory(prefix="hardening-control-") as temporary:
            source = Path(temporary)
            (source / "main.cpp").write_text(
                "int destination(int value);\n"
                "int destination(int value) { return value; }\n"
                "int main(int argc, char** argv) { volatile char buffer[32] = {0};\n"
                "buffer[argc % 32] = argv[0][0];\n"
                "int (*volatile invoke)(int) = &destination; return invoke(buffer[0]); }\n"
            )
            (source / "CMakeLists.txt").write_text(f"""cmake_minimum_required(VERSION 4.4)
project(Hardening CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(DE_CXX_STANDARD 23)
include("{ROOT.as_posix()}/cmake/ProjectOptions.cmake")
add_executable(probe main.cpp)
de_apply_options(probe)
""")
            build = source / "build"
            for command in (
                [
                    str(cmake),
                    "-S",
                    str(source),
                    "-B",
                    str(build),
                    "-G",
                    "Ninja",
                    "-DCMAKE_BUILD_TYPE=Release",
                ],
                [str(cmake), "--build", str(build)],
            ):
                result = subprocess.run(
                    command, capture_output=True, text=True, check=False, timeout=90
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            commands = json.loads((build / "compile_commands.json").read_text())
            self.assertEqual(len(commands), 1)
            required = (
                ("/GS", "/guard:cf")
                if platform.system() == "Windows"
                else ("-fstack-protector-strong",)
            )
            for flag in required:
                self.assertIn(flag, commands[0]["command"])
            executable = build / ("probe.exe" if platform.system() == "Windows" else "probe")
            observed = errors(executable)
            diagnostics = (
                inspect("dumpbin", ["/HEADERS", "/LOADCONFIG"], executable)[-8000:]
                if observed and platform.system() == "Windows"
                else str(observed)
            )
            self.assertEqual(observed, [], diagnostics)
            policy = build / "hardening-policy.json"
            self.assertEqual(compilation_errors(build, policy), [])
            commands[0]["command"] = commands[0]["command"].replace(required[0], "")
            (build / "compile_commands.json").write_text(json.dumps(commands))
            self.assertTrue(compilation_errors(build, policy))
            (build / "compile_commands.json").write_text("[]")
            self.assertTrue(compilation_errors(build, policy))
            with (
                patch("binary_hardening.shutil.which", return_value=None),
                self.assertRaises(ValueError),
            ):
                errors(executable)
