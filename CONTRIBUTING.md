# Contributing

DocEnhance is an early native C++ project. Read [status](docs/status.md), [architecture](docs/architecture.md), [design decisions](docs/decisions.md), [build instructions](docs/build.md) and [quality gates](docs/quality.md) before adding features.

## Development agreement

DocEnhance is copyright 2026 Ervins Strauhmanis. Contributions are accepted under the project's MIT license, and contributors keep the copyright in their own work. Retain SPDX headers and all third-party notices. Do not copy an algorithm implementation merely because a research paper is accessible; verify the implementation's license independently. No noncommercial-only, GPL/AGPL or neural runtime component may enter this selected dependency graph without an explicitly approved design change.

Work in a feature branch. Keep changes coherent and include tests that fail on the defect or missing behavior before the change. Explain the affected contract, method or decision, and the result actually verified. Never report configured or skipped tests as executed tests.

Before submitting:

```sh
python tools/install_build_tools.py --lint   # once, in a virtual environment
pre-commit run --all-files                   # every gate, linter and tooling test
cmake --workflow --preset dev                # the real build, tests and clang-tidy
```

`pre-commit run --all-files` is the single local command: `.pre-commit-config.yaml` holds the
authoritative list, and `tools/check_project.py` keeps it aligned with the GitHub quality workflow.
Install it as a Git hook with `pre-commit install`. When you change a parser or the command line,
also fuzz it with `cmake --workflow --preset fuzz`.

All linters are strict and pinned; see [linting and quality gates](docs/quality.md). First-party targets run clang-tidy 23 in warnings-as-errors mode during the native build. Fix findings rather than suppressing them. When a suppression is genuinely unavoidable, name the rule and register it with an explanation in `tests/exceptions/registry.json`; the gates reject anything else. Size limits have no exceptions: split a file that outgrows them. When fuzzing finds a defect, fix it and add the reproducer to `fuzz/regressions/<target>/`; every build then replays it. Do not globally suppress warnings in dependency headers by weakening first-party warning policies. Local pre-commit-compatible hooks are provided as a convenience, not as a substitute for CI.

## Dependency changes

Do not change a tag name while retaining the old object/digest. Verify the release from its official repository, review license and feature changes, update the lock and explicit feature policy, acquire into a clean cache entry, build in a fresh private prefix, run all tests and the configuration/module audit, and retain package-smoke results. Major OpenCV changes require reviewing the **actual** transitive module graph.

## Documentation and generated artifacts

Edit the reviewed contracts, then run `python tools/generate_spec.py`; do not hand-edit generated headers/reference pages. The application version lives only in the top-level `project(... VERSION ...)` command; `tools/project_version.py` reads it from there. A change of direction belongs in [design decisions](docs/decisions.md), together with the contracts it affects and what it means for existing behavior.

Use synthetic fixtures in public tests. Never commit private correspondence, medical records, invoices with personal details, or proprietary documents. Describe reproduction steps without publishing sensitive source content.
