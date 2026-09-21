# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Repository hygiene gates: what belongs in the tree, and in what shape.

Editor and operating-system droppings, half-applied patches and stale generated files accumulate
quietly, and a repository that carries them teaches contributors that it does not matter. These
checks keep the tree deliberate: only intended files, portable names, text that is UTF-8 with LF
endings and a final newline, executable bits that match shebangs, byte-exact fuzz inputs declared
binary, the documents a public repository owes its readers, and documentation that does not point
at paths which no longer exist.
"""

from __future__ import annotations

import fnmatch
import re
from pathlib import Path, PurePosixPath

import package_source

STRAY_NAMES = ("Thumbs.db", ".DS_Store", "desktop.ini")
STRAY_SUFFIXES = (".orig", ".rej", ".bak", ".swp", ".swo", ".pyc", "~")
# Paths whose bytes are exact test inputs and must never be normalized or rewritten.
BINARY_TREES = ("fuzz/corpus", "fuzz/regressions")
REQUIRED_DOCUMENTS = (
    "README.md",
    "LICENSE",
    "CHANGELOG.md",
    "CONTRIBUTING.md",
    ".github/CODE_OF_CONDUCT.md",
    ".github/SECURITY.md",
    "THIRD_PARTY_NOTICES.md",
    "docs/status.md",
    "docs/status.md",
    ".github/pull_request_template.md",
)
# Markdown that quotes repository paths.
DOCUMENTED_PATH = re.compile(
    r"`?\b((?:src|tools|tests|fuzz|cmake|docs|deps|spec|include|schemas)/[\w./+-]*"
    r"\.(?:md|py|json|cpp|hpp|cmake|txt|yml|dict|in|sha256))\b`?"
)
TEXT_SUFFIXES = frozenset(
    {
        ".c",
        ".cmake",
        ".cpp",
        ".dict",
        ".h",
        ".hpp",
        ".in",
        ".ini",
        ".json",
        ".md",
        ".ps1",
        ".py",
        ".sh",
        ".toml",
        ".txt",
        ".yml",
        ".yaml",
        "",
    }
)


def is_binary_input(rel: str) -> bool:
    """True for files whose exact bytes are the point, such as fuzz corpora."""
    return rel.startswith(BINARY_TREES)


def is_text(path: Path, rel: str) -> bool:
    """True for files this project keeps as normalized text."""
    if is_binary_input(rel) or PurePosixPath(rel).name in STRAY_NAMES:
        return False
    return path.suffix in TEXT_SUFFIXES


def ignored_names(root: Path) -> list[str]:
    """The .gitignore patterns that match a bare file name, such as .DS_Store or *.pyc."""
    lines = (root / ".gitignore").read_text(encoding="utf-8").splitlines()
    return [line.strip() for line in lines if line.strip() and not line.startswith(("#", "/"))]


def stray_errors(root: Path, files: list[tuple[Path, str]]) -> list[str]:
    """Editor backups, operating-system metadata and compiled leftovers do not belong here.

    Names .gitignore already excludes are the tool's business, not the repository's.
    """
    patterns = ignored_names(root)
    errors: list[str] = []
    for _, rel in files:
        name = PurePosixPath(rel).name
        stray = name in STRAY_NAMES or rel.endswith(STRAY_SUFFIXES)
        if stray and not any(fnmatch.fnmatch(name, pattern) for pattern in patterns):
            errors.append(f"Stray file: {rel}")
    return errors


def name_errors(files: list[tuple[Path, str]]) -> list[str]:
    """Names must be portable: ASCII, no spaces, and unique on case-insensitive filesystems."""
    errors = []
    seen: dict[str, str] = {}
    for _, rel in files:
        name = PurePosixPath(rel).name
        if not name.isascii() or any(c.isspace() for c in name):
            errors.append(f"Unportable file name: {rel}")
        clash = seen.setdefault(rel.lower(), rel)
        if clash != rel:
            errors.append(f"Names differ only by case: {clash} and {rel}")
    return errors


def content_errors(path: Path, rel: str) -> list[str]:
    """One text file: UTF-8, no BOM, LF endings, a final newline, no trailing blanks or tabs."""
    data = path.read_bytes()
    if not data:
        return [f"Empty file: {rel}"]
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return [f"Not valid UTF-8: {rel}"]
    errors = []
    if data.startswith(b"\xef\xbb\xbf"):
        errors.append(f"Byte-order mark: {rel}")
    if b"\r\n" in data:
        errors.append(f"CRLF line endings: {rel}")
    if not data.endswith(b"\n"):
        errors.append(f"No final newline: {rel}")
    if path.suffix != ".md":
        # Markdown keeps trailing spaces, which are significant line breaks there.
        if any(line.rstrip("\n").endswith((" ", "\t")) for line in text.splitlines(keepends=True)):
            errors.append(f"Trailing whitespace: {rel}")
        if "\t" in text:
            errors.append(f"Tab indentation: {rel}")
    return errors


def mode_errors(files: list[tuple[Path, str]]) -> list[str]:
    """A file is executable exactly when it starts with a shebang."""
    errors = []
    for path, rel in files:
        if is_binary_input(rel):
            continue
        executable = bool(path.stat().st_mode & 0o111)
        expected = package_source.file_mode(path.read_bytes()) == package_source.EXECUTABLE_MODE
        if executable != expected:
            state = "executable" if executable else "not executable"
            errors.append(f"File is {state} but its shebang says otherwise: {rel}")
    return errors


def attribute_errors(root: Path) -> list[str]:
    """Byte-exact trees must be declared binary, or a checkout could rewrite their line endings."""
    text = (root / ".gitattributes").read_text(encoding="utf-8")
    return [
        f".gitattributes must declare {tree}/** binary"
        for tree in BINARY_TREES
        if not re.search(rf"^{re.escape(tree)}/\*\*\s+binary$", text, flags=re.MULTILINE)
    ]


def document_errors(root: Path) -> list[str]:
    """A public repository owes its readers these documents."""
    return [
        f"Missing required document: {name}"
        for name in REQUIRED_DOCUMENTS
        if not (root / name).is_file()
    ]


REFERENCING_SUFFIXES = frozenset(
    {".md", ".json", ".yml", ".yaml", ".py", ".cmake", ".toml", ".ini"}
)


def reference_errors(root: Path, files: list[tuple[Path, str]]) -> list[str]:
    """No file may quote a repository path that does not exist."""
    errors: list[str] = []
    for path, rel in files:
        # Test fixtures name paths that deliberately do not exist.
        if (
            path.suffix not in REFERENCING_SUFFIXES
            or is_binary_input(rel)
            or rel.startswith("tests/")
        ):
            continue
        quoted_paths = DOCUMENTED_PATH.findall(path.read_text(encoding="utf-8"))
        errors.extend(
            f"{rel} refers to a path that does not exist: {quoted}"
            for quoted in quoted_paths
            if not (root / quoted).exists()
        )
    return errors


def check(root: Path, files: list[tuple[Path, str]]) -> list[str]:
    """Run every repository-hygiene gate over (path, repository-relative path) pairs."""
    errors = [
        *stray_errors(root, files),
        *name_errors(files),
        *mode_errors(files),
        *attribute_errors(root),
        *document_errors(root),
        *reference_errors(root, files),
    ]
    errors.extend(f"Symbolic link: {rel}" for path, rel in files if path.is_symlink())
    for path, rel in files:
        if is_text(path, rel):
            errors.extend(content_errors(path, rel))
    return errors
