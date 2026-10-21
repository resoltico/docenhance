# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Exact frozen invocation index and finite own logical-write admission for one quality run."""

from __future__ import annotations

import shutil
import stat
import uuid
from typing import TYPE_CHECKING, Any

from quality_acquisition import confined
from quality_attempt import ATTEMPT_KINDS, MAX_RESPONSE_BYTES
from quality_protocol import BUNDLE_ROLES
from quality_provenance import provenance_physical_paths, provenance_references
from quality_references import referenced_order

if TYPE_CHECKING:
    from pathlib import Path


ARTIFACT_BYTES = 256 * 1024 * 1024
RECORD_BYTES = 256 * 1024
PSF_BYTES = 1024 * 1024


def invocation_index(plan: dict[str, Any]) -> list[dict[str, Any]]:
    """Choose names in memory only; never create a directory while planning/J is unadmitted."""
    policies = {page["sample"]: page for page in plan["protocol"]["pages"]}
    nonce = uuid.uuid4().hex
    slots = []
    for page_index, assessment in enumerate(plan["assessments"]):
        policy = policies[assessment["sample"]]
        for offset, role in enumerate(BUNDLE_ROLES):
            role_index = 3 * page_index + offset
            command = policy["commands"][role]
            process_directory = f"quality-run-{nonce}-{3 * role_index + 1}"
            replay_target = process_directory + "/candidate"
            for kind_offset, kind in enumerate(ATTEMPT_KINDS):
                index = 3 * role_index + kind_offset
                directory = f"quality-run-{nonce}-{index}"
                target = assessment[role]["bundle"] if kind == "scored_verify" else replay_target
                if kind == "replay_process":
                    requested = command.copy()
                    argv = command.copy()
                    output_index = command[3:-1:2].index("--out-dir") * 2 + 4
                    argv[output_index] = target
                else:
                    requested = (
                        [command[0], "verify", target, "--json"] if target is not None else None
                    )
                    argv = requested.copy() if requested is not None else None
                slots.append(
                    {
                        "index": index,
                        "role_index": role_index,
                        "kind": kind,
                        "source_slot": "scored" if kind == "scored_verify" else "replay",
                        "requested_argv": requested,
                        "argv": argv,
                        "planned_directory": directory,
                        "planned_target": target,
                        "planned_stdout": directory + "/stdout",
                        "planned_stderr": directory + "/stderr",
                    }
                )
    return slots


def payload_allowance(command: list[str]) -> int:
    """Current admitted role envelopes; one RAM encode, one stage and same-directory rename."""
    options = dict(zip(command[3:-1:2], command[4:-1:2], strict=True))
    return (
        ARTIFACT_BYTES
        + RECORD_BYTES
        + (ARTIFACT_BYTES if "--protect-mask" in options else 0)
        + (PSF_BYTES if "--psf-file" in options else 0)
    )


def planned_writes(plan: dict[str, Any], control_bytes: int) -> dict[str, int]:
    """Count all potentially eligible attempts, including failure retention and skipped metadata."""
    policies = {page["sample"]: page for page in plan["protocol"]["pages"]}
    eligible = [
        (assessment, role)
        for assessment in plan["assessments"]
        for role in BUNDLE_ROLES
        if assessment[role]["outcome"] == "completed"
    ]
    payload = sum(
        payload_allowance(policies[page["sample"]]["commands"][role]) for page, role in eligible
    )
    selected = [set(policies[page["sample"]]["commands"][role][3:-1:2]) for page, role in eligible]
    # Each eligible role may launch scored verify, process replay, then replay verify.
    raw = 6 * MAX_RESPONSE_BYTES * len(eligible)
    return {
        "corpus_bytes": payload + raw,
        "control_bytes": control_bytes,
        "invocation_directories": 3 * len(eligible),
        "raw_files": 6 * len(eligible),
        "product_payload_sets": len(eligible),
        "product_stage_directories": len(eligible),
        "product_asset_directories": sum(
            bool(options & {"--protect-mask", "--psf-file"}) for options in selected
        ),
        "product_files": sum(
            2 + ("--protect-mask" in options) + ("--psf-file" in options) for options in selected
        ),
    }


def directory_identity(path: Path) -> tuple[int, int]:
    """Observe the actual directory namespace/volume; a later change cannot move its ledger."""
    observed = path.stat(follow_symlinks=False)
    if not stat.S_ISDIR(observed.st_mode):
        message = "Quality storage owner requires an existing directory"
        raise ValueError(message)
    return observed.st_dev, observed.st_ino


class VolumeAdmission:
    """Non-reserving capacity observations plus stable owners; never a physical quota promise."""

    def __init__(
        self, root: Path, output: Path, writes: dict[str, int], intended: dict[str, Any]
    ) -> None:
        """Prepare this actual owner and all retained state before its effects."""
        self.root = root
        self.parent = output.parent
        self.root_identity = directory_identity(root)
        self.output_identity = directory_identity(self.parent)
        corpus = writes["corpus_bytes"]
        control = writes["control_bytes"]
        self.required = {self.root_identity[0]: corpus}
        self.required[self.output_identity[0]] = (
            self.required.get(self.output_identity[0], 0) + control
        )
        for directory in (root, self.parent):
            if shutil.disk_usage(directory).free < self.required[directory.stat().st_dev]:
                message = "Insufficient observed free space for planned quality logical writes"
                raise OSError(message)
        self.existing = self._existing(intended)
        self.check()

    def _existing(self, intended: dict[str, Any]) -> dict[int, dict[str, Any]]:
        """Retain earlier work without evicting or subtracting it twice from free bytes."""
        result: dict[int, dict[str, Any]] = {}
        plan = intended["plan"]
        try:
            provenance = provenance_references(intended)
            implementation = provenance_physical_paths(intended)
        except (ValueError, TypeError):
            # This malformed receipt cannot admit a later nested-file acquisition.
            provenance = []
            implementation = []
        names = {sample["path"] for sample in plan["protocol"]["corpus"]}
        names.update(ref["path"] for page in plan["protocol"]["pages"] for ref in page["assets"])
        names.update(ref["path"] for ref in referenced_order(plan))
        names.add(plan["protocol"]["review_protocol"]["path"])
        names.update(reference["path"] for reference in provenance)
        for assessment in plan["assessments"]:
            names.update(
                assessment[role]["bundle"]
                for role in BUNDLE_ROLES
                if assessment[role]["bundle"] is not None
            )
            names.update(
                assessment[role]["execution"][stream]["path"]
                for role in BUNDLE_ROLES
                if assessment[role]["execution"] is not None
                for stream in ("stdout", "stderr")
            )
        paths = [
            *self.root.glob("quality-replay-*"),
            *self.root.glob("quality-run-*"),
            *(confined(self.root, name) for name in sorted(names)),
            *implementation,
        ]
        visited: set[tuple[int, int]] = set()
        for path in paths:
            if path.is_symlink():
                message = "Retained quality evidence redirects through a symbolic link"
                raise ValueError(message)
            files = path.rglob("*") if path.is_dir() else (path,)
            for item in files:
                if item.is_symlink():
                    message = "Retained quality evidence contains a symbolic link"
                    raise ValueError(message)
                if item.is_file():
                    observed = item.stat()
                    identity = observed.st_dev, observed.st_ino
                    if identity not in visited:
                        visited.add(identity)
                        volume = result.setdefault(
                            observed.st_dev, {"bytes": 0, "files": 0, "roots": []}
                        )
                        volume["bytes"] += observed.st_size
                        volume["files"] += 1
                        if str(path) not in volume["roots"]:
                            volume["roots"].append(str(path))
        return result

    def check(self) -> None:
        """Recheck this actual admitted storage namespace."""
        if (
            directory_identity(self.root) != self.root_identity
            or directory_identity(self.parent) != self.output_identity
        ):
            message = "Quality storage namespace or target volume changed"
            raise OSError(message)
