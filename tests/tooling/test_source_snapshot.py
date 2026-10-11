# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Committed blob transfer must avoid duplex request pipes and preserve exact bytes."""

from __future__ import annotations

import io
import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from tools_path import ROOT

import source_snapshot
from dep_verify import command_environment


class SourceSnapshotTests(unittest.TestCase):
    """Real Git and independent response corruptions protect source-package identity."""

    def test_large_request_set_uses_regular_input_and_preserves_committed_bytes(self) -> None:
        """Requests exceed pipe capacity; dirty and untracked files cannot replace blobs."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            files = {
                f"blob-{index:04d}.dat": str(index).encode() + b"\x00\n" + bytes(range(256)) * 2
                for index in range(2048)
            }
            for name, data in files.items():
                (root / name).write_bytes(data)
            for arguments in (
                ["git", "init", str(root)],
                ["git", "-C", str(root), "add", "."],
                [
                    "git",
                    "-C",
                    str(root),
                    "-c",
                    "user.name=Fixture",
                    "-c",
                    "user.email=fixture@example.invalid",
                    "-c",
                    "commit.gpgsign=false",
                    "commit",
                    "-m",
                    "immutable blobs",
                ],
            ):
                subprocess.run(
                    arguments,
                    env=command_environment(),
                    cwd=ROOT,
                    capture_output=True,
                    check=True,
                    timeout=30,
                )
            (root / "blob-0000.dat").write_bytes(b"dirty replacement")
            (root / "untracked.dat").write_bytes(b"untracked")
            actual_run = subprocess.run
            transfers = []

            def observed_run(
                command: list[str], **kwargs: object
            ) -> subprocess.CompletedProcess[bytes]:
                self.assertIn("cat-file", command)
                self.assertNotIn("input", kwargs)
                stream = kwargs.get("stdin")
                if not isinstance(stream, io.BufferedIOBase):
                    self.fail("Batch requests require a regular input stream")
                self.assertTrue(stat.S_ISREG(os.fstat(stream.fileno()).st_mode))
                self.assertEqual(kwargs["timeout"], 60)
                transfers.append(True)
                return actual_run(
                    command,
                    stdin=stream.fileno(),
                    capture_output=True,
                    cwd=root,
                    env=command_environment(),
                    check=True,
                    timeout=60,
                )

            with patch("source_snapshot.subprocess", SimpleNamespace(run=observed_run)):
                self.assertEqual(source_snapshot.snapshot(root), files)
            self.assertEqual(transfers, [True])

    def test_malformed_batch_response_cannot_become_a_partial_archive(self) -> None:
        """Truncation, wrong identities/types and unrequested bytes are independent refusals."""
        identity = "a" * 40
        content = b"binary\x00\ncontent"
        valid = f"{identity} blob {len(content)}\n".encode() + content + b"\n"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)

            def metadata(*arguments: str, cwd: Path | None = None) -> str:
                del cwd
                if "--show-toplevel" in arguments:
                    return str(root)
                if "rev-parse" in arguments:
                    return "b" * 40
                return f"100644 blob {identity}\tcontent.dat"

            corruptions = (
                valid[:-1],
                valid + b"extra",
                valid.replace(b"blob", b"tree", 1),
                valid.replace(identity.encode(), b"c" * 40, 1),
            )
            for payload in corruptions:
                with (
                    self.subTest(payload=payload[:50]),
                    patch("source_snapshot.run", side_effect=metadata),
                    patch(
                        "source_snapshot.subprocess.run",
                        return_value=subprocess.CompletedProcess([], 0, stdout=payload, stderr=b""),
                    ),
                    self.assertRaises(ValueError),
                ):
                    source_snapshot.snapshot(root)
