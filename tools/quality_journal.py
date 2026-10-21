# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""One exclusively owned append-only quality JSON stream with a pre-effect byte allowance."""

from __future__ import annotations

import copy
from typing import TYPE_CHECKING, Any, Protocol

from quality_allowance import attempt_allowance, issue_allowance, original_allowance
from quality_attempt import CaptureAdmission, issue
from quality_json import canonical
from quality_report_allowance import report_allowance

if TYPE_CHECKING:
    from collections.abc import Buffer
    from pathlib import Path


class JournalSink(Protocol):
    """Actual journal delivery needs bounded writes, explicit flush and one close."""

    def write(self, data: Buffer, /) -> int | None:
        """Return actual write progress or an unavailable result."""
        ...

    def flush(self) -> None:
        """Deliver the current frame to the selected file handle."""
        ...

    def close(self) -> None:
        """Consume this actual sink once, even when close reports an error."""
        ...


class VolumeCheck(Protocol):
    """The existing storage owner rechecks its admitted namespace before writes."""

    def check(self) -> None:
        """Refuse a changed actual storage namespace."""
        ...


class JournalError(RuntimeError):
    """No further frames or invocations may follow a failed journal delivery."""


def event(kind: str, index: int, observation: dict[str, Any] | None) -> dict[str, Any]:
    """One actual event projection also supplies its count-only null framing."""
    key = "role_index" if kind == "original_observed" else "invocation_index"
    return {"event": kind, key: index, "observation": observation}


def event_allowance(kind: str, index: int, observation_bytes: int) -> int:
    """Count the same frame with one independently bounded observation occurrence."""
    return len(canonical(event(kind, index, None))) - 4 + observation_bytes


def terminal(
    roles: int, invocations: int, count: int, admission: CaptureAdmission
) -> dict[str, Any]:
    """Retain actual final counts and the monotonic admission cause."""
    return {
        "state": "finished",
        "accounted_roles": roles,
        "accounted_invocations": invocations,
        "event_count": count,
        "launch_admission": "open" if admission.is_open else "closed",
        "closure_reason": admission.closure_reason,
    }


class JournalBudget:
    """Charge every duplicate from the same projections before opening the output owner."""

    def __init__(
        self,
        intended: dict[str, Any],
        pages: list[dict[str, Any]],
        errors: dict[str, Any],
        prior: dict[str, Any],
        maximum: int,
    ) -> None:
        """Prepare this actual owner and all retained state before its effects."""
        plan, slots = intended["plan"], intended["invocations"]
        self.originals = {}
        for page in pages:
            for role in page["roles"].values():
                original = role["original"]
                index = original["role_index"]
                self.originals[index] = event_allowance(
                    "original_observed", index, original_allowance(original, canonical)
                )
        self.attempts = {}
        for slot in slots:
            for kind in ("invocation_prepared", "invocation_finished"):
                self.attempts[kind, slot["index"]] = event_allowance(
                    kind, slot["index"], attempt_allowance(slot, canonical)
                )
        self.prefix = (
            b'{"kind":"quality_run","protocol_version":3,'
            b'"evidence_contract":"docenhance-quality-v3","protocol_sha256":'
            + canonical(plan["protocol_sha256"])
            + b',"intended":'
            + canonical(intended)
            + b',"events":['
        )
        self.report = report_allowance(
            plan, pages, errors, prior, {"invocations": slots, "subject_max": maximum}
        )
        sample = terminal(
            len(self.originals),
            len(slots),
            len(self.originals) + 2 * len(slots),
            CaptureAdmission(),
        )
        self.terminal = (
            len(canonical(sample))
            + len(canonical("closed"))
            - len(canonical("open"))
            + max(4, issue_allowance(maximum, canonical))
            - 4
        )
        self.suffix = len(b'],"report":,"terminal":}\n') + self.report + self.terminal
        count = len(self.originals) + len(self.attempts)
        self.total = (
            len(self.prefix)
            + sum(self.originals.values())
            + sum(self.attempts.values())
            + max(0, count - 1)
            + self.suffix
        )


class QualityJournal:
    """Never reopen, truncate, retry delivery or close while a different writer owns this handle."""

    def __init__(
        self,
        output: Path,
        budget: JournalBudget,
        admission: CaptureAdmission,
        errors: dict[str, Any],
        volume: VolumeCheck,
    ) -> None:
        """Prepare this actual owner and all retained state before its effects."""
        self.budget, self.admission, self.errors, self.volume = budget, admission, errors, volume
        self.stream: JournalSink | None = None
        self.retained_sink: JournalSink | None = None
        self.spent = 0
        self.failed = False
        self.events: list[dict[str, Any]] = []
        try:
            self.volume.check()
            self.stream = output.open("xb", buffering=0)
        except (OSError, ValueError, MemoryError) as error:
            self.fail("header_write", error)
            message = "Quality journal could not open"
            raise JournalError(message) from error
        try:
            self.append(budget.prefix, len(budget.prefix), "header")
        except JournalError:
            self.close()
            raise

    def fail(self, key: str, error: BaseException | None = None) -> None:
        """Record this fixed journal failure and close later admission."""
        cause = issue(key, None, error)
        if self.errors[key] is None:
            self.errors[key] = cause
        self.admission.close(cause)
        self.failed = True

    def append(self, data: bytes, allowance: int, phase: str) -> None:
        """Check before any write; a partial write spends its bytes and cannot restart a frame."""
        if self.failed:
            message = "Quality journal delivery already failed"
            raise JournalError(message)
        if len(data) > allowance or self.spent + len(data) > self.budget.total:
            self.fail("serialization_allowance")
            message = "Quality frame exceeds its pre-effect allowance"
            raise JournalError(message)
        try:
            self.volume.check()
            self._write_frame(data, self.stream)
        except (OSError, ValueError, TypeError, MemoryError) as error:
            self.fail(phase + "_write", error)
            message = "Quality journal write failed"
            raise JournalError(message) from error
        try:
            self._flush_frame()
        except (OSError, ValueError, MemoryError) as error:
            self.fail(phase + "_flush", error)
            message = "Quality journal flush failed"
            raise JournalError(message) from error

    def _write_frame(self, data: bytes, stream: JournalSink | None) -> None:
        """Spend each actual partial write once, without restarting a failed frame."""
        if stream is None:
            message = "Quality journal stream is closed"
            raise OSError(message)
        offset = 0
        while offset < len(data):
            written = stream.write(memoryview(data)[offset:])
            if type(written) is not int or not 0 < written <= len(data) - offset:
                message = "Quality journal write progress is unavailable"
                raise OSError(message)
            offset += written
            self.spent += written

    def _flush_frame(self) -> None:
        """Flush the actual retained stream, refusing an already consumed sink."""
        stream = self.stream
        if stream is None:
            message = "Quality journal stream is closed"
            raise OSError(message)
        stream.flush()

    def observe(self, kind: str, index: int, observation: dict[str, Any]) -> None:
        """Append one actual immutable observation to its assigned event frame."""
        try:
            value = event(kind, index, copy.deepcopy(observation))
            encoded = canonical(value)
            delimiter = b"," if self.events else b""
            frame = delimiter + encoded
        except (ValueError, TypeError, MemoryError) as error:
            self.fail("serialization_allowance", error)
            message = "Quality event could not serialize"
            raise JournalError(message) from error
        allowance = (
            self.budget.originals[index]
            if kind == "original_observed"
            else self.budget.attempts[kind, index]
        )
        self.append(frame, len(delimiter) + allowance, "append")
        self.events.append(value)

    def finish(self, report: dict[str, Any], final: dict[str, Any]) -> None:
        """Deliver the final actual projection only after complete accounting."""
        try:
            try:
                encoded_report, encoded_final = canonical(report), canonical(final)
                frame = b'],"report":' + encoded_report + b',"terminal":' + encoded_final + b"}\n"
            except (ValueError, TypeError, MemoryError) as error:
                self.fail("serialization_allowance", error)
                message = "Final quality projection could not serialize"
                raise JournalError(message) from error
            if (
                len(encoded_report) > self.budget.report
                or len(encoded_final) > self.budget.terminal
            ):
                self.fail("serialization_allowance")
                message = "Final quality projection exceeds its charged allowance"
                raise JournalError(message)
            self.append(frame, self.budget.suffix, "final")
        finally:
            self.close()

    def close(self) -> None:
        """One final close; complete-looking retained bytes cannot attest that this succeeded."""
        if self.stream is None:
            return
        stream, self.stream = self.stream, None
        try:
            stream.close()
        except (OSError, ValueError, MemoryError) as error:
            self.retained_sink = stream
            self.fail("final_close", error)
            self.admission.retained_live.append(stream)
            message = "Quality journal close failed"
            raise JournalError(message) from error
