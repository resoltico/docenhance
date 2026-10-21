# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Pinned Git source bytes do not inherit platform-native text checkout policies."""

from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import tools_path  # noqa: F401 -- Bootstrap direct tool imports for standalone unittest discovery.

import dep_acquire
import dep_verify


class CheckoutTests(unittest.TestCase):
    """Exercise real Git attributes, pinned-object fetch and source-byte checkout."""

    def test_declared_text_uses_lf_under_inherited_crlf_policy(self) -> None:
        """A global native-EOL policy cannot change reviewed C++ source bytes."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            upstream = root / "upstream"
            upstream.mkdir()
            dep_verify.run("git", "init", str(upstream))
            dep_verify.run("git", "config", "user.email", "fixture@example.invalid", cwd=upstream)
            dep_verify.run("git", "config", "user.name", "Fixture", cwd=upstream)
            (upstream / ".gitattributes").write_text("*.cpp text\n*.cmd text eol=crlf\n")
            source_name = "source" + ".cpp"
            command_name = "command" + ".cmd"
            (upstream / source_name).write_bytes(b"int value = 1;\n")
            (upstream / command_name).write_bytes(b"exit /b 0\n")
            dep_verify.run("git", "add", ".", cwd=upstream)
            dep_verify.run(
                "git", "-c", "commit.gpgsign=false", "commit", "-m", "Fixture", cwd=upstream
            )
            dep_verify.run("git", "-c", "tag.gpgsign=false", "tag", "v1", cwd=upstream)
            pinned = dep_verify.run("git", "rev-parse", "refs/tags/v1", cwd=upstream)
            global_config = root / "global-config"
            global_config.write_text("[core]\n eol=crlf\n autocrlf=false\n")
            staging = root / "staging"

            def fixture_run(*arguments: str, cwd: Path | None = None) -> str:
                # Only the test's local repository transport is changed; all acquisition,
                # object verification, attributes and checkout operations remain real Git.
                values = [
                    "protocol.file.allow=always" if value == "protocol.file.allow=never" else value
                    for value in arguments
                ]
                return dep_verify.run(*values, cwd=cwd)

            with (
                mock.patch.dict(
                    os.environ,
                    {"GIT_CONFIG_GLOBAL": str(global_config), "GIT_CONFIG_NOSYSTEM": "1"},
                ),
                mock.patch.object(dep_acquire, "run", side_effect=fixture_run),
            ):
                dep_acquire.fetch_git(
                    {
                        "name": "fixture",
                        "repository": str(upstream),
                        "ref": "refs/tags/v1",
                        "object": pinned,
                    },
                    staging,
                )
            self.assertEqual((staging / source_name).read_bytes(), b"int value = 1;\n")
            self.assertEqual((staging / command_name).read_bytes(), b"exit /b 0\r\n")

    def repository(self, root: Path) -> tuple[Path, str]:
        """A real offline upstream with licensed bytes and an immutable commit."""
        source = root / "origin"
        dep_verify.run("git", "-c", "init.defaultBranch=main", "init", str(source))
        dep_verify.run("git", "config", "user.email", "fixture@example.invalid", cwd=source)
        dep_verify.run("git", "config", "user.name", "Fixture", cwd=source)
        (source / "LICENSE").write_bytes(b"fixture license\n")
        (source / "source.h").write_bytes(b"int fixture = 1;\n")
        dep_verify.run("git", "add", ".", cwd=source)
        dep_verify.run("git", "-c", "commit.gpgsign=false", "commit", "-m", "Pinned", cwd=source)
        return source, dep_verify.run("git", "rev-parse", "HEAD", cwd=source)

    def dependency(self, commit: str) -> dep_verify.Dependency:
        """Exercise actual lock admission while keeping transport entirely offline."""
        return {
            "name": "fixture",
            "version": commit,
            "transport": "git",
            "repository": "https://example.invalid/fixture.git",
            "ref": commit,
            "object": commit,
            "object_type": "commit",
            "license_files": ["LICENSE"],
        }

    def acquire_local(
        self, dep: dep_verify.Dependency, origin: Path, cache: Path
    ) -> list[tuple[str, ...]]:
        """Only fixture transport changes; exact original Git argv and all object work are real."""
        calls: list[tuple[str, ...]] = []

        def fixture_run(*arguments: str, cwd: Path | None = None) -> str:
            calls.append(arguments)
            values = list(arguments)
            if values[:4] == ["git", "remote", "add", "origin"]:
                self.assertEqual(values[4], "https://example.invalid/fixture.git")
                values[4] = str(origin)
            values = [
                "protocol.file.allow=always" if v == "protocol.file.allow=never" else v
                for v in values
            ]
            return dep_verify.run(*values, cwd=cwd)

        with mock.patch.object(dep_acquire, "run", side_effect=fixture_run):
            dep_acquire.fetch(dep, cache)
        return calls

    def test_full_commit_lock_admission_refuses_moving_or_ambiguous_routes(self) -> None:
        """Only full identical ref/object commit pins extend the existing tag domain."""
        with tempfile.TemporaryDirectory() as temporary:
            lock = Path(temporary) / "lock.json"
            valid = self.dependency("a" * 40)
            lock.write_text(json.dumps({"schema_version": 1, "dependencies": [valid]}))
            self.assertEqual(dep_verify.load_lock(lock)["dependencies"], [valid])
            for changes in (
                {"ref": "refs/heads/cpp"},
                {"ref": "a" * 12},
                {"ref": "b" * 40},
                {"object_type": "tag"},
                {"object": "A" * 40},
                {"repository": "file:///tmp/source"},
            ):
                with self.subTest(changes=changes):
                    lock.write_text(
                        json.dumps({"schema_version": 1, "dependencies": [valid | changes]})
                    )
                    with self.assertRaises(dep_verify.DependencyError):
                        dep_verify.load_lock(lock)
            tagged = valid | {"ref": "refs/tags/release", "object_type": "tag"}
            lock.write_text(json.dumps({"schema_version": 1, "dependencies": [tagged]}))
            self.assertEqual(dep_verify.load_lock(lock)["dependencies"], [tagged])

    def test_exact_commit_fetch_checkout_receipt_and_existing_source_agree(self) -> None:
        """Actual fetched commit/type/HEAD/tree/clean bytes are verified and never refetched."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            origin, commit = self.repository(root)
            cache = root / "cache"
            dep = self.dependency(commit)
            calls = self.acquire_local(dep, origin, cache)
            fetches = [args for args in calls if "fetch" in args]
            self.assertEqual(len(fetches), 1)
            self.assertEqual(fetches[0][-1], commit)
            self.assertIn("protocol.file.allow=never", fetches[0])
            self.assertIn("protocol.ext.allow=never", fetches[0])
            source = cache / "sources/fixture"
            receipt = dep_verify.verify(dep, cache)
            self.assertEqual(receipt["resolved_commit"], commit)
            self.assertEqual(dep_verify.run("git", "cat-file", "-t", commit, cwd=source), "commit")
            self.assertEqual(
                dep_verify.run("git", "rev-parse", "HEAD^{tree}", cwd=source),
                dep_verify.run("git", "rev-parse", "HEAD^{tree}", cwd=origin),
            )
            self.assertEqual((source / "source.h").read_bytes(), b"int fixture = 1;\n")
            self.assertIn("LICENSE", receipt["files"])
            self.assertEqual(self.acquire_local(dep, origin, cache), [])
            self.assertEqual(json.loads((cache / "receipts/fixture.json").read_text()), receipt)

    def test_commit_verification_refuses_dirty_tree_wrong_head_and_receipt(self) -> None:
        """A bare commit does not weaken existing source, identity or inventory refusal."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            origin, commit = self.repository(root)
            cache = root / "cache"
            dep = self.dependency(commit)
            self.acquire_local(dep, origin, cache)
            source = cache / "sources/fixture"
            (source / "extra").write_bytes(b"untracked")
            with self.assertRaisesRegex(dep_verify.DependencyError, "Modified dependency"):
                dep_verify.verify(dep, cache)
            (source / "extra").unlink()
            receipt = cache / "receipts/fixture.json"
            original = receipt.read_bytes()
            data = json.loads(original)
            data["files"]["source.h"] = "0" * 64
            receipt.write_text(json.dumps(data))
            with self.assertRaisesRegex(dep_verify.DependencyError, "content or lock changed"):
                dep_verify.verify(dep, cache)
            receipt.write_bytes(original)
            (source / "source.h").write_bytes(b"changed\n")
            dep_verify.run(
                "git",
                "-c",
                "user.name=Fixture",
                "-c",
                "user.email=fixture@example.invalid",
                "-c",
                "commit.gpgsign=false",
                "commit",
                "-am",
                "Different",
                cwd=source,
            )
            with self.assertRaisesRegex(dep_verify.DependencyError, "Wrong checkout"):
                dep_verify.verify(dep, cache)

    def test_bare_noncommit_object_and_missing_commit_do_not_publish_source(self) -> None:
        """Actual wrong-type/fetch failures cannot silently fall back or create a receipt."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            origin, _ = self.repository(root)
            dep_verify.run(
                "git", "-c", "tag.gpgsign=false", "tag", "-a", "v1", "-m", "Tag", cwd=origin
            )
            tag = dep_verify.run("git", "rev-parse", "refs/tags/v1", cwd=origin)
            for index, pinned in enumerate([tag, "f" * 40]):
                cache = root / ("cache" + str(index))
                with self.assertRaises((dep_verify.DependencyError, dep_verify.CommandError)):
                    self.acquire_local(self.dependency(pinned), origin, cache)
                self.assertFalse((cache / "sources/fixture").exists())
                self.assertFalse((cache / "receipts/fixture.json").exists())
                self.assertEqual(list((cache / "sources").iterdir()), [])

    def test_annotated_tag_object_and_drift_keep_original_guards(self) -> None:
        """Existing annotated-tag fetch/type/peel and drift refusal remain actual Git controls."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            origin, commit = self.repository(root)
            dep_verify.run(
                "git", "-c", "tag.gpgsign=false", "tag", "-a", "v1", "-m", "Tag", cwd=origin
            )
            tag = dep_verify.run("git", "rev-parse", "refs/tags/v1", cwd=origin)
            dep = self.dependency(tag) | {"ref": "refs/tags/v1", "object_type": "tag"}
            cache = root / "cache"
            calls = self.acquire_local(dep, origin, cache)
            self.assertEqual([x[-1] for x in calls if "fetch" in x], ["refs/tags/v1:refs/tags/v1"])
            self.assertEqual(dep_verify.verify(dep, cache)["resolved_commit"], commit)
            dep_verify.run(
                "git", "-c", "tag.gpgsign=false", "tag", "-fa", "v1", "-m", "Moved", cwd=origin
            )
            with self.assertRaisesRegex(dep_verify.DependencyError, "Upstream tag has changed"):
                self.acquire_local(dep, origin, root / "drift")


if __name__ == "__main__":
    unittest.main()
