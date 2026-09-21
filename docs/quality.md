# Quality gates

Every check here is strict, pinned and enforced in three places: the build, the local hooks
(`python tools/check_all.py`, which `.pre-commit-config.yaml` runs) and the GitHub quality
workflow. The separate source-archive workflow runs the source-level checks before packaging, but
neither workflow publishes binary artifacts. The reasoning behind the strictness is in
[design decisions](decisions.md).

```sh
python tools/install_build_tools.py --lint   # pinned Ruff, mypy, clang-format, pre-commit
python tools/check_all.py                    # ~2 s: structure, gates, format, lint, types, tests
cmake --workflow --preset dev                # build and test, clang-tidy on every target
cmake --workflow --preset sanitize           # the suite under ASan and UBSan
cmake --workflow --preset tsan               # the suite under ThreadSanitizer
cmake --workflow --preset fuzz               # strict fuzzing of parsers and the command line
```

The build also runs the [architecture rules](architecture.md#how-the-rules-are-enforced) as the
`architecture` test, over the real include graph, the real abstract syntax tree, the links the build
declares and every public header on its own. The rules that need no build run in `check_all.py` too,
and a forbidden link fails the configure step before anything is compiled.

## Sanitizers

```sh
cmake --workflow --preset sanitize
```

The sanitizer preset is for GCC and Clang on Linux and macOS, and instruments **first-party code**, not the whole dependency graph; it makes no claim of complete codec instrumentation. Every AddressSanitizer or UndefinedBehaviorSanitizer report is fatal, so undefined behavior fails a test instead of printing and continuing, and the build adds implicit-conversion, bounds and hardened standard-library checks. The nightly workflow runs this preset over the whole suite.

## The tools

Every linter is pinned in `deps/tools.json`:

| Tool | Scope | Configuration | Run |
| --- | --- | --- | --- |
| clang-tidy 23 | Every first-party C++ target, during the native build | `.clang-tidy` (strict profile), `tests/.clang-tidy` | `cmake --workflow --preset dev` |
| Compiler warnings | Every first-party target, `-Werror` / `/WX` | `cmake/ProjectOptions.cmake` | the build |
| clang-format 23 | Every first-party C/C++ file | `.clang-format` | `python tools/check_format.py [--fix]` |
| Ruff (all rules) | Every Python file | `ruff.toml` | `python -m ruff check` and `python -m ruff format --check` |
| mypy (strict) | Every Python file | `mypy.ini` | `python -m mypy` |
| Quality gates | Everything above | `tools/check_gates.py` | `python tools/check_gates.py` |

Install the pinned Python-distributed tools into the project virtual environment with `python tools/install_build_tools.py --lint`. clang-tidy's major version must equal the pin, because findings differ between releases; CMake refuses any other. `python tools/install_llvm.py` installs the pinned build (apt.llvm.org with a fingerprint-checked key on Linux, Homebrew `llvm` on macOS, the SHA-256-verified official installer on Windows); on macOS, `brew install llvm` is equivalent. Homebrew does not preserve every historical formula name, so the major-version gate remains the authority. Shared presets always enable clang-tidy and warnings-as-errors, and the gates reject any shared preset that turns either off. Every first-party translation unit, including the reference runner and the fuzz entry point, is compiled in the normal build so that it is linted.

The gates allow nothing to be grandfathered:

- **File size.** Code files are limited to 300 physical lines (production), 400 (tests and fuzzers) and 200 (build scripts). There is no waiver mechanism: split the file. Function size and complexity are limited by `readability-function-size`/`readability-function-cognitive-complexity` and by Ruff's `C901`/`PLR09xx` rules.
- **In-source suppressions.** Every `NOLINT(...)`, diagnostic pragma, `clang-format off`, `# noqa: ...`, `# type: ignore[...]`, warning-disabling CMake flag or `SKIP_LINTING` must name its rules and be registered in `tests/exceptions/registry.json` as `"path:tool/rule@<digest>": {"explanation": "..."}`, where the digest covers the suppressed line, so the entry survives edits elsewhere in the file but must be reviewed when that line itself changes. Blanket, file-wide and range-wide forms (`// NOLINT`, `# noqa`, `# ruff: noqa`, `# mypy:`) are rejected outright, and registry entries that no longer match a suppression fail as stale.
- **Configuration suppressions.** Every disabled clang-tidy check and every ignored Ruff code carries its written reason in the configuration file itself. Nested `.clang-tidy` files may only disable checks with reasons; nested Ruff, mypy or clang-format configurations are rejected, as are mypy per-module overrides and escape hatches.
- **Coverage.** A `.cpp` file that no target compiles, or a target that skips `de_apply_options()`, fails the gates because it would never be linted.

## Fuzzing

Fuzzing is strict and engine-agnostic; see [design decisions](decisions.md) and [fuzz/README.md](../fuzz/README.md). Each harness checks correctness properties against independent references, not only for crashes. Fuzz and sanitizer builds make every ASan and UBSan report fatal, add implicit-conversion and bounds checks, and harden the standard library.

```sh
python tools/install_llvm.py --fuzzing          # or: brew install llvm (macOS)
python tools/deps.py fetch --dependency cli11
python tools/deps.py fetch --dependency json
CC=clang-23 CXX=clang++-23 cmake --workflow --preset fuzz
```

On macOS, use `CC=$(brew --prefix llvm)/bin/clang CXX=$(brew --prefix llvm)/bin/clang++`; Apple's clang has no libFuzzer runtime. The `fuzz` preset builds only the fuzz targets and their two header-only dependencies, then fuzzes every target for `DE_FUZZ_SECONDS` (default 60). For longer runs, or to merge new coverage into the committed corpus after review:

```sh
python tools/run_fuzzers.py --target cli --binary out/fuzz/app/fuzz/de_fuzz_cli --work out/fuzz/work --seconds 1800
python tools/run_fuzzers.py --target cli --binary out/fuzz/app/fuzz/de_fuzz_cli --work out/fuzz/work --seconds 600 --merge
```

AFL++ uses the same harnesses: `python tools/install_aflplusplus.py`, then `CC=afl-clang-fast CXX=afl-clang-fast++ cmake --preset fuzz-afl` and `run_fuzzers.py --engine afl`. CI fuzzes every pull request with libFuzzer. The nightly workflow runs both engines for 30 minutes per target and also runs the whole test suite under ASan and UBSan. Every normal build replays each seed corpus and recorded regression as the `fuzz-replay-*` tests, on every compiler.
