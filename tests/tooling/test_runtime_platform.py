# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""A deployment flag and execution on a newer OS cannot establish minimum-runtime support."""

from __future__ import annotations

import unittest

import tools_path

from check_runtime_platform import runtime_errors


class RuntimePlatformTests(unittest.TestCase):
    """Both physical minimum lanes reject wrong host versions and architectures."""

    def test_minimum_macos_execution_is_exact(self) -> None:
        """Healthy ARM/Intel observations pass; newer hosts, wrong OS/CPU and aliases fail."""
        self.assertTrue(tools_path.ROOT.is_dir())
        for lane, machine in (("macos-15-arm64", "arm64"), ("macos-15-x86_64", "x86_64")):
            with self.subTest(lane=lane):
                self.assertEqual(runtime_errors(lane, "Darwin", machine, "15.7.1"), [])
                for system, cpu, version in (
                    ("Darwin", machine, "26.0"),
                    ("Darwin", machine, "14.7"),
                    ("Linux", machine, "15.0"),
                    ("Darwin", "unsupported", "15.0"),
                ):
                    self.assertTrue(runtime_errors(lane, system, cpu, version))
        self.assertTrue(runtime_errors("macos-latest", "Darwin", "arm64", "15.0"))

    def test_original_native_lanes_remain_valid(self) -> None:
        """Forward macOS and original Linux/Windows coverage is retained."""
        for lane, system, machine, version in (
            ("macos-arm64", "Darwin", "arm64", "26.1"),
            ("macos-x86_64", "Darwin", "x86_64", "26.1"),
            ("linux-arm64", "Linux", "aarch64", "6.8"),
            ("linux-x86_64", "Linux", "x86_64", "6.8"),
            ("windows-x86_64", "Windows", "AMD64", "11"),
        ):
            with self.subTest(lane=lane):
                self.assertEqual(runtime_errors(lane, system, machine, version), [])
