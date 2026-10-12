# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Failed real child commands cannot masquerade as small successful resource envelopes."""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import tools_path

from measure_pipeline_resources import main, observe


class PipelineResourceTests(unittest.TestCase):
    """Exercise the actual subprocess boundary and controlled prerequisite refusal."""

    def test_refused_or_unknown_publication_cannot_emit_resource_evidence(self) -> None:
        """Matching error exit codes and successful unknown publication both refuse measurement."""
        self.assertTrue(tools_path.ROOT.is_dir())
        if os.name != "posix":
            with (
                patch.object(sys, "argv", ["measure", "--executable", "binary", "--out", "report"]),
                self.assertRaises(SystemExit) as error,
            ):
                main()
            self.assertEqual(error.exception.code, 2)
            return
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "command"
            for index, (code, state) in enumerate(((3, "not_started"), (0, "unknown"))):
                payload = json.dumps({"exit_code": code, "publication": state})
                executable.write_text(
                    f"#!{sys.executable}\nprint({payload!r})\nraise SystemExit({code})\n"
                )
                executable.chmod(0o755)
                with self.assertRaisesRegex(ValueError, "successful completed processing"):
                    observe(executable, root / "source.png", root / str(index), [])
