# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real path, publication, delivery and synchronized interrupt boundaries."""

from __future__ import annotations

import concurrent.futures
import json
import os
import signal
import subprocess
from pathlib import Path
from threading import Barrier
from typing import TYPE_CHECKING

from failure_fixtures import gray_png
from failure_records import bundle_refusals

if TYPE_CHECKING:
    from failure_audit import Audit

SUCCESS = (0, "", "completed")
VERIFIED = (0, "", "")
ARGUMENT = (2, "E_ARGUMENT", "not_started")
INPUT = (3, "E_INPUT", "not_started")
OUTPUT = (5, "E_OUTPUT", "not_started")
OUTPUT_EXIT = 5
CANCELLED_EXIT = 130


def paths(audit: Audit, source: Path) -> None:
    """Preserve exact admitted spelling, and refuse invalid byte identities before effects."""
    names = ["Rīga-文書-📄", "decomposed-e\u0301"]
    if os.name == "posix":
        names += [r"literal\backslash", 'line\nquote"']
    for index, name in enumerate(names):
        target = audit.workspace / name
        response = audit.invoke(f"path-valid-{index}", audit.process(source, target), SUCCESS)
        audit.check(
            f"path-spelling-{index}",
            response["output"] == str(target / "result.png") and (target / "result.png").is_file(),
            "Reported UTF-8 spelling and actual result agree",
        )
        audit.invoke(f"path-verify-{index}", ["verify", str(target), "--json"], VERIFIED)
    if os.name == "posix":
        malformed_paths(audit, source)
    audit.invoke(
        "source-is-directory", audit.process(audit.workspace, audit.workspace / "unused"), INPUT
    )
    if os.name == "posix":
        fifo = audit.workspace / "source-fifo"
        vars(os)["mkfifo"](fifo)
        audit.invoke("source-is-fifo", audit.process(fifo, audit.workspace / "unused"), INPUT)
        fifo.unlink()
    linked = audit.workspace / "source-link.png"
    linked.symlink_to(source.name)
    target = audit.workspace / "linked-source"
    audit.invoke("source-leaf-symlink", audit.process(linked, target), SUCCESS)
    audit.invoke("source-link-verify", ["verify", str(target), "--json"], VERIFIED)


def malformed_paths(audit: Audit, source: Path) -> None:
    """Native POSIX argv bytes cannot acquire repaired or normalized path identities."""
    invalid_output = os.fsencode(audit.workspace) + b"/bad-\xff"
    invalid_input = os.fsencode(audit.workspace) + b"/bad-\x80.png"
    before = set(audit.workspace.iterdir())
    audit.invoke("path-invalid-output", audit.process(source, invalid_output), ARGUMENT)
    audit.invoke(
        "path-invalid-input", audit.process(invalid_input, audit.workspace / "unused"), ARGUMENT
    )
    audit.invoke("argument-invalid-utf8", ["version", b"--bad-\xff", "--json"], ARGUMENT)
    audit.check(
        "path-invalid-effects", set(audit.workspace.iterdir()) == before, "No filesystem effects"
    )


def publication(audit: Audit, source: Path) -> None:
    """Existing foreign objects and staging occupants must retain identity and contents."""
    directory, file = audit.workspace / "existing-directory", audit.workspace / "existing-file"
    directory.mkdir()
    (directory / "owner").write_bytes(b"foreign directory")
    file.write_bytes(b"foreign file")
    for target in (directory, file, source):
        audit.invoke(f"existing-{target.name}", audit.process(source, target), OUTPUT)
    audit.check(
        "existing-effects",
        (directory / "owner").read_bytes() == b"foreign directory"
        and file.read_bytes() == b"foreign file"
        and source.read_bytes() == gray_png(),
        "Foreign destination objects and source bytes preserved",
    )
    audit.invoke(
        "missing-parent", audit.process(source, audit.workspace / "absent" / "output"), OUTPUT
    )
    dangling = audit.workspace / "dangling-destination"
    dangling.symlink_to("missing-object")
    audit.invoke("existing-dangling-link", audit.process(source, dangling), OUTPUT)
    audit.check(
        "dangling-link-effects",
        dangling.is_symlink() and dangling.readlink() == Path("missing-object"),
        "Dangling destination link remains intact",
    )
    target = audit.workspace / "foreign-stage"
    occupied = audit.workspace / "foreign-stage.staging-0"
    occupied.mkdir()
    (occupied / "owner").write_bytes(b"foreign staging")
    audit.invoke("foreign-stage", audit.process(source, target), SUCCESS)
    audit.check(
        "foreign-stage-effects",
        (occupied / "owner").read_bytes() == b"foreign staging",
        "Occupied staging directory was neither adopted nor deleted",
    )


def contention(audit: Audit, source: Path) -> None:
    """Eight barrier-released publishers produce exactly one complete destination."""
    participants = 8
    barrier = Barrier(participants)
    target = audit.workspace / "contended"

    def publish(index: int) -> dict[str, object]:
        barrier.wait(timeout=10)
        command = [str(audit.executable), *map(os.fsdecode, audit.process(source, target))]
        result = subprocess.run(command, capture_output=True, check=False, timeout=30)
        audit.save_output(f"contender-{index}", result.stdout, result.stderr)
        response: dict[str, object] = json.loads(result.stdout)
        valid = result.returncode in (0, OUTPUT_EXIT) and not result.stderr
        valid &= response["exit_code"] == result.returncode
        audit.check(
            f"contender-{index}", valid, "Publisher succeeded or refused without stream noise"
        )
        return response

    with concurrent.futures.ThreadPoolExecutor(max_workers=participants) as workers:
        responses = list(workers.map(publish, range(participants)))
    winners = sum(response["exit_code"] == 0 for response in responses)
    audit.check(
        "contention-effects",
        winners == 1
        and not list(audit.workspace.glob("contended.staging-*"))
        and source.read_bytes() == gray_png(),
        "Exactly one winner; losing owned stages cleaned; source preserved",
    )
    audit.invoke("contention-verify", ["verify", str(target), "--json"], VERIFIED)


def delivery(audit: Audit, source: Path) -> None:
    """A consumer disappearing cannot relabel committed image processing as a safe retry."""
    reader, writer = os.pipe()
    os.close(reader)
    target = audit.workspace / "closed-pipe-result"
    try:
        result = subprocess.run(
            [str(audit.executable), *map(os.fsdecode, audit.process(source, target))],
            stdout=writer,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
        )
    finally:
        os.close(writer)
    audit.save_output("closed-response-pipe", b"", result.stderr)
    audit.check(
        "closed-response-pipe",
        result.returncode == OUTPUT_EXIT and not result.stderr and target.is_dir(),
        "Process exit 5 with committed destination and no second diagnostic response",
    )
    audit.invoke("closed-response-pipe-verify", ["verify", str(target), "--json"], VERIFIED)


def interrupts(audit: Audit, driver: Path) -> None:
    """Use the native production bridge driver readiness handshake, with real OS signals."""
    if os.name != "posix":
        audit.check(
            "interrupt-platform", condition=False, detail="Synchronized probe requires POSIX"
        )
        return
    root = audit.workspace / "interrupts"
    root.mkdir()
    for event in (signal.SIGINT, signal.SIGTERM):
        for verifying in (False, True):
            name = f"native-{event.name}-{'verify' if verifying else 'process'}"
            with subprocess.Popen(
                [str(driver), "--verify" if verifying else str(root / "absent"), str(root / "out")],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            ) as child:
                if child.stdout is None or child.stdout.readline() != b"READY\n":
                    message = "Native interrupt driver did not establish readiness"
                    raise RuntimeError(message)
                os.kill(child.pid, event)
                stdout, stderr = child.communicate(b"\n", timeout=15)
            response = json.loads(stdout)
            audit.save_output(name, stdout, stderr)
            audit.check(
                name,
                child.returncode == CANCELLED_EXIT
                and response["error"]["code"] == "E_CANCELLED"
                and response["publication"] == "not_started"
                and not stderr
                and not list(root.iterdir()),
                f"Driver {driver}: readiness then native {event.name}; no pre-dispatch effects",
            )


def run_boundaries(audit: Audit, interrupt_driver: Path | None) -> None:
    """Run distinct real-system boundaries and retain their reproduction artifacts."""
    source = audit.workspace / "source.png"
    source.write_bytes(gray_png())
    paths(audit, source)
    publication(audit, source)
    contention(audit, source)
    delivery(audit, source)
    bundle_refusals(audit, source)
    if interrupt_driver is not None:
        interrupts(audit, interrupt_driver.resolve(strict=True))
    audit.check("source-final", source.read_bytes() == gray_png(), "Original source unchanged")
