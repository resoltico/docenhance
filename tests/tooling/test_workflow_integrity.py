# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Mutated real workflows must expose removed work and decorative success."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import override

from tools_path import ROOT

from parallel import available_jobs
from workflow_config import workflow
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

    def test_source_cache_identity_and_payload(self) -> None:
        """Restores cannot include binaries, writer claims or fallback identities."""
        for name in ("ci.yml", "nightly.yml"):
            for old, new in (
                (".cache/deps/archives", ".cache/deps"),
                (".cache/deps/sources", "out/release/prefix"),
                (".cache/deps/receipts", ".cache/deps/acquisition.active"),
                ("'tools/dep_verify.py', ", ""),
                ("deps-source-${{ runner.os }}", "deps-source-shared"),
                (
                    "          key: deps-source-",
                    "          restore-keys: deps-source-\n          key: deps-source-",
                ),
            ):
                with self.subTest(name=name, old=old):
                    self.assert_mutation_refused(
                        f".github/workflows/{name}", old, new, "Dependency source cache"
                    )

    def test_alternate_cache_actions_are_refused(self) -> None:
        """Combined cache actions cannot restore binaries or save automatic partial state."""
        for action in ("actions/cache/restore", "actions/cache", "Actions/Cache"):
            for name in ("ci.yml", "nightly.yml"):
                with self.subTest(action=action, name=name):
                    self.assert_mutation_refused(
                        f".github/workflows/{name}",
                        "      - name: Restore locked dependency sources",
                        (
                            f"      - uses: {action}@55cc8345863c7cc4c66a329aec7e433d2d1c52a9\n"
                            "        with:\n          path: out/release/prefix\n"
                            "          key: alternate\n"
                            "      - name: Restore locked dependency sources"
                        ),
                        "Dependency source cache",
                    )

    def test_source_cache_cannot_bypass_verification(self) -> None:
        """Cache hits still acquire; failed or unfinished acquisition cannot be saved."""
        for name in ("ci.yml", "nightly.yml"):
            for old, new in (
                (
                    "      - run: cmake -P cmake/AcquireDependencies.cmake",
                    (
                        "      - if: steps.dependency-source-cache.outputs.cache-hit != 'true'\n"
                        "        run: cmake -P cmake/AcquireDependencies.cmake"
                    ),
                ),
                (
                    "if: steps.dependency-source-cache.outputs.cache-hit != 'true'",
                    "if: always()",
                ),
                (
                    "      - name: Restore locked dependency sources",
                    "      - if: false\n        name: Restore locked dependency sources",
                ),
            ):
                with self.subTest(name=name, old=old):
                    diagnostic = "executable step" if "- run:" in old else "Dependency source cache"
                    self.assert_mutation_refused(f".github/workflows/{name}", old, new, diagnostic)
            path = self.root / ".github/workflows" / name
            original = path.read_text()
            acquisition = "      - run: cmake -P cmake/AcquireDependencies.cmake\n"
            self.assertIn(acquisition, original)
            changed = original.replace(acquisition, "", 1)
            # Move acquisition after the save: its success can no longer authorize saved bytes.
            save = changed.index(
                "      - name: Save verified dependency sources", original.index(acquisition)
            )
            following = changed.index("      - ", save + len("      - "))
            changed = changed[:following] + acquisition + changed[following:]
            path.write_text(changed)
            try:
                self.assertTrue(any("bracket" in error for error in integrity_errors(self.root)))
            finally:
                path.write_text(original)

    def test_required_execution_starts_independently(self) -> None:
        """A source job produces no inputs for the native, engine or sanitizer executions."""
        for job in ("native", "fuzz", "sanitize"):
            with self.subTest(job=job):
                self.assert_mutation_refused(
                    ".github/workflows/ci.yml",
                    f"  {job}:\n",
                    f"  {job}:\n    needs: structural\n",
                    "start independently",
                )

    def test_runner_worker_budget_is_executable(self) -> None:
        """Each expensive execution exports the real bounded CPU count before using it."""
        for filename, jobs in (
            ("ci.yml", ("structural", "native", "fuzz", "sanitize")),
            ("nightly.yml", ("sanitize", "campaign")),
        ):
            document = workflow(ROOT / ".github/workflows" / filename)
            for name in jobs:
                with self.subTest(filename=filename, job=name):
                    steps = document["jobs"][name]["steps"]
                    budgets = [step for step in steps if "GITHUB_ENV" in step.get("run", "")]
                    self.assertEqual(len(budgets), 1)
                    step = budgets[0]
                    self.assertEqual(step.get("shell"), "python")
                    self.assertNotIn("if", step)
                    output = self.root / "environment.txt"
                    output.unlink(missing_ok=True)
                    result = subprocess.run(
                        [sys.executable, "-c", step["run"]],
                        cwd=ROOT,
                        env={**os.environ, "GITHUB_ENV": str(output)},
                        capture_output=True,
                        text=True,
                        check=False,
                        timeout=30,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(output.read_text(), f"DE_BUILD_JOBS={available_jobs()}\n")
                    workloads = [
                        i
                        for i, item in enumerate(steps)
                        if "cmake --" in item.get("run", "")
                        or "check_reference_suite.py" in item.get("run", "")
                    ]
                    self.assertTrue(workloads)
                    self.assertLess(steps.index(step), min(workloads))

    def test_fuzz_parallelism_and_complete_execution(self) -> None:
        """Planning and configuration agree, and all campaigns still execute CTest."""
        for filename in ("ci.yml", "nightly.yml"):
            for old, new in (
                ("--jobs 4 --job-budget", "--jobs 2 --job-budget"),
                ("-DDE_FUZZ_JOBS=4", "-DDE_FUZZ_JOBS=2"),
                ("ctest --preset", "echo ctest --preset"),
            ):
                with self.subTest(filename=filename, old=old):
                    self.assert_mutation_refused(
                        f".github/workflows/{filename}",
                        old,
                        new,
                        "executable step",
                    )
            document = workflow(ROOT / ".github/workflows" / filename)
            job = document["jobs"]["fuzz" if filename == "ci.yml" else "campaign"]
            commands = [step.get("run", "") for step in job["steps"]]
            plan = next(i for i, command in enumerate(commands) if "--plan --seconds" in command)
            install = commands.index("python tools/install_llvm.py --fuzzing")
            self.assertLess(plan, install)

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
