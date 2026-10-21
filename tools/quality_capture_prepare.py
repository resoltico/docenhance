# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Just-in-time capture command/directory/raw-file preparation and namespace admission."""

from __future__ import annotations

import os
import stat
from typing import TYPE_CHECKING, Any

from quality_acquisition import confined
from quality_attempt import CaptureAdmission, issue, note_issue

if TYPE_CHECKING:
    from pathlib import Path
    from typing import BinaryIO


def _argv(attempt: dict[str, Any], slot: dict[str, Any]) -> list[str]:
    requested, actual, target = slot["requested_argv"], slot["argv"], slot["planned_target"]
    if requested is None or actual is None or target is None:
        message = "Invocation command cannot be truthfully prepared"
        raise ValueError(message)
    if not isinstance(actual, list) or not all(
        isinstance(value, str) and value for value in actual
    ):
        message = "Invalid invocation argv"
        raise ValueError(message)
    if attempt["kind"] != "replay_process":
        if actual != [requested[0], "verify", target, "--json"] or requested != actual:
            message = "Verifier command is not its exact derived command"
            raise ValueError(message)
    else:
        expected = requested.copy()
        names = requested[3:-1:2]
        if names.count("--out-dir") != 1:
            message = "Replay lacks its unique frozen output option"
            raise ValueError(message)
        expected[4 + names.index("--out-dir") * 2] = target
        if actual != expected:
            message = "Replay changed more than its owned output destination"
            raise ValueError(message)
    return list(actual)


def _prepare(
    attempt: dict[str, Any],
    slot: dict[str, Any],
    root: Path,
    admission: CaptureAdmission,
    files: dict[str, BinaryIO],
) -> bool:
    phase = "prepare_argv"
    try:
        attempt["argv"] = _argv(attempt, slot)
        before = root.stat(follow_symlinks=False)
        directory = confined(root, slot["planned_directory"])
        confined(root, slot["planned_target"])
        phase = "prepare_directory"
        directory.mkdir(mode=0o700)
        owned = directory.stat(follow_symlinks=False)
        attempt["artifact_directory"] = slot["planned_directory"]
        _directory_volume(before, owned)
        for name in ("stdout", "stderr"):
            phase = "open_" + name
            if not admission.is_open:
                break
            path = _capture_path(root, slot["planned_" + name], directory)
            attempt[name]["path"] = slot["planned_" + name]
            _open_capture_file(path, before, files, name)
        _prepared_namespace(root, directory, before, owned)
    except (OSError, ValueError, TypeError, KeyError, MemoryError) as error:
        note_issue(attempt, "errors." + phase, issue(phase, slot["index"], error), admission)
    except BaseException as error:
        note_issue(
            attempt,
            "errors." + phase,
            issue("unexpected_exception", slot["index"], error),
            admission,
        )
        raise

    return admission.is_open and len(files) == len(("stdout", "stderr"))


def _directory_volume(before: os.stat_result, owned: os.stat_result) -> None:
    if not stat.S_ISDIR(owned.st_mode) or owned.st_dev != before.st_dev:
        message = "Capture directory changed its admitted volume"
        raise ValueError(message)


def _capture_path(root: Path, name: str, directory: Path) -> Path:
    path = confined(root, name)
    if path.parent != directory:
        message = "Capture stream is outside its owned directory"
        raise ValueError(message)
    return path


def _open_capture_file(
    path: Path, before: os.stat_result, files: dict[str, BinaryIO], name: str
) -> None:
    files[name] = path.open("x+b", buffering=0)
    info = os.fstat(files[name].fileno())
    if not stat.S_ISREG(info.st_mode) or info.st_dev != before.st_dev:
        message = "Capture file changed its admitted volume"
        raise ValueError(message)


def _prepared_namespace(
    root: Path, directory: Path, before: os.stat_result, owned: os.stat_result
) -> None:
    after = root.stat(follow_symlinks=False)
    if (before.st_dev, before.st_ino) != (after.st_dev, after.st_ino):
        message = "Source root identity changed during capture preparation"
        raise ValueError(message)
    current = directory.stat(follow_symlinks=False)
    if (owned.st_dev, owned.st_ino) != (current.st_dev, current.st_ino):
        message = "Owned capture directory changed before launch"
        raise ValueError(message)
