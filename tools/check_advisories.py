#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Query OSV for verified locked sources; fail on unreviewed matches or incomplete evidence."""

from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import os
import re
import sys
from datetime import UTC, datetime
from http import HTTPStatus
from pathlib import Path
from typing import Any

from advisory_review import reviewed
from deps import ROOT, Dependency, load_lock, verify

API_HOST = "api.osv.dev"
MAX_RESPONSE_BYTES = 32 * 1024 * 1024
MAX_PAGES = 16
TIMEOUT_SECONDS = 30


def request(path: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
    """One bounded HTTPS observation; failures do not become empty results or retries."""
    body = None if payload is None else json.dumps(payload).encode("utf-8")
    connection = http.client.HTTPSConnection(API_HOST, timeout=TIMEOUT_SECONDS)
    try:
        connection.request(
            "GET" if payload is None else "POST",
            "/v1" + path,
            body=body,
            headers={"Content-Type": "application/json"},
        )
        response = connection.getresponse()
        if response.status != HTTPStatus.OK:
            message = f"OSV observation returned HTTP {response.status}"
            raise OSError(message)
        raw = response.read(MAX_RESPONSE_BYTES + 1)
    finally:
        connection.close()
    if len(raw) > MAX_RESPONSE_BYTES:
        message = "OSV response exceeds the admitted size"
        raise ValueError(message)
    data: object = json.loads(raw.decode("utf-8"))
    if not isinstance(data, dict):
        message = "OSV response is not an object"
        raise TypeError(message)
    return data


def matches(payload: dict[str, Any]) -> list[dict[str, Any]]:
    """Follow complete bounded pagination, rejecting malformed and repeated results."""
    results: list[dict[str, Any]] = []
    tokens: set[str] = set()
    identifiers: set[str] = set()
    query = dict(payload)
    for _ in range(MAX_PAGES):
        response = request("/query", query)
        if set(response) - {"vulns", "next_page_token"}:
            message = "OSV returned an unsupported response envelope"
            raise ValueError(message)
        values = response.get("vulns", [])
        if not isinstance(values, list):
            message = "OSV match collection is malformed"
            raise TypeError(message)
        for value in values:
            if not isinstance(value, dict) or not isinstance(value.get("id"), str):
                message = "OSV match identity is malformed"
                raise TypeError(message)
            identifier = value["id"]
            if (
                not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}", identifier)
                or identifier in identifiers
            ):
                message = "OSV match identity is invalid or repeated"
                raise ValueError(message)
            identifiers.add(identifier)
            results.append(value)
        token = response.get("next_page_token")
        if token is None:
            return results
        if not isinstance(token, str) or not token or token in tokens:
            message = "OSV pagination is invalid or repeated"
            raise ValueError(message)
        tokens.add(token)
        query["page_token"] = token
    message = "OSV pagination exceeds the admitted page limit"
    raise ValueError(message)


def source_query(dependency: Dependency, cache: Path) -> dict[str, Any]:
    """Peel verified Git objects; archive TIFF has explicitly narrower version-query coverage."""
    receipt = verify(dependency, cache)
    if dependency["transport"] == "git":
        commit = receipt["resolved_commit"]
        if not isinstance(commit, str) or re.fullmatch(r"[0-9a-f]{40}", commit) is None:
            message = "Verified source has no exact Git-commit query identity"
            raise ValueError(message)
        return {"commit": commit}
    if dependency["name"] != "tiff":
        message = "No reviewed archive advisory query identity"
        raise ValueError(message)
    return {
        "package": {"name": "libtiff", "ecosystem": "OSS-Fuzz"},
        "version": dependency["version"],
    }


def scan(cache: Path, report_path: Path | None = None) -> bool:
    """Retain a complete or explicitly incomplete source-bound advisory observation."""
    features = (ROOT / "deps/features.json").read_bytes()
    locked = (ROOT / "deps/lock.json").read_bytes()
    report: dict[str, Any] = {
        "schema_version": 1,
        "observed_at": datetime.now(UTC).isoformat(),
        "lock_sha256": hashlib.sha256(locked).hexdigest(),
        "features_sha256": hashlib.sha256(features).hexdigest(),
        "status": "incomplete",
        "sources": [],
    }
    unreviewed = False

    def persist() -> None:
        if report_path is None:
            return
        report_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = report_path.with_suffix(".tmp")
        temporary.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        temporary.replace(report_path)

    try:
        for dependency in load_lock(ROOT / "deps/lock.json")["dependencies"]:
            observation: dict[str, Any] = {
                "name": dependency["name"],
                "pinned_identity": dependency.get("object", dependency.get("digest")),
                "findings": [],
            }
            report["sources"].append(observation)
            persist()
            query = source_query(dependency, cache)
            observation["query"] = query
            findings = matches(query)
            print(
                f"{dependency['name']}: {json.dumps(query, sort_keys=True)}; "
                f"{len(findings)} matches",
                flush=True,
            )
            for advisory in findings:
                accepted = reviewed(dependency, advisory, features)
                verdict = (
                    "reviewed exclusion from configured build" if accepted else "REVIEW REQUIRED"
                )
                canonical = json.dumps(
                    {key: value for key, value in advisory.items() if key != "modified"},
                    sort_keys=True,
                    separators=(",", ":"),
                ).encode("utf-8")
                observation["findings"].append(
                    {
                        "id": advisory["id"],
                        "summary": advisory.get("summary", ""),
                        "modified": advisory.get("modified"),
                        "evidence_sha256": hashlib.sha256(canonical).hexdigest(),
                        "reviewed_exclusion": accepted,
                    }
                )
                print(f"  {advisory['id']}: {verdict}; {advisory.get('summary', '')}", flush=True)
                unreviewed |= not accepted
            persist()
        report["status"] = "review_required" if unreviewed else "reviewed_or_empty"
        print(
            "OSV database observations only; empty results do not prove absence of vulnerabilities."
        )
        print("TIFF archive coverage uses OSS-Fuzz package version, not an exact Git-commit query.")
    except (OSError, ValueError, TypeError, RuntimeError, http.client.HTTPException) as error:
        report["status"] = "observation_failed"
        report["error"] = str(error)
        raise
    else:
        return not unreviewed
    finally:
        persist()
        if summary := os.getenv("GITHUB_STEP_SUMMARY"):
            with Path(summary).open("a", encoding="utf-8") as output:
                output.write(
                    f"### Locked-source advisory observation\n\n"
                    f"Status: **{report['status']}**; "
                    f"sources visited: {len(report['sources'])}. "
                    "A reviewed exclusion is not a repaired upstream source.\n"
                )


def main() -> int:
    """Network monitoring is development/CI tooling; product execution remains offline."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=ROOT / ".cache/deps")
    parser.add_argument("--report", type=Path, help="Atomically retain full or partial observation")
    args = parser.parse_args()
    try:
        return 0 if scan(args.cache, args.report) else 1
    except (OSError, ValueError, TypeError, RuntimeError, http.client.HTTPException) as error:
        print(f"Advisory observation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
