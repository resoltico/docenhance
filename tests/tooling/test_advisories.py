# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Advisory identity, pagination, network failure and source-bound review controls."""

from __future__ import annotations

import json
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

from tools_path import ROOT

from advisory_review import reviewed
from check_advisories import matches, request, source_query


class AdvisoryTests(unittest.TestCase):
    """Missing evidence never becomes an empty success or an unrelated advisory exemption."""

    def test_complete_pages_and_refusals(self) -> None:
        """Consume both pages; malformed, repeated and unfinished evidence fails closed."""
        with patch(
            "check_advisories.request",
            side_effect=[
                {"vulns": [{"id": "FIRST"}], "next_page_token": "second"},
                {"vulns": [{"id": "SECOND"}]},
            ],
        ) as query:
            self.assertEqual([m["id"] for m in matches({"commit": "a" * 40})], ["FIRST", "SECOND"])
            self.assertEqual(query.call_args_list[1].args[1]["page_token"], "second")
        for response in (
            {"error": "failed"},
            {"vulns": None},
            {"vulns": [None]},
            {"vulns": [{"id": "../invalid"}]},
            {"next_page_token": False},
            {"vulns": [{"id": "same"}, {"id": "same"}]},
        ):
            with (
                self.subTest(response=response),
                patch("check_advisories.request", return_value=response),
                self.assertRaises((TypeError, ValueError)),
            ):
                matches({"commit": "a" * 40})
        with (
            patch("check_advisories.request", return_value={"next_page_token": "repeat"}),
            self.assertRaises(ValueError),
        ):
            matches({"commit": "a" * 40})
        with (
            patch("check_advisories.MAX_PAGES", 1),
            patch("check_advisories.request", return_value={"next_page_token": "unfinished"}),
            self.assertRaises(ValueError),
        ):
            matches({"commit": "a" * 40})

    def test_transport_failure_size_and_types(self) -> None:
        """HTTP failure, oversized bytes and invalid JSON/types are visible; no retry occurs."""
        for status, raw, exception in (
            (500, b"{}", OSError),
            (200, b"too large", ValueError),
            (200, b"[1]", TypeError),
            (200, b"no-json", ValueError),
            (200, b"\xff", ValueError),
        ):
            with (
                self.subTest(status=status, raw=raw),
                patch("check_advisories.http.client.HTTPSConnection") as connect,
                patch("check_advisories.MAX_RESPONSE_BYTES", 8),
            ):
                response = MagicMock(status=status)
                response.read.return_value = raw
                connect.return_value.getresponse.return_value = response
                with self.assertRaises(exception):
                    request("/query", {"commit": "a" * 40})
                self.assertEqual(connect.return_value.request.call_count, 1)
                connect.return_value.close.assert_called_once()

    def test_exact_receipt_commit_and_archive_scope(self) -> None:
        """Queries use a verified peeled commit, never the annotated tag object."""
        dependency = {"name": "zlib", "transport": "git", "object": "b" * 40}
        with patch("check_advisories.verify", return_value={"resolved_commit": "a" * 40}) as verify:
            self.assertEqual(source_query(dependency, Path("cache")), {"commit": "a" * 40})
            verify.assert_called_once_with(dependency, Path("cache"))
        with (
            patch("check_advisories.verify", return_value={"resolved_commit": None}),
            self.assertRaises(ValueError),
        ):
            source_query(dependency, Path("cache"))
        with patch("check_advisories.verify", return_value={}):
            query = source_query(
                {"name": "tiff", "transport": "archive", "version": "4.7.2"}, Path("cache")
            )
            self.assertEqual(query["package"], {"name": "libtiff", "ecosystem": "OSS-Fuzz"})
            with self.assertRaises(ValueError):
                source_query({"name": "another", "transport": "archive"}, Path("cache"))

    def test_reviews_require_current_source_policy_and_advisory(self) -> None:
        """The two real reviewed records pass; changed identity/content/policy require review."""
        dependencies = json.loads((ROOT / "deps/lock.json").read_bytes())["dependencies"]
        features = (ROOT / "deps/features.json").read_bytes()
        for name, identifier in (("zlib", "CVE-2026-76844"), ("opencv", "OSV-2023-444")):
            dependency = next(d for d in dependencies if d["name"] == name)
            advisory = json.loads(
                (ROOT / "tests/fixtures/advisories" / (identifier + ".json")).read_bytes()
            )
            self.assertTrue(reviewed(dependency, advisory, features))
            self.assertFalse(reviewed({**dependency, "object": "a" * 40}, advisory, features))
            self.assertFalse(reviewed(dependency, {**advisory, "modified": "changed"}, features))
            self.assertFalse(reviewed(dependency, advisory, features + b" "))
            self.assertFalse(reviewed(dependency, {**advisory, "id": "another"}, features))
