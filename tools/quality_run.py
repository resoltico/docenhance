# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""The actual quality run owns planned work, observation lifetimes and one journal."""

from __future__ import annotations

import copy
from typing import TYPE_CHECKING, Any

from document_quality import validate_run
from quality_attempt import (
    ATTEMPT_KINDS,
    RUN_ERRORS,
    VALIDATION_ERRORS,
    CaptureAdmission,
    issue,
    new_attempt,
    new_original,
    note_issue,
)
from quality_capture import capture_attempt
from quality_journal import JournalBudget, JournalError, QualityJournal, terminal
from quality_observations import original_ready
from quality_protocol import BUNDLE_ROLES
from quality_report import page_report, role_report
from quality_work import VolumeAdmission, invocation_index, planned_writes

if TYPE_CHECKING:
    from pathlib import Path

    from quality_evidence import QualityInputs


class QualityRun:
    """Attach every mutable fact before effects; finished snapshots are never rewritten."""

    def __init__(self, inputs: QualityInputs, output: Path) -> None:
        """Prepare this actual owner and all retained state before its effects."""
        plan, root, executable = inputs.plan, inputs.root, inputs.executable
        self.plan, self.root, self.executable = plan, root, executable
        self.provenance = inputs.provenance
        self.admission = CaptureAdmission(
            environment=(self.provenance.environment if self.provenance is not None else None)
        )
        self.errors = dict.fromkeys(RUN_ERRORS)
        self.snapshots = inputs.snapshots
        self.slots = invocation_index(plan)
        self.attempts = [new_attempt(slot) for slot in self.slots]
        self.pages = self._pages()
        self.originals: dict[int, Any] = {}
        self.finished: dict[int, Any] = {}
        self.journal: QualityJournal
        if self.snapshots.failures:
            self.reject("referenced_evidence", next(iter(self.snapshots.failures.values())))
        self.intended = {
            "plan": copy.deepcopy(plan),
            "referenced_evidence": self.snapshots.entries,
            "invocations": self.slots,
        }
        maximum = max(len(self.slots) - 1, len(self.snapshots.entries) - 1, 0)
        prior = {
            sample: [review for revision in revisions for review in revision["reviews"]]
            for sample, revisions in self.snapshots.prior.items()
        }
        self.budget = JournalBudget(self.intended, self.pages, self.errors, prior, maximum)
        try:
            self.volume = VolumeAdmission(
                root, output, planned_writes(plan, self.budget.total), self.intended
            )
        except (OSError, ValueError, MemoryError) as error:
            self.reject("storage_admission", issue("storage_admission", None, error))
            raise
        self.journal = QualityJournal(output, self.budget, self.admission, self.errors, self.volume)

    def _pages(self) -> list[dict[str, Any]]:
        policies = {page["sample"]: page for page in self.plan["protocol"]["pages"]}
        samples = {sample["id"]: sample for sample in self.plan["protocol"]["corpus"]}
        pages = []
        for position, assessment in enumerate(self.plan["assessments"]):
            roles = {}
            for offset, name in enumerate(BUNDLE_ROLES):
                index = 3 * position + offset
                observations = {"original": new_original(index, assessment[name])}
                observations.update(
                    {
                        kind: self.attempts[3 * index + step]
                        for step, kind in enumerate(ATTEMPT_KINDS)
                    }
                )
                roles[name] = role_report(
                    assessment[name],
                    observations,
                    dict.fromkeys(VALIDATION_ERRORS),
                    None,
                    assessment[name]["outcome"],
                )
            validation = dict.fromkeys(VALIDATION_ERRORS)
            sample = assessment["sample"]
            if sample in self.snapshots.prior_failures:
                validation["prior_reviews"] = self.snapshots.prior_failures[sample]
                for role in roles.values():
                    role["outcome"] = "missing_evidence"
            policy = policies[sample] | {"source": samples[sample]}
            pages.append(
                page_report(policy, assessment, roles, validation, self.snapshots.prior[sample])
            )
        return pages

    def reject(self, key: str, cause: dict[str, Any]) -> None:
        """Retain the first bounded cause and close later launch admission."""
        if self.errors[key] is None:
            self.errors[key] = cause
        self.admission.close(cause)

    def original(self, observation: dict[str, Any]) -> None:
        """Persist this original observation once without rewriting its delivery."""
        index = observation["role_index"]
        if index in self.originals:
            message = "Original observation was already persisted"
            raise JournalError(message)
        self.journal.observe("original_observed", index, observation)
        self.originals[index] = copy.deepcopy(observation)

    def _eligible(self, index: int) -> bool:
        slot = self.slots[index]
        original = self.originals.get(slot["role_index"])
        if original is None:
            message = "Invocation preceded its original observation"
            raise JournalError(message)
        if slot["kind"] == "scored_verify":
            return original_ready(original)
        return index - 1 in self.finished and self.finished[index - 1]["outcome"] == "completed"

    def _launch_ready(self, index: int) -> bool:
        """Observe the actual volume and retained provenance again before a launch."""
        try:
            self.volume.check()
        except (OSError, ValueError, MemoryError) as error:
            self.reject("storage_admission", issue("storage_admission", index, error))
            return False
        provenance = self.provenance
        if provenance is None:
            self.reject("no_child_admission", issue("no_child_admission", index))
            return False
        try:
            provenance.before_launch()
        except (OSError, ValueError, TypeError, MemoryError) as error:
            self.reject("no_child_admission", issue("no_child_admission", index, error))
        return self.admission.is_open

    def invoke(self, index: int) -> dict[str, Any]:
        """Persist intent before directory creation and FINISHED before dependent invocation."""
        if index in self.finished:
            message = "Invocation was already consumed"
            raise JournalError(message)
        attempt = self.attempts[index]
        eligible = self._eligible(index)
        if self.admission.is_open and eligible and self.provenance is None:
            self.reject("no_child_admission", issue("no_child_admission", index))
        self._persist("invocation_prepared", index, attempt)
        if self.admission.is_open and eligible:
            if not self._launch_ready(index):
                self._persist("invocation_finished", index, attempt)
                self.finished[index] = copy.deepcopy(attempt)
                return attempt
            capture_attempt(attempt, self.slots[index], self.root, self.admission)
            if self.provenance is not None and attempt["direct_join"] == "confirmed":
                attempt["descendant_cleanup"] = "not_applicable"
            if attempt["outcome"] != "completed":
                self.admission.close(issue("delivery", index))
        self._persist("invocation_finished", index, attempt)
        self.finished[index] = copy.deepcopy(attempt)
        return attempt

    def skip(self, index: int, causecode: str = "not_eligible") -> dict[str, Any]:
        """An expected skip has no fake child/exit or actual directory and consumes its slot."""
        if causecode != "not_eligible":
            self.admission.close(issue(causecode, index))
        if index in self.finished:
            message = "Invocation was already consumed"
            raise JournalError(message)
        if self.slots[index]["role_index"] not in self.originals:
            message = "Skipped invocation preceded its original observation"
            raise JournalError(message)
        attempt = self.attempts[index]
        self._persist("invocation_prepared", index, attempt)
        self._persist("invocation_finished", index, attempt)
        self.finished[index] = copy.deepcopy(attempt)
        return attempt

    def _persist(self, kind: str, index: int, attempt: dict[str, Any]) -> None:
        try:
            self.journal.observe(kind, index, attempt)
        except JournalError:
            prepared = kind == "invocation_prepared"
            slot = "errors.prepared_persist" if prepared else "cleanup_errors.finished_persist"
            code = "prepared_persist" if prepared else "finished_persist"
            note_issue(attempt, slot, issue(code, index), self.admission)
            raise

    def finish(self, report: dict[str, Any]) -> dict[str, Any]:
        """Deliver the final actual projection only after complete accounting."""
        if len(self.originals) != len(self.slots) // 3 or len(self.finished) != len(self.slots):
            message = "Quality accounting did not finish every intended slot"
            raise JournalError(message)
        final = terminal(
            len(self.originals), len(self.finished), len(self.journal.events), self.admission
        )
        value = {
            "kind": "quality_run",
            "protocol_version": 3,
            "evidence_contract": "docenhance-quality-v3",
            "protocol_sha256": self.plan["protocol_sha256"],
            "intended": self.intended,
            "events": self.journal.events,
            "report": report,
            "terminal": final,
        }
        validate_run(value)
        self.journal.finish(report, final)
        return value

    def close(self) -> None:
        """Consume the actual retained journal sink without retrying delivery."""
        self.journal.close()
