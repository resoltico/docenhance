# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Private byte-identified synthetic corpus and receipts, never natural-image quality evidence."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import sys
from typing import TYPE_CHECKING, Any

from tools_path import ROOT

from quality_fixtures import freeze, healthy_plan

from project_version import project_version
from quality_acquisition import fingerprint
from quality_attempt import delivered_path
from quality_evidence import QualityInputs, admit, digest, replay_options
from quality_inputs import FrozenEvidence
from quality_provenance import child_environment

if TYPE_CHECKING:
    from pathlib import Path
    from types import ModuleType


def png_fixture_module() -> ModuleType:
    """Load the actual CLI PNG fixture owner independently of discovery's ambient path."""
    spec = importlib.util.spec_from_file_location(
        "docenhance_quality_png_fixture", ROOT / "tests/cli/continuous_fixtures.py"
    )
    if spec is None or spec.loader is None:
        message = "The declared CLI PNG fixture is unavailable"
        raise ImportError(message)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


continuous_fixtures = png_fixture_module()
REFUSAL_EXIT = 4


def write_reference(root: Path, name: str, value: object) -> dict[str, str]:
    """Record exact ordinary bytes, including readable independent receipt fixtures."""
    path = root / name
    data = value if isinstance(value, bytes) else json.dumps(value).encode()
    path.write_bytes(data)
    return {"path": name, "sha256": digest(path)}


def synthetic_build() -> dict[str, str]:
    """Identify synthetic receipt values without qualifying a native build."""
    return {
        "application_version": project_version(),
        "platform": "synthetic",
        "compiler": "synthetic",
        "dependency_lock_sha256": fingerprint(ROOT / "deps/lock.json")["sha256"],
    }


def pending_provenance(root: Path, protocol: dict[str, Any]) -> dict[str, Any]:
    """Real byte references exercise accounting; missing producer facts refuse launch."""
    return {
        "source_commit": protocol["source_commit"],
        "executable_sha256": protocol["executable_sha256"],
        "build": synthetic_build(),
        "capture_scope": {
            "native_producer": write_reference(root, "native-producer.json", {}),
            "interpreter": write_reference(root, "interpreter.json", {}),
            "child_environment": dict(child_environment()),
        },
    }


def disk_plan(root: Path, count: int = 2) -> tuple[dict[str, Any], Path]:
    """Admit real fixture assets/receipts; native/interpreter producer premises remain pending."""
    plan = healthy_plan(count, natural=False)
    executable = root / "binary"
    executable.write_bytes(b"selected executable fixture")
    protocol = plan["protocol"]
    protocol["executable_sha256"] = digest(executable)
    protocol["build_provenance"] = write_reference(
        root,
        "build.json",
        pending_provenance(root, protocol),
    )
    protocol["review_protocol"] = write_reference(root, "review.txt", b"external reviewer protocol")
    pairs = []
    for sample, page, assessment in zip(
        protocol["corpus"], protocol["pages"], plan["assessments"], strict=True
    ):
        image = continuous_fixtures.Fixture(3, 3, tuple((int(sample["id"]) + i,) for i in range(9)))
        (root / sample["path"]).write_bytes(image.encoded())
        sample["sha256"] = digest(root / sample["path"])
        mask_name, psf_name = f"mask-{sample['id']}.png", f"psf-{sample['id']}.png"
        mask = continuous_fixtures.Fixture(
            3, 3, tuple((255 if i == int(sample["id"]) else 0,) for i in range(9))
        )
        psf = continuous_fixtures.Fixture(3, 3, ((1,),) * 9)
        page["assets"] = [
            write_reference(root, mask_name, mask.encoded()),
            write_reference(root, psf_name, psf.encoded()),
        ]
        for name in ("candidate", "baseline", "replicate"):
            extra = ["--protect-mask", mask_name]
            if name != "baseline":
                extra += ["--deblur", "wiener", "--psf", "kernel", "--psf-file", psf_name]
            command = [
                str(executable),
                "process",
                sample["path"],
                "--out-dir",
                f"{name}-{sample['id']}",
                *extra,
                "--json",
            ]
            page["commands"][name] = command
            assessment[name] = {"outcome": "missing_evidence", "bundle": None, "execution": None}
        assessment["reviews"] = []
        pairs.append(
            {
                "sample": sample["id"],
                "blind_pair": page["blind_pair"],
                "reviewers": [person["id"] for person in protocol["panel"]],
                "candidate_side": "left" if int(sample["id"]) % 2 else "right",
            }
        )
    protocol["assignment"] = write_reference(root, "assignment.json", {"pairs": pairs})
    freeze(plan)
    plan["preregistration"] = write_reference(
        root, "receipt.json", {"protocol_sha256": plan["protocol_sha256"]}
    )
    admit(plan, root, executable, replay_options, FrozenEvidence(plan, root))
    return plan, executable


def component_inputs(plan: dict[str, Any], root: Path, executable: Path) -> QualityInputs:
    """Internal journal/accounting inputs have no execution premises and cannot launch children."""
    return QualityInputs(
        plan, root, executable, FrozenEvidence(plan, root), synthetic_build(), None
    )


def refusal(
    root: Path,
    command: list[str],
    name: str,
    *,
    exit_code: int = 4,
    publication: str = "not_started",
) -> dict[str, Any]:
    """Retain actual raw response bytes; synthetic statuses model known CLI refusals."""
    response = failure_response(exit_code, publication)
    return {
        "outcome": "refused" if exit_code == REFUSAL_EXIT else "failed",
        "bundle": None,
        "execution": {
            "argv": command,
            "stdout": write_reference(root, name + ".stdout", response),
            "stderr": write_reference(root, name + ".stderr", b""),
            "exit_code": exit_code,
            "timed_out": False,
            "response": response,
            "publication": publication,
        },
    }


def failure_response(exit_code: int, publication: str) -> dict[str, Any]:
    """A complete synthetic process failure, including nullable unstarted observations."""
    return {
        "schema_version": 15,
        "command": "process",
        "version": project_version(),
        "exit_code": exit_code,
        "publication": publication,
        "error": {
            "code": "E_METHOD_INAPPLICABLE" if exit_code == REFUSAL_EXIT else "E_OUTPUT_VERIFY",
            "message": "synthetic controlled refusal",
        },
        "failed_stage": None,
        "completed": {},
        "diagnostics": None,
        "warnings": [],
    }


def binary_payloads() -> tuple[bytes, bytes]:
    """Two white samples have independently known fixed-threshold PNG1 output."""
    source = continuous_fixtures.Fixture(2, 1, ((255,), (255,))).encoded()
    output = continuous_fixtures.Fixture(2, 1, ((1,), (1,)), depth=1).encoded()
    return source, output


def binary_record(run: str = "a" * 32, source_name: str = "source") -> dict[str, Any]:
    """Complete synthetic wire15 B03 record; never a native verification receipt."""
    source, output = binary_payloads()
    absent = dict.fromkeys(("illumination", "denoising", "restoration", "contrast", "sharpening"))
    return {
        "record": {
            "format": "docenhance.run",
            "version": 15,
            "run": run,
            "recorded": "2026-10-19T00:00:00Z",
        },
        "build": synthetic_build(),
        "source": {
            "name": source_name,
            "sha256": hashlib.sha256(source).hexdigest(),
            "bytes": len(source),
            "container": {
                "format": "png",
                "decoder_policy": "bounded-png-1",
                "width": 2,
                "height": 1,
                "precision": 8,
                "color_type": 0,
                "interlaced": False,
            },
        },
        "request": {
            **absent,
            "output": {"mode": "bw", "depth": None, "alpha": "white", "profile_policy": "embedded"},
            "rotation": 0,
            "protection": None,
            "binarization": {
                "method": "B03",
                "version": 2,
                "selector": "fixed",
                "parameters": {"threshold": 0.5},
            },
        },
        "execution": {
            **absent,
            "interpretation": {
                "decoded": {"width": 2, "height": 1, "channels": 1, "bit_depth": 8},
                "output": {"width": 2, "height": 1, "channels": 1, "bit_depth": 1},
                "profile_decision": "assumed_srgb",
                "source_orientation": 1,
                "rotation": 0,
                "resolution": None,
                "alpha_composited_pixels": 0,
                "clipped_components": 0,
                "assumed_transfer": True,
                "assumed_primaries": False,
                "depth_reduced": False,
            },
            "binarization": {
                "method": "B03",
                "disposition": "applied",
                "reason": "none",
                "black_samples": 0,
                "white_samples": 2,
            },
        },
        "assets": [],
        "output": {
            "path": "result.png",
            "sha256": hashlib.sha256(output).hexdigest(),
            "bytes": len(output),
            "width": 2,
            "height": 1,
            "channels": 1,
            "depth": 1,
            "profile_embedded": False,
            "resolution": None,
            "verification": "decoded_and_compared",
        },
    }


def successful_response(output: Path, record: dict[str, Any]) -> dict[str, Any]:
    """Write the synthetic bundle and bind the full response to its exact run.json bytes."""
    (output / "result.png").write_bytes(binary_payloads()[1])
    ref = write_reference(output, "run.json", record)
    return {
        "schema_version": 15,
        "command": "process",
        "version": project_version(),
        "exit_code": 0,
        "publication": "completed",
        "output": str(output / "result.png"),
        "record": record,
        "record_sha256": ref["sha256"],
        "diagnostics": {"threads": 1, "memory_limit_mib": 1024, "charged_peak_bytes": 1},
        "warnings": [],
    }


def delivered_candidate(root: Path) -> tuple[dict[str, Any], Path, dict[str, Any]]:
    """One synthetic full response/raw delivery, not native verification or source qualification."""
    plan, executable = disk_plan(root)
    destination = "candidate-0"
    directory = root / destination
    directory.mkdir()
    record = binary_record()
    response = successful_response(directory, record)
    response["output"] = delivered_path(destination, "result.png")
    command = plan["protocol"]["pages"][0]["commands"]["candidate"]
    plan["assessments"][0]["candidate"] = {
        "outcome": "completed",
        "bundle": destination,
        "execution": {
            "argv": command,
            "exit_code": 0,
            "timed_out": False,
            "response": response,
            "publication": "completed",
            "stdout": write_reference(root, "candidate.stdout", response),
            "stderr": write_reference(root, "candidate.stderr", b""),
        },
    }
    return plan, executable, record
