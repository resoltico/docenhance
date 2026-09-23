# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Exercise real nested CTest scheduling with explicitly synthetic runner evidence fixtures."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import fuzz_execution
import fuzz_instrumentation
import fuzz_manifest
import run_fuzz_campaign

# This is an orchestration fixture, NOT an image harness or claimed native engine result.
CHILD = """
import json, os, pathlib, sys, time
root = pathlib.Path(os.environ["DE_FUZZ_RUN_ROOT"])
name = sys.argv[1]
(root / (name + ".ready")).touch()
deadline = time.monotonic() + 3
while not all((root / (item + ".ready")).exists() for item in ("a", "b")):
    if time.monotonic() >= deadline:
        raise SystemExit(1)
    time.sleep(0.01)
out = root / (name + "-fixture")
out.mkdir()
(out / "result.json").write_text(json.dumps({
    "target": name, "passed": True, "exit_code": 0, "executions": 1, "findings": []
}))
"""


def make_ctest_tree(root: Path, script: str) -> None:
    """Create only CTest fixtures; no compiler or replacement product implementation is used."""
    child = root / "child.py"
    child.write_text(textwrap.dedent(script), encoding="utf-8")
    tests = [
        f'add_test(fuzz-{name} "{Path(sys.executable).as_posix()}" "{child.as_posix()}" {name})\n'
        for name in ("a", "b")
    ]
    (root / "CTestTestfile.cmake").write_text("".join(tests), encoding="utf-8")


class CampaignOrchestrationTests(unittest.TestCase):
    """Child-level parallelism and evidence reconciliation are tested independently of engines."""

    def test_parallelism_reaches_the_actual_child_and_missing_evidence_fails(self) -> None:
        """Two barrier participants finish together; serial execution or empty evidence fails."""
        ctest = shutil.which("ctest")
        if ctest is None:
            self.fail("Install the pinned build tools before running orchestration tests")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            make_ctest_tree(root, CHILD)
            args = argparse.Namespace(build=root, ctest=ctest, seconds=1, jobs=2)
            with (
                patch("run_fuzz_campaign.registration", return_value={"a", "b"}),
                patch("run_fuzz_campaign.inspect_archives", return_value={}),
            ):
                self.assertEqual(run_fuzz_campaign.campaign(args), 0)
                args.jobs = 1
                self.assertEqual(run_fuzz_campaign.campaign(args), 1)
                args.jobs = 2
                make_ctest_tree(root, "raise SystemExit(0)\n")
                self.assertEqual(run_fuzz_campaign.campaign(args), 1)
            reports = list((root / "fuzz-work").glob("campaign-*/campaign.json"))
            self.assertEqual(len(reports), 3)
            self.assertEqual(sum(json.loads(p.read_text())["passed"] for p in reports), 1)

    def test_invalid_cli_duration_does_not_claim_a_workspace(self) -> None:
        """Duration zero fails before any engine or evidence directory can be created."""
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary) / "uncreated"
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/run_fuzzers.py"),
                    "--target",
                    "pages",
                    "--binary",
                    sys.executable,
                    "--seconds",
                    "0",
                    "--work",
                    str(work),
                ],
                capture_output=True,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(work.exists())

    def test_codec_symbols_need_both_sanitizers_and_coverage(self) -> None:
        """Wrapper-only or partially instrumented upstream archives cannot pass the check."""
        symbols = "__asan_load1 __ubsan_handle_type_mismatch_v1 __sanitizer_cov_8bit_counters_init"
        self.assertTrue(fuzz_instrumentation.instrumented_symbols(symbols))
        for removed in (
            "__asan_load1",
            "__ubsan_handle_type_mismatch_v1",
            "__sanitizer_cov_8bit_counters_init",
        ):
            self.assertFalse(
                fuzz_instrumentation.instrumented_symbols(symbols.replace(removed, ""))
            )

    @unittest.skipUnless(os.name == "posix", "POSIX symlink semantics")
    def test_corpus_symlink_is_not_followed(self) -> None:
        """External bytes cannot silently enter a supposedly repository-owned seed inventory."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "fuzz/corpus/probe"
            source.mkdir(parents=True)
            (root / "fuzz/regressions/probe").mkdir(parents=True)
            foreign = root / "foreign"
            foreign.write_bytes(b"preserve")
            (source / "alias").symlink_to(foreign)
            target = fuzz_manifest.Target("probe", "probe.cpp", ("de_core",), 64)
            run = fuzz_execution.Run(target, Path(sys.executable), "libfuzzer", 1, root)
            with self.assertRaises(fuzz_manifest.FuzzError):
                fuzz_execution.stage_corpus(run, fuzz_execution.new_directory(root, "case-"))
            self.assertEqual(foreign.read_bytes(), b"preserve")
