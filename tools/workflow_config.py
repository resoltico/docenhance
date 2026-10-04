# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Read development workflow configuration and check its Python authority."""

from __future__ import annotations

import configparser
import json
import tomllib
from typing import TYPE_CHECKING, Any, override

import yaml

if TYPE_CHECKING:
    from pathlib import Path


class WorkflowLoader(yaml.BaseLoader):
    """String scalars with unique, explicit mapping keys; no hidden merge authority."""

    @override
    def construct_mapping(self, node: yaml.nodes.MappingNode, deep: bool = False) -> dict[Any, Any]:
        """Reject duplicate or merged mappings before the parser can overwrite facts."""
        keys: set[str] = set()
        for key, _ in node.value:
            if not isinstance(key, yaml.nodes.ScalarNode) or key.value == "<<":
                msg = "Workflow mapping keys must be explicit scalars"
                raise ValueError(msg)
            if key.value in keys:
                msg = f"Duplicate workflow mapping key: {key.value}"
                raise ValueError(msg)
            keys.add(key.value)
        return super().construct_mapping(node, deep=deep)


def workflow(path: Path) -> dict[str, Any]:
    """Parse mappings/sequences with string scalars, including GitHub's `on` key."""
    loader = WorkflowLoader(path.read_text(encoding="utf-8"))
    try:
        data = loader.get_single_data()
    finally:
        loader.dispose()
    if not isinstance(data, dict):
        msg = f"Workflow must be a mapping: {path}"
        raise TypeError(msg)
    return data


def python_errors(root: Path) -> list[str]:
    """All Python setup steps and static analysis targets use the reviewed minimum."""
    minimum = json.loads((root / "deps/tools.json").read_text())["python"]["minimum"]
    errors = []
    for path in sorted((root / ".github/workflows").glob("*.yml")):
        for name, job in workflow(path)["jobs"].items():
            steps = job.get("steps", [])
            setups = [
                step for step in steps if step.get("uses", "").startswith("actions/setup-python@")
            ]
            if any("python " in step.get("run", "") for step in steps) and len(setups) != 1:
                errors.append(f"{path.name}/{name}: Python commands require one setup step")
            errors.extend(
                f"{path.name}/{name}: Python setup must unconditionally equal minimum {minimum}"
                for step in setups
                if (step.get("with") or {}).get("python-version") != minimum
                or "if" in step
                or step.get("continue-on-error", "false") != "false"
            )
    ruff = tomllib.loads((root / "ruff.toml").read_text())
    if ruff.get("target-version") != "py" + minimum.replace(".", ""):
        errors.append("Ruff Python target differs from deps/tools.json")
    mypy = configparser.ConfigParser()
    mypy.read(root / "mypy.ini")
    if mypy.get("mypy", "python_version", fallback=None) != minimum:
        errors.append("mypy Python target differs from deps/tools.json")
    if f"Python {minimum} or later" not in (root / "README.md").read_text():
        errors.append("README Python prerequisite differs from deps/tools.json")
    return errors
