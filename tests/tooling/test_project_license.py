# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""License checks reject altered terms and obsolete first-party declarations."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import check_project
import generate_spec
import license_inventory
import package_inspection


class ProjectLicenseTests(unittest.TestCase):
    """Check canonical terms independently from generated license declarations."""

    def test_license_text_rejects_replacement_and_amendment(self) -> None:
        """A new label or additional restriction cannot pass the canonical text check."""
        original = (ROOT / "LICENSE").read_bytes()
        self.assertEqual(check_project.license_errors(), [])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for invalid in (b"MIT license", original + b"\nAdditional restriction\n"):
                (root / "LICENSE").write_bytes(invalid)
                with patch.object(check_project, "ROOT", root):
                    self.assertTrue(check_project.license_errors())

    def test_generated_declarations_use_current_license(self) -> None:
        """Generated declarations and checked headers agree on the current terms."""
        self.assertIn("SPDX-License-Identifier: MPL-2.0", generate_spec.HEADER_PREAMBLE)
        self.assertEqual(check_project.LICENSE_TAG, "SPDX-License-Identifier: MPL-2.0")
        self.assertIn("MPL-2.0-licensed", "\n".join(license_inventory.NOTICE_PREAMBLE))

    def test_obsolete_first_party_header_is_refused(self) -> None:
        """An old MIT header cannot pass first-party ownership admission."""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "source.cpp"
            path.write_text(
                "// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis\n"
                "// SPDX-License-Identifier: MIT\n"
            )
            with patch.object(check_project, "code_files", return_value=[(path, path.name, "cxx")]):
                self.assertIn(
                    "Missing SPDX-License-Identifier header: source.cpp",
                    check_project.metadata_errors(),
                )

    def test_copyright_names_actual_holder_and_requires_notice(self) -> None:
        """Contributors keep their own copyright; absent or empty declarations are refused."""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "source.cpp"
            for notice, accepted in (
                ("// SPDX-FileCopyrightText: 2026 Example Contributor", True),
                ("# SPDX-FileCopyrightText: 2027 Another Contributor", True),
                ("// SPDX-FileCopyrightText: ", False),
                ("// SPDX-FileCopyrightText:   ", False),
                ("", False),
            ):
                with self.subTest(notice=notice):
                    path.write_text(notice + "\n// SPDX-License-Identifier: MPL-2.0\n")
                    with patch.object(
                        check_project, "code_files", return_value=[(path, path.name, "cxx")]
                    ):
                        errors = check_project.metadata_errors()
                    self.assertEqual(errors == [], accepted, errors)


class InventoryDocumentTests(unittest.TestCase):
    """Source-inventory identity and attribution describe the generated document."""

    def test_frozen_inputs_are_stable_but_material_changes_get_new_namespaces(self) -> None:
        """Version, license/source facts and creation time cannot share a stale document URI."""
        created = "2026-10-05T10:00:00Z"
        package = {"SPDXID": "SPDXRef-Package-example", "licenseDeclared": "MIT", "sourceInfo": "a"}
        original = license_inventory.spdx_document("0.6.0", [package], created)
        self.assertEqual(original, license_inventory.spdx_document("0.6.0", [package], created))
        changed = (
            license_inventory.spdx_document("0.7.0", [package], created),
            license_inventory.spdx_document(
                "0.6.0", [{**package, "licenseDeclared": "Zlib"}], created
            ),
            license_inventory.spdx_document("0.6.0", [{**package, "sourceInfo": "b"}], created),
            license_inventory.spdx_document("0.6.0", [package], "2026-10-05T10:00:01Z"),
        )
        namespaces = {doc["documentNamespace"] for doc in (original, *changed)}
        self.assertEqual(len(namespaces), len(changed) + 1)
        self.assertTrue(all(uri.startswith("https://") and "#" not in uri for uri in namespaces))

    def test_creator_time_and_metadata_license_do_not_relicense_software(self) -> None:
        """The tool creates metadata under CC0 while the dependency retains its own terms."""
        created = "2026-10-05T10:00:00Z"
        doc = license_inventory.spdx_document(
            "0.6.0", [{"SPDXID": "SPDXRef-Package-example", "licenseDeclared": "MIT"}], created
        )
        self.assertEqual(doc["creationInfo"]["created"], created)
        self.assertEqual(
            doc["creationInfo"]["creators"], ["Tool: DocEnhance-source-inventory-0.6.0"]
        )
        self.assertEqual(doc["dataLicense"], "CC0-1.0")
        self.assertEqual(doc["packages"][0]["licenseDeclared"], "MIT")

    def test_invalid_creation_instants_are_refused_before_generation(self) -> None:
        """Missing, malformed, noncanonical or impossible date facts cannot enter an inventory."""
        for invalid in (
            None,
            True,
            "",
            "2026-02-30T10:00:00Z",
            "2026-10-5T10:00:00Z",
            "2026-10-05T10:00:00+00:00",
            "2026-10-05T10:00:00.1Z",
        ):
            with self.subTest(invalid=invalid), self.assertRaises(license_inventory.InventoryError):
                license_inventory.inventory_instant(invalid)

    def test_obsolete_build_metadata_is_refused_without_source_access(self) -> None:
        """An old package inventory requires regeneration rather than a compatibility fallback."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "app/package-metadata").mkdir(parents=True)
            (root / "app/package-metadata/build-info.json").write_text(
                json.dumps({"schema_version": 1})
            )
            with self.assertRaisesRegex(ValueError, "Regenerate"):
                package_inspection.expected_files(root, "docenhance")
