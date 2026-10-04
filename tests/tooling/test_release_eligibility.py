# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Release CI admission rejects indirect checkout, incomplete execution and changed attempts."""

from __future__ import annotations

import copy
import unittest
from typing import TYPE_CHECKING, override
from urllib.parse import parse_qs

from tools_path import ROOT

from changelog import ReleaseError
from ci_contract import job_names
from release_eligibility import require_quality

if TYPE_CHECKING:
    from release_publication import Record

COMMIT = "a" * 40
BASE = "repos/example/project"


class ReleaseEligibilityTests(unittest.TestCase):
    """Controlled API records challenge every identity and completeness claim."""

    @override
    def setUp(self) -> None:
        """Start with a complete direct attempt and an independently captured run listing."""
        self.current: Record = {
            "id": 1,
            "run_attempt": 1,
            "created_at": "2026-10-03T00:00:00Z",
            "head_sha": COMMIT,
            "event": "push",
            "path": ".github/workflows/ci.yml",
            "repository": {"full_name": "example/project"},
            "head_repository": {"full_name": "example/project"},
            "status": "completed",
            "conclusion": "success",
        }
        self.listings: dict[str, list[Record]] = {
            "push": [copy.deepcopy(self.current)],
            "workflow_dispatch": [],
        }
        self.jobs: list[Record] = [
            {
                "name": name,
                "head_sha": COMMIT,
                "run_id": 1,
                "run_attempt": 1,
                "status": "completed",
                "conclusion": "success",
            }
            for name in sorted(job_names())
        ]
        self.reads = 0
        self.change_after_jobs = False

    def request(self, method: str, path: str) -> object:
        """Record-shaped read transport; writes cannot occur through eligibility."""
        self.assertEqual(method, "GET")
        if "/workflows/ci.yml/runs?" in path:
            query = parse_qs(path.partition("?")[2])
            self.assertEqual(query["head_sha"], [COMMIT])
            runs = self.listings[query["event"][0]]
            return {"total_count": len(runs), "workflow_runs": copy.deepcopy(runs)}
        if "/attempts/" in path:
            return {"total_count": len(self.jobs), "jobs": copy.deepcopy(self.jobs)}
        self.reads += 1
        result = copy.deepcopy(self.current)
        if self.change_after_jobs and self.reads > 1:
            result["run_attempt"] = 2
        return result

    def test_complete_direct_attempt_passes(self) -> None:
        """The entire required matrix and final unchanged attempt are observed."""
        self.assertTrue((ROOT / "tools/release_eligibility.py").is_file())
        require_quality(self.request, BASE, COMMIT)
        self.assertEqual(self.reads, 2)

    def test_documented_ref_qualified_workflow_path(self) -> None:
        """GitHub can qualify its workflow file by ref without changing file identity."""
        self.current["path"] = ".github/workflows/ci.yml@main"
        require_quality(self.request, BASE, COMMIT)
        self.current["path"] = ".github/workflows/foreign.yml@main"
        with self.assertRaises(ReleaseError):
            require_quality(self.request, BASE, COMMIT)

    def test_pr_only_and_wrong_identity_refused(self) -> None:
        """A branch head SHA on a PR run cannot certify its tested merge checkout."""
        self.listings["push"][0]["event"] = "pull_request"
        with self.assertRaises(ReleaseError):
            require_quality(self.request, BASE, COMMIT)
        self.listings["push"] = []
        with self.assertRaisesRegex(ReleaseError, "No direct"):
            require_quality(self.request, BASE, COMMIT)

    def test_pending_failed_and_changed_run_refused(self) -> None:
        """A historical listing cannot conceal current failure or another attempt/checkout."""
        original = copy.deepcopy(self.current)
        for changes in (
            {"status": "in_progress", "conclusion": None},
            {"conclusion": "failure"},
            {"head_sha": "b" * 40},
            {"run_attempt": 2},
            {"head_repository": {"full_name": "foreign/project"}},
        ):
            with self.subTest(changes=changes):
                self.current = original | changes
                with self.assertRaises(ReleaseError):
                    require_quality(self.request, BASE, COMMIT)
        self.current = original
        self.reads = 0
        self.change_after_jobs = True
        with self.assertRaisesRegex(ReleaseError, "changed during admission"):
            require_quality(self.request, BASE, COMMIT)

    def test_later_manual_failure_cannot_hide_behind_push_success(self) -> None:
        """The latest direct validation governs even when an earlier direct run passed."""
        later = self.current | {
            "id": 2,
            "event": "workflow_dispatch",
            "created_at": "2026-10-04T00:00:00Z",
            "conclusion": "failure",
        }
        self.listings["workflow_dispatch"] = [later]
        self.current = later
        with self.assertRaisesRegex(ReleaseError, "not successful"):
            require_quality(self.request, BASE, COMMIT)

    def test_equal_timestamp_runs_are_not_guessed(self) -> None:
        """Ambiguous ordering cannot select the convenient success by collection order."""
        self.listings["workflow_dispatch"] = [
            self.current | {"id": 2, "event": "workflow_dispatch", "conclusion": "failure"}
        ]
        with self.assertRaisesRegex(ReleaseError, "ordering is ambiguous"):
            require_quality(self.request, BASE, COMMIT)

    def test_missing_duplicate_skipped_and_other_attempt_jobs_refused(self) -> None:
        """Successful run metadata alone cannot establish complete platform execution."""
        original = copy.deepcopy(self.jobs)
        variants = [original[:-1], [*original[:-1], original[0]]]
        for changes in (
            {"conclusion": "skipped"},
            {"run_attempt": 2},
            {"head_sha": "b" * 40},
            {"run_id": 2},
        ):
            modified = copy.deepcopy(original)
            modified[0] |= changes
            variants.append(modified)
        for jobs in variants:
            with self.subTest(jobs=jobs):
                self.jobs = jobs
                with self.assertRaises(ReleaseError):
                    require_quality(self.request, BASE, COMMIT)
