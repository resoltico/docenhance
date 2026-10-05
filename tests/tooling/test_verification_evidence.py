# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real and synthetic negative controls for complete suite execution and source artifacts."""

from __future__ import annotations

import hashlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import deps
import license_inventory
import package_inspection
import package_native
import package_source
import source_snapshot
import test_evidence


class EvidenceTests(unittest.TestCase):
    """Exit zero, decorative success and matching counts cannot substitute for complete work."""

    def test_actual_ctest_disabled_and_skipped_work_is_rejected(self) -> None:
        """CTest really exits zero for both escapes; discovery and fresh results reject them."""
        cmake = shutil.which("cmake")
        ctest = shutil.which("ctest")
        self.assertIsNotNone(cmake)
        self.assertIsNotNone(ctest)
        for property_text in ("DISABLED TRUE", "SKIP_RETURN_CODE 1"):
            with self.subTest(property=property_text), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root / "CMakeLists.txt").write_text(
                    "cmake_minimum_required(VERSION 4.4)\nproject(Probe NONE)\nenable_testing()\n"
                    'add_test(NAME escaped COMMAND "${CMAKE_COMMAND}" -E false)\n'
                    f"set_tests_properties(escaped PROPERTIES {property_text})\n"
                    'add_test(NAME runs COMMAND "${CMAKE_COMMAND}" -E true)\n',
                )
                build = root / "build"
                subprocess.run(
                    [str(cmake), "-G", "Ninja", "-S", str(root), "-B", str(build)],
                    check=True,
                    capture_output=True,
                )
                report = root / "ctest.xml"
                result = subprocess.run(
                    [str(ctest), "--test-dir", str(build), "--output-junit", str(report)],
                    check=False,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 0)
                with self.assertRaises(test_evidence.EvidenceError):
                    test_evidence.discovery(str(ctest), build)
                with self.assertRaises(test_evidence.EvidenceError):
                    test_evidence.complete_junit(report, {"escaped", "runs"})

    def test_junit_requires_unique_complete_executed_results(self) -> None:
        """Missing/duplicate identities and run statuses reject apparently successful reports."""
        valid = '<testsuite><testcase name="probe" status="run"/></testsuite>'
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "result.xml"
            path.write_text(valid)
            test_evidence.complete_junit(path, {"probe"})
            for invalid in (
                valid.replace('status="run"', 'status="disabled"'),
                valid.replace('status="run"', 'status="skipped"'),
                valid.replace("/>", "><failure/></testcase>"),
                valid.replace('name="probe"', 'name="other"'),
                "<testsuite/>",
                valid.replace("</testsuite>", '<testcase name="probe" status="run"/></testsuite>'),
            ):
                with self.subTest(invalid=invalid):
                    path.write_text(invalid)
                    with self.assertRaises(test_evidence.EvidenceError):
                        test_evidence.complete_junit(path, {"probe"})

    def test_catch_success_needs_actual_assertions_and_no_skips(self) -> None:
        """Catch process success cannot hide zero assertions, skipped cases or expected failure."""
        valid = (
            '<Catch2TestRun><TestCase name="probe"/><OverallResults successes="1" '
            'failures="0" expectedFailures="0" skips="0"/></Catch2TestRun>'
        )
        test_evidence.catch_result(valid, "probe")
        for mutation in ('successes="0"', 'failures="1"', 'expectedFailures="1"', 'skips="1"'):
            key = mutation.split("=", 1)[0]
            invalid = valid.replace(f'{key}="{1 if key == "successes" else 0}"', mutation)
            with self.subTest(mutation=mutation), self.assertRaises(test_evidence.EvidenceError):
                test_evidence.catch_result(invalid, "probe")
        with self.assertRaises(test_evidence.EvidenceError):
            test_evidence.report_xml('<!DOCTYPE x [<!ENTITY a "value">]><x>&a;</x>')

    def test_real_tooling_empty_and_skipped_modules_fail(self) -> None:
        """Run isolated unittest controls through the actual strict suite entry point."""
        runner = ROOT / "tools/run_tooling_tests.py"
        for body, expected in (
            ("", 1),
            (
                (
                    "import unittest\nclass Probe(unittest.TestCase):\n"
                    ' @unittest.skip("skipped")\n def test_probe(self): pass\n'
                ),
                1,
            ),
            (
                (
                    "import unittest\nclass Probe(unittest.TestCase):\n"
                    " def test_probe(self): self.assertEqual(1, 1)\n"
                ),
                0,
            ),
        ):
            with tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                (directory / "test_probe.py").write_text(body)
                result = subprocess.run(
                    [
                        sys.executable,
                        str(runner),
                        "--directory",
                        str(directory),
                    ],
                    capture_output=True,
                    check=False,
                )
                self.assertEqual(result.returncode, expected, result.stderr)


class DeliveredBytesTests(unittest.TestCase):
    """Independent expected bytes reject stale, omitted and permissive packaged metadata."""

    def test_closed_inventory_rejects_altered_or_missing_contracts_and_licenses(self) -> None:
        """Metadata existence and a self-supplied hash do not prove source agreement."""
        expected = {"schema.json": b'{"additionalProperties":false}', "LICENSE": b"upstream"}
        self.assertEqual(package_inspection.differences(expected, expected), [])
        for changed in (
            {"schema.json": b"{}", "LICENSE": b"upstream"},
            {"schema.json": expected["schema.json"]},
            expected | {"extra.dll": b"x"},
        ):
            self.assertTrue(package_inspection.differences(changed, expected))

    def test_windows_virtual_contract_requires_an_os_host(self) -> None:
        """Synthetic resolution tests classification; Windows CI exercises the actual OS loader."""
        with tempfile.TemporaryDirectory() as temporary:
            system = Path(temporary)
            (system / "KERNEL32.dll").write_bytes(b"OS fixture")
            self.assertTrue(package_inspection.windows_system_import("KERNEL32.dll", system))
            name = "api-ms-win-core-synch-l1-2-0.dll"
            with patch(
                "package_inspection.windows_api_host", return_value=system / "KernelBase.dll"
            ):
                self.assertTrue(package_inspection.windows_system_import(name, system))
            for host in (None, system.parent / "foreign.dll"):
                with patch("package_inspection.windows_api_host", return_value=host):
                    self.assertFalse(package_inspection.windows_system_import(name, system))
            self.assertFalse(
                package_inspection.windows_system_import(
                    "api-ms-win-crt-runtime-l1-1-0.dll", system
                )
            )
            self.assertFalse(package_inspection.windows_system_import("opencv_photo.dll", system))

    def test_committed_snapshot_excludes_dirty_and_untracked_contents(self) -> None:
        """Source payloads and hashes describe the committed tree even in a dirty checkout."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            deps.run("git", "init", str(root))
            (root / "CMakeLists.txt").write_text("project(DocEnhance VERSION 1.2.3)\n")
            (root / "source.bin").write_bytes(b"\x00\xffexact\n")
            deps.run("git", "add", ".", cwd=root)
            deps.run(
                "git",
                "-c",
                "user.name=Fixture",
                "-c",
                "user.email=fixture@example.invalid",
                "-c",
                "commit.gpgsign=false",
                "commit",
                "-m",
                "source",
                cwd=root,
            )
            (root / "source.bin").write_bytes(b"dirty")
            (root / "private.txt").write_text("not committed")
            snap = source_snapshot.snapshot(root)
            self.assertEqual(snap["source.bin"], b"\x00\xffexact\n")
            self.assertNotIn("private.txt", snap)
            archive = root / "source.tar.gz"
            manifest = "".join(
                f"{hashlib.sha256(data).hexdigest()}  {name}\n"
                for name, data in sorted(snap.items())
            )
            package_source.write_tar(archive, snap, manifest, 0)
            extracted = root / "extracted"
            deps.safe_extract(archive, extracted)
            self.assertEqual((extracted / "docenhance/source.bin").read_bytes(), snap["source.bin"])


class InventoryOwnershipTests(unittest.TestCase):
    """Regeneration manages its configured metadata directory without clearing unrelated paths."""

    def test_replacement_removes_stale_output_and_refuses_unowned_names(self) -> None:
        """Synthetic generation proves cleanup authority; real source/package checks prove bytes."""

        def generated(
            _cache: Path, out: Path, _platform: str, _compiler: str, _created: str
        ) -> None:
            out.mkdir()
            (out / "current-notice").write_text("current generated bytes")

        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            (build / "CMakeCache.txt").write_text(
                f"CMAKE_PROJECT_NAME:STATIC=DocEnhance\nCMAKE_HOME_DIRECTORY:INTERNAL={ROOT}\n"
            )
            output = build / "package-metadata"
            output.mkdir()
            (output / "obsolete-notice").write_text("prior generated bytes")
            arguments = [
                "license_inventory.py",
                "--cache",
                str(build),
                "--out",
                str(output),
                "--platform",
                "fixture",
                "--compiler",
                "fixture",
            ]
            with patch("sys.argv", arguments), patch("license_inventory.generate", generated):
                self.assertEqual(license_inventory.main(), 0)
            self.assertEqual({p.name for p in output.iterdir()}, {"current-notice"})
            foreign = build / "unowned"
            foreign.mkdir()
            protected = foreign / "preserve"
            protected.write_text("unrelated")
            arguments[4] = str(foreign)
            with patch("sys.argv", arguments), self.assertRaises(license_inventory.InventoryError):
                license_inventory.main()
            self.assertEqual(protected.read_text(), "unrelated")


class NativeArchiveTests(unittest.TestCase):
    """Payload archives preserve intended bytes/modes without incidental host attributes."""

    def test_native_payload_is_closed_regular_and_byte_stable(self) -> None:
        """Repack a stage twice and check the tar contains only intended regular payloads."""
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary)
            stage = parent / "docenhance-fixture"
            (stage / "bin").mkdir(parents=True)
            (stage / "bin/docenhance").write_bytes(b"native fixture bytes")
            (stage / "LICENSE").write_bytes(b"original license bytes")
            target = parent / (stage.name + ".tar.gz")
            package_native.package(stage, target)
            first = target.read_bytes()
            package_native.package(stage, target)
            self.assertEqual(target.read_bytes(), first)
            with tarfile.open(target) as archive:
                entries = archive.getmembers()
                self.assertEqual(
                    {item.name for item in entries},
                    {stage.name + "/bin/docenhance", stage.name + "/LICENSE"},
                )
                self.assertTrue(all(item.isfile() and not item.pax_headers for item in entries))
                executable = archive.getmember(stage.name + "/bin/docenhance")
                self.assertEqual(executable.mode, package_native.EXECUTABLE_MODE)
            with self.assertRaises(ValueError):
                package_native.package(stage, parent / "unowned.tar.gz")
