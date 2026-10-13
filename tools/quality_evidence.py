# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Admit frozen assets, provenance and the fixed per-page processing commands."""

from __future__ import annotations

import hashlib
from pathlib import Path
from typing import TYPE_CHECKING, Any

from jsonschema import ValidationError

from document_quality import frozen_digest, read_json
from quality_protocol import BUNDLE_ROLES

if TYPE_CHECKING:
    from collections.abc import Callable


def evidence_error(error: Exception) -> str:
    """Derived schema diagnostics retain path/rule without duplicating evidence or schema."""
    if isinstance(error, ValidationError):
        path = "/" + "/".join(
            str(part).replace("~", "~0").replace("/", "~1") for part in error.absolute_path
        )
        return f"JSON schema refusal at {path} (rule {error.validator})"
    return str(error)


def digest(path: Path) -> str:
    """Hash ordinary bytes in bounded chunks."""
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def confined(root: Path, name: str) -> Path:
    """Evidence identities stay confined and cannot redirect through links."""
    path = root / name
    if (
        Path(name).is_absolute()
        or ".." in Path(name).parts
        or not path.resolve().is_relative_to(root)
    ):
        message = "Evaluation path escapes its corpus root"
        raise ValueError(message)
    if any(part.is_symlink() for part in (path, *path.parents)):
        message = "Evaluation evidence must not redirect through symbolic links"
        raise ValueError(message)
    return path


def reference(root: Path, ref: dict[str, Any]) -> Path:
    """Admission checks bytes independently of the declared receipt or asset contents."""
    path = confined(root, ref["path"])
    if digest(path) != ref["sha256"]:
        message = "Frozen referenced bytes differ: " + ref["path"]
        raise ValueError(message)
    return path


def command_options(command: list[str], source: str, executable: Path) -> tuple[str, list[str]]:
    """A role freezes full argv; replay substitutes only the output destination."""
    if command[:3] != [str(executable), "process", source] or command[-1] != "--json":
        message = "Frozen command must bind selected executable, process source and JSON delivery"
        raise ValueError(message)
    options = command[3:-1]
    names = options[::2]
    if len(options) % 2 or len(set(names)) != len(names) or names.count("--out-dir") != 1:
        message = "Frozen command requires unique option/value pairs and one output directory"
        raise ValueError(message)
    index = names.index("--out-dir") * 2
    return options[index + 1], options[:index] + options[index + 2 :]


def admit(
    plan: dict[str, Any],
    root: Path,
    executable: Path,
    check_options: Callable[[list[str], Path], None],
) -> dict[str, Any]:
    """Refuse drift before assessment; receipt identity stays outside the protocol self-hash."""
    protocol = plan["protocol"]
    if digest(executable) != protocol["executable_sha256"]:
        message = "Executable differs from the reviewed evaluation identity"
        raise ValueError(message)
    receipt = read_json(reference(root, plan["preregistration"]).read_bytes())
    if (
        set(receipt) != {"protocol_sha256"}
        or receipt.get("protocol_sha256") != plan["protocol_sha256"]
    ):
        message = "Independent preregistration receipt references another protocol"
        raise ValueError(message)
    build = read_json(reference(root, protocol["build_provenance"]).read_bytes())
    if (
        set(build) != {"source_commit", "executable_sha256", "build"}
        or build.get("source_commit") != protocol["source_commit"]
        or build.get("executable_sha256") != protocol["executable_sha256"]
        or not isinstance(build.get("build"), dict)
    ):
        message = "Reviewed build provenance differs from frozen source/executable"
        raise ValueError(message)
    reference(root, protocol["review_protocol"])
    admit_reviews(protocol, root)
    samples = {sample["id"]: sample for sample in protocol["corpus"]}
    for sample in samples.values():
        if digest(confined(root, sample["path"])) != sample["sha256"]:
            message = "Source input changed after corpus freezing"
            raise ValueError(message)
    admit_pages(protocol, root, executable, check_options)
    return dict(build["build"])


def admit_pages(
    protocol: dict[str, Any],
    root: Path,
    executable: Path,
    check_options: Callable[[list[str], Path], None],
) -> None:
    """Frozen per-page commands use exactly their byte-identified assets and distinct outputs."""
    samples = {sample["id"]: sample for sample in protocol["corpus"]}
    for page in protocol["pages"]:
        assets = {ref["path"]: ref for ref in page["assets"]}
        if len(assets) != len(page["assets"]):
            message = "Duplicate frozen asset path"
            raise ValueError(message)
        for ref in assets.values():
            reference(root, ref)
        used = set()
        destinations = []
        for command in page["commands"].values():
            destination, options = command_options(
                command, samples[page["sample"]]["path"], executable
            )
            destinations.append(confined(root, destination))
            check_options(options, root)
            for i in range(0, len(options), 2):
                if options[i] in {"--protect-mask", "--psf-file"}:
                    used.add(options[i + 1])
        if used != set(assets) or len(set(destinations)) != len(BUNDLE_ROLES):
            message = "Frozen assets or distinct role destinations disagree with commands"
            raise ValueError(message)


def admit_reviews(protocol: dict[str, Any], root: Path) -> None:
    """Retained assignment and tuning receipts bind reviewed identities, not just file existence."""
    assignment = read_json(reference(root, protocol["assignment"]).read_bytes())
    expected = [
        {
            "sample": page["sample"],
            "blind_pair": page["blind_pair"],
            "reviewers": [person["id"] for person in protocol["panel"]],
        }
        for page in protocol["pages"]
    ]
    pairs = assignment.get("pairs")
    if (
        set(assignment) != {"pairs"}
        or not isinstance(pairs, list)
        or any(
            not isinstance(pair, dict)
            or set(pair) != {"sample", "blind_pair", "reviewers", "candidate_side"}
            or pair["candidate_side"] not in {"left", "right"}
            for pair in pairs
        )
        or [
            {key: value for key, value in pair.items() if key != "candidate_side"} for pair in pairs
        ]
        != expected
    ):
        message = "Blinded assignment differs from frozen pages and panel"
        raise ValueError(message)
    if protocol["tuning"]["receipt"] is not None:
        tuning = read_json(reference(root, protocol["tuning"]["receipt"]).read_bytes())
        inputs = [sample for sample in protocol["corpus"] if sample["split"] == "tuning"]
        if set(tuning) != {"tuning_corpus_sha256"} or tuning[
            "tuning_corpus_sha256"
        ] != frozen_digest(inputs):
            message = "Tuning receipt differs from cleared tuning inputs"
            raise ValueError(message)
