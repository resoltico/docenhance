# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Windows generated-command quoting is independent of POSIX backslash escaping."""

from __future__ import annotations

import ctypes
import os
import subprocess
import sys
import unittest

from tools_path import ROOT

from command_arguments import command_arguments, windows_arguments


class CommandArgumentsTests(unittest.TestCase):
    """Quoted executable/source paths retain identity and cannot manufacture tidy arguments."""

    def test_windows_quotes_backslashes_and_empty_arguments(self) -> None:
        """Independent fixed expectations and the stdlib encoder cover emitted Windows forms."""
        cases = (
            (
                '"C:\\Program Files\\CMake\\cmake.exe" -E __run_co_compile',
                [r"C:\Program Files\CMake\cmake.exe", "-E", "__run_co_compile"],
            ),
            (
                (
                    'tool --tidy="C:\\Tools Name\\clang-tidy.exe;--warnings-as-errors=*" '
                    '--source="C:\\source name\\a.cpp"'
                ),
                [
                    "tool",
                    r"--tidy=C:\Tools Name\clang-tidy.exe;--warnings-as-errors=*",
                    r"--source=C:\source name\a.cpp",
                ],
            ),
            ('one "" "two words"', ["one", "", "two words"]),
            (r'one "trailing slash\\"', ["one", "trailing slash\\"]),
        )
        for command, expected in cases:
            with self.subTest(command=command):
                self.assertEqual(windows_arguments(command), expected)
                self.assertEqual(command_arguments(command, windows=True), expected)
                self.assertEqual(windows_arguments(subprocess.list2cmdline(expected)), expected)
                if os.name == "nt":
                    self.assertEqual(self.native_windows_arguments(command), expected)
        with self.assertRaises(ValueError):
            windows_arguments('tool --tidy="unterminated')
        self.assertEqual(
            command_arguments('"a b" --source="c d"', windows=False), ["a b", "--source=c d"]
        )
        source = [str(ROOT / "source path/a.cpp")]
        self.assertEqual(windows_arguments(subprocess.list2cmdline(source)), source)

    @staticmethod
    def native_windows_arguments(command: str) -> list[str]:
        """Compare with Shell32's actual parser when this suite runs on Windows."""
        if sys.platform == "win32":
            shell = ctypes.WinDLL("shell32", use_last_error=True)
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            shell.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
            shell.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
            kernel.LocalFree.argtypes = [ctypes.c_void_p]
            count = ctypes.c_int()
            parsed = shell.CommandLineToArgvW(command, ctypes.byref(count))
            try:
                return [str(parsed[index]) for index in range(count.value)]
            finally:
                kernel.LocalFree(parsed)
        else:
            message = "Native Windows argument parsing requires Windows"
            raise RuntimeError(message)
