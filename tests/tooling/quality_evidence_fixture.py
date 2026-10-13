# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Private byte-identified synthetic corpus and receipts, never natural-image quality evidence."""

from __future__ import annotations

import importlib
import json
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from pathlib import Path

from quality_fixtures import freeze, healthy_plan

from evaluate_document_quality import replay_options
from quality_evidence import admit, digest

continuous_fixtures = importlib.import_module("continuous_fixtures")
REFUSAL_EXIT = 4


def write_reference(root: Path, name: str, value: object) -> dict[str, str]:
    """Record exact ordinary bytes, including readable independent receipt fixtures."""
    path = root / name
    data = value if isinstance(value, bytes) else json.dumps(value).encode()
    path.write_bytes(data)
    return {"path": name, "sha256": digest(path)}


def disk_plan(root: Path, count: int = 2) -> tuple[dict[str, Any], Path]:
    """Exercise real admission with distinct valid PNG sources, masks and 3x3 PSF assets."""
    plan = healthy_plan(count, natural=False)
    executable = root / "binary"
    executable.write_bytes(b"selected executable fixture")
    protocol = plan["protocol"]
    protocol["executable_sha256"] = digest(executable)
    protocol["build_provenance"] = write_reference(
        root,
        "build.json",
        {
            "source_commit": protocol["source_commit"],
            "executable_sha256": protocol["executable_sha256"],
            "build": {},
        },
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
    admit(plan, root, executable, replay_options)
    return plan, executable


def refusal(
    root: Path,
    command: list[str],
    name: str,
    *,
    exit_code: int = 4,
    publication: str = "not_started",
) -> dict[str, Any]:
    """Retain actual raw response bytes; synthetic statuses model known CLI refusals."""
    response = {
        "schema_version": 14,
        "command": "process",
        "version": "0.7.0",
        "exit_code": exit_code,
        "publication": publication,
        "error": {
            "code": "E_METHOD_INAPPLICABLE" if exit_code == REFUSAL_EXIT else "E_OUTPUT_VERIFY",
            "message": "synthetic controlled refusal",
        },
    }
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
