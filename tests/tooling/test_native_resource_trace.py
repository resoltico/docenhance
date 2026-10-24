# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Canonical compact event strings retain every original allocation accounting predicate."""

from __future__ import annotations

import base64
import unittest
import zlib

from tools_path import prepare_tool_imports

from restoration_resource_fixture import event_wire

import native_resource_capture as capture

prepare_tool_imports()


class NativeResourceTraceTests(unittest.TestCase):
    """Lossless markers and bounded lowercase hexadecimal refuse ambiguous evidence."""

    def test_zero_maximum_and_all_three_kinds(self) -> None:
        """Zero never loses its kind and maximum u64 needs no packed overflow."""
        maximum = (1 << 64) - 1
        self.assertEqual(capture.trace({"events": event_wire("+0-0!0"), "observed_peak": 0}), 2)
        self.assertEqual(
            capture.trace(
                {
                    "events": event_wire("+ffffffffffffffff-ffffffffffffffff!ffffffffffffffff"),
                    "observed_peak": maximum,
                }
            ),
            2,
        )
        self.assertEqual(capture.trace({"events": event_wire("+10-10"), "observed_peak": 16}), 1)

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
                capture.trace(
                    {
                        "events": event_wire(events) if isinstance(events, str) else events,
                        "observed_peak": 1,
                    }
                )
        for events, peak in (("-1+1", 1), ("+1", 1), ("+1-1", 2)):
            with self.subTest(events=events, peak=peak), self.assertRaises(ValueError):
                capture.trace(
                    {
                        "events": event_wire(events),
                        "observed_peak": peak,
                    }
                )

    def test_original_nested_array_form_refuses(self) -> None:
        """The internal diagnostic format changes cleanly without a compatibility reader."""
        with self.assertRaises(ValueError):
            capture.trace({"events": [[1, 0], [1, 1]], "observed_peak": 1})

    def test_original_token_limit_and_overlap_limit(self) -> None:
        """Exact native event counts remain bounded independently of encoded byte length."""
        self.assertEqual(
            capture.trace({"events": event_wire("!0" * 8192), "observed_peak": 0}), 8192
        )
        self.assertEqual(
            capture.trace({"events": event_wire("!0" * (7 * 8192)), "observed_peak": 0}, 7),
            7 * 8192,
        )
        with self.assertRaises(ValueError):
            capture.trace({"events": event_wire("!0" * (7 * 8192 + 1)), "observed_peak": 0}, 7)
        with self.assertRaises(ValueError):
            capture.trace({"events": event_wire("!f" * (7 * 8192 * 17)), "observed_peak": 0}, 7)

    def test_codec_refuses_noncanonical_or_ambiguous_streams(self) -> None:
        """Only one complete, bounded zlib stream with canonical base64 can carry events."""
        packed = zlib.compress(b"+1-1")
        dictionary = zlib.compressobj(zdict=b"+1-1")
        dictionary_body = dictionary.compress(b"+1-1") + dictionary.flush()

        def wire(raw: bytes) -> str:
            return "z1:" + base64.b64encode(raw).decode("ascii")

        for value in (
            "+1-1",
            "z2:" + event_wire("+1-1")[3:],
            "z1:",
            "z1:!!!!",
            "z1:é",
            event_wire("+1-1") + "=",
            event_wire("+1-1") + "\n",
            wire(packed[:-1]),
            wire(packed + b"junk"),
            wire(packed + packed),
            wire(dictionary_body),
            wire(b"not deflate"),
            wire(zlib.compress(b"\xff")),
            event_wire("!0" * (1024**2 // 2 + 1)),
            "z1:" + "A" * (4 * ((1024**2 + 1026) // 3) + 4),
        ):
            with self.subTest(value=value[:40]), self.assertRaises(ValueError):
                capture.trace({"events": value, "observed_peak": 1}, 100)
        canonical = event_wire("!0")
        alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
        position = len(canonical.rstrip("=")) - 1
        changed = (
            canonical[:position]
            + alphabet[alphabet.index(canonical[position]) + 1]
            + canonical[position + 1 :]
        )
        self.assertEqual(base64.b64decode(changed[3:]), base64.b64decode(canonical[3:]))
        with self.assertRaisesRegex(ValueError, "Noncanonical event base64"):
            capture.event_text(changed)
        self.assertEqual(capture.event_text(event_wire("")), "")
        self.assertEqual(len(capture.event_text(event_wire("!0" * (1024**2 // 2)), 100)), 1024**2)
        with self.assertRaises(ValueError):
            capture.trace({"events": event_wire("!0" * (1024**2 // 2)), "observed_peak": 0}, 7)
