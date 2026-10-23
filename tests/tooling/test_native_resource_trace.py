# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Canonical compact event strings retain every original allocation accounting predicate."""

from __future__ import annotations

import unittest

from tools_path import prepare_tool_imports

import native_resource_capture as capture

prepare_tool_imports()


class NativeResourceTraceTests(unittest.TestCase):
    """Lossless markers and bounded lowercase hexadecimal refuse ambiguous evidence."""

    def test_zero_maximum_and_all_three_kinds(self) -> None:
        """Zero never loses its kind and maximum u64 needs no packed overflow."""
        maximum = (1 << 64) - 1
        self.assertEqual(capture.trace({"events": "+0-0!0", "observed_peak": 0}), 2)
        self.assertEqual(
            capture.trace(
                {
                    "events": "+ffffffffffffffff-ffffffffffffffff!ffffffffffffffff",
                    "observed_peak": maximum,
                }
            ),
            2,
        )
        self.assertEqual(capture.trace({"events": "+10-10", "observed_peak": 16}), 1)

    def test_canonical_shape_and_boundaries_refuse(self) -> None:
        """Old arrays, missing digits, leading zeros and unconsumed text cannot qualify."""
        for events in (
            list[int](),
            None,
            True,
            "",
            "1",
            "+",
            "+00",
            "+01-1",
            "+A-A",
            "+1-1!",
            " +1-1",
            "+1-1 ",
            "+0x1-1",
            "+1?1",
            "+1-1junk",
            "+10000000000000000-10000000000000000",
            "!0" * 8193,
        ):
            with self.subTest(events=str(events)[:40]), self.assertRaises(ValueError):
                capture.trace({"events": events, "observed_peak": 1})
        for events, peak in (("-1+1", 1), ("+1", 1), ("+1-1", 2)):
            with self.subTest(events=events, peak=peak), self.assertRaises(ValueError):
                capture.trace({"events": events, "observed_peak": peak})

    def test_original_nested_array_form_refuses(self) -> None:
        """The internal diagnostic format changes cleanly without a compatibility reader."""
        with self.assertRaises(ValueError):
            capture.trace({"events": [[1, 0], [1, 1]], "observed_peak": 1})

    def test_original_token_limit_and_overlap_limit(self) -> None:
        """Exact native event counts remain bounded independently of encoded byte length."""
        self.assertEqual(capture.trace({"events": "!0" * 8192, "observed_peak": 0}), 8192)
        self.assertEqual(
            capture.trace({"events": "!0" * (7 * 8192), "observed_peak": 0}, 7), 7 * 8192
        )
        with self.assertRaises(ValueError):
            capture.trace({"events": "!0" * (7 * 8192 + 1), "observed_peak": 0}, 7)
        with self.assertRaises(ValueError):
            capture.trace({"events": "!f" * (7 * 8192 * 17), "observed_peak": 0}, 7)
