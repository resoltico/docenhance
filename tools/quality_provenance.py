# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""One existing build receipt owns selected capture evidence and the fixed child environment."""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass
from pathlib import Path, PureWindowsPath
from types import MappingProxyType
from typing import TYPE_CHECKING, Any

from quality_native_body import nested_references as native_references
from quality_native_body import physical_references as native_files
from quality_native_evidence import admit_native
from quality_provenance_contract import capture_references, environment_shape, provenance_shape
from quality_runtime_evidence import admit_runtime
from quality_runtime_evidence import nested_references as runtime_references
from quality_runtime_evidence import physical_references as runtime_files

if TYPE_CHECKING:
    from collections.abc import Mapping

    from quality_inputs import FrozenEvidence
    from quality_native_evidence import NativeAdmission
    from quality_runtime_evidence import RuntimeAdmission


def child_environment() -> Mapping[str, str]:
    """Choose once; pass this exact immutable mapping to every child without later ambient reads."""
    if os.name == "posix":
        return MappingProxyType({})
    if os.name != "nt":
        message = "Unsupported capture platform"
        raise ValueError(message)
    selected = os.environ.get("SYSTEMROOT")
    environment_shape({"SystemRoot": selected})
    if not isinstance(selected, str):
        message = "Windows capture environment requires an absolute selected SystemRoot"
        raise TypeError(message)
    environment = {"SystemRoot": selected}
    if not Path(environment["SystemRoot"]).is_dir():
        message = "Selected Windows SystemRoot is unavailable"
        raise ValueError(message)
    return MappingProxyType(environment)


def provenance_references(intended: dict[str, Any]) -> list[dict[str, Any]]:
    """Inventory already acquired evidence; never reopen a receipt to discover its file owners."""
    entries = intended["referenced_evidence"]
    values = {
        (entry["reference"]["path"], entry["reference"]["sha256"]): entry["value"]
        for entry in entries
    }
    selected = intended["plan"]["protocol"]["build_provenance"]
    receipt = values[(selected["path"], selected["sha256"])]
    references = capture_references(receipt)
    result = list(references)
    for reference, nested in zip(references, (native_references, runtime_references), strict=True):
        body = values.get((reference["path"], reference["sha256"]))
        if not isinstance(body, dict):
            continue
        try:
            result.extend(nested(body))
        except (ValueError, TypeError, KeyError):
            # An invalid pending component cannot admit acquisition of these nested files.
            continue
    return result


def provenance_physical_paths(intended: dict[str, Any]) -> list[Path]:
    """Implementation files are observed absolute owners, never corpus output destinations."""
    values = {
        (entry["reference"]["path"], entry["reference"]["sha256"]): entry["value"]
        for entry in intended["referenced_evidence"]
    }
    selected = intended["plan"]["protocol"]["build_provenance"]
    receipt = values[(selected["path"], selected["sha256"])]
    result = []
    for reference, files in zip(
        capture_references(receipt), (native_files, runtime_files), strict=True
    ):
        body = values.get((reference["path"], reference["sha256"]))
        if not isinstance(body, dict):
            continue
        try:
            references = files(body)
        except (ValueError, TypeError, KeyError):
            continue
        for item in references:
            path = Path(item["path"])
            if not path.is_absolute():
                message = "Implementation inventory requires actual absolute file owners"
                raise ValueError(message)
            result.append(path)
    return result


@dataclass(frozen=True)
class QualityProvenance:
    """Admitted concrete evidence owners and actual fixed launch data; no proof booleans."""

    native: NativeAdmission
    runtime: RuntimeAdmission
    environment: Mapping[str, str]

    def before_launch(self) -> None:
        """Recheck retained implementation identities before another launch."""
        self.native.before_launch()
        self.runtime.before_launch()


def admit_provenance(
    plan: dict[str, Any], root: Path, executable: Path, snapshots: FrozenEvidence
) -> QualityProvenance:
    """Admit once before J/journal/directory effects, reusing the one acquired reference set."""
    receipt = snapshots.value(plan["protocol"]["build_provenance"])
    provenance_shape(receipt)
    protocol = plan["protocol"]
    if (
        receipt["source_commit"] != protocol["source_commit"]
        or receipt["executable_sha256"] != protocol["executable_sha256"]
        or not executable.is_absolute()
    ):
        message = "Capture provenance identifies another source or executable"
        raise ValueError(message)
    scope = receipt["capture_scope"]
    environment = child_environment()
    if dict(environment) != scope["child_environment"]:
        message = "Selected child environment differs from the frozen capture scope"
        raise ValueError(message)
    runtime = admit_runtime(snapshots.value(scope["interpreter"]), root)
    native = admit_native(
        snapshots.value(scope["native_producer"]), plan, root, executable, receipt["build"]
    )
    expected_platform = {"linux": "Linux", "darwin": "Darwin", "win32": "Windows"}.get(sys.platform)
    if expected_platform is None or native.target_platform != expected_platform:
        message = "Native producer platform differs from the actual selected execution platform"
        raise ValueError(message)
    if os.name == "nt":
        directory = native.system_directory
        if directory is None or PureWindowsPath(directory) != (
            PureWindowsPath(environment["SystemRoot"]) / "System32"
        ):
            message = "Native import directory differs from the frozen Windows environment"
            raise ValueError(message)
    return QualityProvenance(native, runtime, environment)
