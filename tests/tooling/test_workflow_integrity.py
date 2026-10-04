# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Mutated real workflows must expose removed work and decorative success."""

from __future__ import annotations

import shutil
import tempfile
import unittest
from pathlib import Path
from typing import override

from tools_path import ROOT

from workflow_integrity import integrity_errors


class WorkflowIntegrityTests(unittest.TestCase):
    """Guard actual execution structure rather than its comments or completion labels."""

    @override
    def setUp(self) -> None:
        """Each case owns an isolated copy of the real workflow and Python authorities."""
        directory = tempfile.TemporaryDirectory(prefix="workflow-integrity-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        for name in (
            "deps/tools.json",
            "ruff.toml",
            "mypy.ini",
            "README.md",
            ".pre-commit-config.yaml",
        ):
            destination = self.root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, destination)
        shutil.copytree(ROOT / ".github/workflows", self.root / ".github/workflows")

    def assert_mutation_refused(self, name: str, old: str, new: str, diagnostic: str) -> None:
        """Require the intended rejection and restore the isolated input between controls."""
        path = self.root / name
        original = path.read_text()
        self.assertIn(old, original)
        path.write_text(original.replace(old, new, 1))
        try:
            self.assertTrue(any(diagnostic in error for error in integrity_errors(self.root)))
        finally:
            path.write_text(original)

    def test_reviewed_workflows(self) -> None:
        """Current source, platform, compiler, engine and aggregate routes agree."""
        self.assertEqual(integrity_errors(self.root), [])

    def test_advisory_observation_cannot_be_disabled(self) -> None:
        """Scheduled monitoring must acquire identities and perform a mandatory query."""
        for old, new in (
            ("  advisories:\n", "  advisories:\n    if: false\n"),
            ("      - run: python tools/check_advisories.py", "      - run: echo observed"),
            ("      - run: python tools/deps.py fetch", "      - run: echo acquired"),
        ):
            with self.subTest(new=new):
                self.assert_mutation_refused(".github/workflows/nightly.yml", old, new, "Required")

    def test_comments_echo_condition_and_optional_steps(self) -> None:
        """No mention of the aggregate can substitute for its unconditional execution."""
        old = "      - run: python tools/check_all.py"
        for new in (
            "      # - run: python tools/check_all.py",
            "      - run: echo python tools/check_all.py",
            "      - if: false\n        run: python tools/check_all.py",
            "      - continue-on-error: true\n        run: python tools/check_all.py",
            (
                "      - run: |\n          if false; then\n"
                "            python tools/check_all.py\n          fi"
            ),
        ):
            with self.subTest(new=new):
                self.assert_mutation_refused(
                    ".github/workflows/ci.yml", old, new, "executable step"
                )

    def test_job_conditions_and_aggregate_results(self) -> None:
        """Skipped/optional work, detached dependencies and echoed results must fail."""
        for old, new, diagnostic in (
            ("  native:\n", "  native:\n    if: false\n", "Required job"),
            ("  sanitize:\n", "  sanitize:\n    continue-on-error: true\n", "Required job"),
            (
                "[structural, native, fuzz, sanitize]",
                "[structural, fuzz, sanitize]",
                "depend on every",
            ),
            (
                'test "$NATIVE_RESULT" = success',
                'echo test "$NATIVE_RESULT" = success',
                "actual result assertion",
            ),
            ("${{ needs.native.result }}", "success", "result bindings"),
            ("test job\n        shell: bash", "test job\n        shell: bash {0}", "fatal Bash"),
        ):
            with self.subTest(old=old):
                self.assert_mutation_refused(".github/workflows/ci.yml", old, new, diagnostic)

    def test_matrices_and_compiler_bindings(self) -> None:
        """Missing platforms/modes and decorative compiler pins cannot preserve coverage."""
        for old, new, diagnostic in (
            ("fail-fast: false", "fail-fast: true", "matrix coverage"),
            ("preset: [sanitize, tsan]", "preset: [sanitize]", "matrix coverage"),
            ("os: ubuntu-24.04-arm", "os: ubuntu-24.04", "matrix coverage"),
            ("CC: clang-23", "CC: gcc # CC: clang-23", "pinned compiler"),
            ("preset: fuzz-afl", "preset: fuzz", "matrix coverage"),
        ):
            with self.subTest(old=old):
                self.assert_mutation_refused(".github/workflows/ci.yml", old, new, diagnostic)

    def test_package_and_workload_steps_remain_executable(self) -> None:
        """Removing package inspection or workload admission cannot leave a passing matrix."""
        for name, old, new in (
            ("ci.yml", "python tools/package_smoke.py", "echo python tools/package_smoke.py"),
            (
                "ci.yml",
                "python tools/run_fuzz_campaign.py --plan",
                "echo python tools/run_fuzz_campaign.py --plan",
            ),
            ("nightly.yml", "ctest --preset", "echo ctest --preset"),
        ):
            with self.subTest(name=name, old=old):
                self.assert_mutation_refused(
                    f".github/workflows/{name}", old, new, "executable step"
                )

    def test_duplicate_mappings_and_unpinned_actions(self) -> None:
        """YAML overwrites and actual unpinned Action values must be rejected."""
        self.assert_mutation_refused(
            ".github/workflows/ci.yml",
            "  native:\n",
            "  native: {}\n  native:\n",
            "Duplicate workflow",
        )
        self.assert_mutation_refused(
            ".github/workflows/ci.yml",
            "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1",
            "actions/checkout@main",
            "Unpinned action",
        )
        self.assert_mutation_refused(
            ".github/workflows/ci.yml",
            "  pull_request:",
            "  pull_request_target:",
            "Privileged pull-request",
        )

    def test_release_identity_and_permissions(self) -> None:
        """Mutable checkouts or missing CI read authority cannot support release eligibility."""
        self.assert_mutation_refused(
            ".github/workflows/ci.yml", "ref: ${{ github.sha }}", "ref: main", "immutable event SHA"
        )
        self.assert_mutation_refused(
            ".github/workflows/source.yml", "actions: read", "actions: write", "Actions-read only"
        )

    def test_source_and_hook_cannot_lose_their_checks(self) -> None:
        """The source preflight and normal local commit always execute the aggregate."""
        self.assert_mutation_refused(
            ".github/workflows/source.yml",
            "run: python tools/check_all.py",
            "run: echo python tools/check_all.py",
            "executable step",
        )
        self.assert_mutation_refused(
            ".pre-commit-config.yaml", "always_run: true", "always_run: false", "must always run"
        )
        (self.root / ".github/workflows/ci.yml").unlink()
        self.assertTrue(any("Workflow inventory" in e for e in integrity_errors(self.root)))
