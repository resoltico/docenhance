#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Produce source-license inventory and declared-dependency SPDX metadata after verification.

This is a source-package inventory, NOT a binary composition scanner or legal clearance.
No dependency license is changed to MIT. Full binary release review remains required.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import uuid
from pathlib import Path
from typing import Any

from deps import ROOT, Dependency, load_lock, verify
from project_version import project_version

LICENSE_PREFIXES = ("LICENSE", "LICENCE", "COPYING", "COPYRIGHT", "NOTICE")
NOTICE_PREAMBLE = [
    "# Third-party source-license inventory",
    "",
    (
        "DocEnhance is copyright 2026 Ervins Strauhmanis and MIT-licensed; bundled dependencies "
        "retain their own licenses."
    ),
    "",
    (
        "This inventory covers locked source dependencies, including test-only material. It is a "
        "superset of potentially linked code, not a final binary composition assessment."
    ),
    "",
    "This software is based in part on the work of the Independent JPEG Group.",
    "",
]


class InventoryError(ValueError):
    """A license path or output location is unsafe."""


def copy_licenses(dep: Dependency, receipt: dict[str, Any], source: Path, out: Path) -> None:
    """Copy declared and discovered license files of one dependency into out/licenses/."""
    candidates = set(dep["license_files"])
    for rel in receipt["files"]:
        if Path(rel).name.upper().startswith(LICENSE_PREFIXES) and (source / rel).is_file():
            candidates.add(rel)
    for rel in sorted(candidates):
        path = source / rel
        if not path.resolve().is_relative_to(source.resolve()):
            msg = "Escaping license path"
            raise InventoryError(msg)
        target = out / "licenses" / dep["name"] / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)


def notice_lines(dep: Dependency) -> list[str]:
    """Return the THIRD_PARTY_NOTICES.md section for one dependency."""
    name = dep["name"]
    return [
        f"## {name} {dep['version']}",
        "",
        f"Declared upstream license: `{dep['license']}`. Scope: `{dep['scope']}`.",
        f"Source: {dep.get('repository', dep.get('url'))}",
        f"Original license files: `licenses/{name}/`. No relicensing is asserted.",
        "",
    ]


def spdx_package(dep: Dependency, receipt: dict[str, Any]) -> dict[str, Any]:
    """Return the SPDX package entry for one dependency."""
    source_info = {
        "ref": dep.get("ref"),
        "object": dep.get("object"),
        "commit": receipt["resolved_commit"],
        "scope": dep["scope"],
    }
    package: dict[str, Any] = {
        "SPDXID": f"SPDXRef-Package-{dep['name']}",
        "name": dep["name"],
        "versionInfo": dep["version"],
        "downloadLocation": dep.get("repository", dep.get("url")),
        "filesAnalyzed": False,
        "licenseConcluded": "NOASSERTION",
        "licenseDeclared": dep["license"],
        "copyrightText": "NOASSERTION",
        "sourceInfo": json.dumps(source_info),
    }
    if dep["transport"] == "archive":
        checksum = {"algorithm": dep["digest_algorithm"].upper(), "checksumValue": dep["digest"]}
        package["checksums"] = [checksum]
    return package


def spdx_document(version: str, lock_hash: str, packages: list[dict[str, Any]]) -> dict[str, Any]:
    """Return the SPDX 2.3 document describing every package."""
    namespace = str(uuid.uuid5(uuid.NAMESPACE_URL, lock_hash))
    return {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"DocEnhance {version} declared source dependency inventory",
        "documentNamespace": f"https://spdx.org/spdxdocs/docenhance-{namespace}",
        "creationInfo": {
            "creators": ["Tool: DocEnhance-source-inventory", "Person: Ervins Strauhmanis"],
            "created": "2026-09-17T00:00:00Z",
            "comment": "Fixed specification-date metadata; not a claimed binary build timestamp.",
        },
        "packages": packages,
        "relationships": [
            {
                "spdxElementId": "SPDXRef-DOCUMENT",
                "relationshipType": "DESCRIBES",
                "relatedSpdxElement": p["SPDXID"],
            }
            for p in packages
        ],
    }


def write_json(path: Path, value: dict[str, Any]) -> None:
    """Write indented JSON with a trailing newline."""
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    """Verify every dependency and write licenses, notices, SBOM and build info."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--compiler", required=True)
    args = parser.parse_args()
    lock = load_lock(ROOT / "deps/lock.json")
    lock_hash = hashlib.sha256((ROOT / "deps/lock.json").read_bytes()).hexdigest()
    version = project_version()
    # Managed build directory only. Do not accept the repository or any parent as output.
    resolved = args.out.resolve()
    if resolved == ROOT or ROOT.is_relative_to(resolved):
        msg = "Unsafe metadata output directory"
        raise InventoryError(msg)
    args.out.mkdir(parents=True, exist_ok=True)
    packages, notices = [], list(NOTICE_PREAMBLE)
    for dep in lock["dependencies"]:
        receipt = verify(dep, args.cache)
        copy_licenses(dep, receipt, args.cache / "sources" / dep["name"], args.out)
        notices += notice_lines(dep)
        packages.append(spdx_package(dep, receipt))
    (args.out / "THIRD_PARTY_NOTICES.md").write_text("\n".join(notices) + "\n", encoding="utf-8")
    write_json(args.out / "sbom.spdx.json", spdx_document(version, lock_hash, packages))
    info = {
        "schema_version": 1,
        "version": version,
        "development_stage": "foundation",
        "platform": args.platform,
        "compiler": args.compiler,
        "dependency_lock_sha256": lock_hash,
        "inventory_kind": "declared-source-dependencies",
        "binary_composition_verified": False,
    }
    write_json(args.out / "build-info.json", info)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
