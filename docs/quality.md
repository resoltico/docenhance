# Quality gates

Every check here is strict, pinned and enforced in three places: the build, the local hooks
(`python tools/check_all.py`, which `.pre-commit-config.yaml` runs) and the GitHub quality
workflow. Linux CI and source packaging execute the same aggregate source checks. Native,
sanitizer and fuzz workflows remain separate required execution evidence; neither workflow
publishes binary artifacts. The reasoning behind the strictness is in [design decisions](decisions.md).

```sh
python tools/install_build_tools.py          # pinned CMake, Ninja and development/test dependencies
python tools/install_build_tools.py --lint   # pinned Ruff, mypy, clang-format, pre-commit
python tools/install_llvm.py --compiler      # the pinned clang, its runtimes and clang-tidy
python tools/check_all.py                    # source checks + Docker Linux native gate on macOS/Windows
export CC=clang-23 CXX=clang++-23            # macOS: "$(brew --prefix llvm)/bin/clang[++]"
cmake --workflow --preset dev                # build and test, clang-tidy on every target
cmake --workflow --preset sanitize           # the suite under ASan and UBSan
cmake --workflow --preset tsan               # the suite under ThreadSanitizer
cmake --workflow --preset fuzz               # strict complete fuzz campaign, including PNG
```

These presets declare `DE_TOOLCHAIN=analysis`, so configure fails rather than falling back to the
host `c++`. That matters most on macOS, where `c++` is Apple clang and implements fewer `-Wextra`
diagnostics than the compilers the required jobs use. Only `release` builds with the platform's
own toolchain; [build and developer workflows](build.md#the-compiler-contract-every-preset-states)
has the contract and the per-platform commands.

The build also runs the [architecture rules](architecture.md#enforced-boundaries-not-just-a-diagram) as the
`architecture` test, over the real include graph, the real abstract syntax tree, the links the build
declares and every public header on its own. The rules that need no build run in `check_all.py` too,
and a forbidden link fails the configure step before anything is compiled.

The workflow guard parses actual YAML fields, requires standalone source-gate commands, checks the
reviewed platform/engine/sanitizer matrices and compiler bindings, and requires the aggregate to
refuse every non-success result. Comments, echoed commands, conditional/optional checks, missing
matrix members and duplicate mapping keys are refused. The reviewed workflow inventory is closed;
new execution contracts must extend the guard and its rejection controls. This establishes wiring,
not arbitrary script correctness or completed CI. Hosted execution and native evidence remain
necessary, including Windows' distinct compiler-shell helper.

The structural, native, sanitizer and fuzz jobs start independently because they exchange no
build inputs. The final required gate still waits for all four roles and rejects every failed,
skipped or cancelled role. This removes a source-check latency barrier; a structural failure can
therefore consume additional runner work before the final refusal. Compiler checks and builds
use the runner's observed CPU count bounded by the shared execution policy.

Native cases run with at most two concurrent processes, separately from build/AST worker counts. Every case, assertion, per-contract
timeout and complete-result reconciliation remains required. The full composition reference case
runs in isolation from other test processes, retaining its deadline and internal scheduler checks.

Full Python tooling coverage remains in each native and sanitizer suite under the reviewed
whole-suite requirement. Its fixture CMake builds generally use the platform toolchain without
product sanitizer instrumentation; running them in both sanitizer modes does not establish
additional ASan/TSan coverage. Native platform repetitions remain necessary for compiler,
filesystem and packaging behavior. The strict tooling runner uses two fresh spawned module interpreters and reconciles exact case
identities with full parent discovery; skipped and expected-failure cases cannot count as passes.
CTest reserves both slots for this nested work. Module timings identify slow fixtures without
omitting cases. Dependency-free references compile separate translation units with bounded
`--jobs` workers and link the complete object set once, retaining strict and sanitizer flags.

The outer native-suite watchdog allows 30 minutes for the whole graph, including the separately weighted compiler/AST check;
it is not a processing-latency guarantee.

## Local Linux verification with Docker

On macOS and Windows, `python tools/check_all.py` and the local Git hook require the Linux
Docker gate. Run it separately with `python tools/check_linux.py`. Missing Docker, an unavailable
daemon, image preparation failure or any failed native check fails the gate; none is a skipped pass.
Linux hosts keep the source check list without recursively starting another container.

The base image is digest-pinned in `deps/tools.json`. A content-addressed development image installs
the repository's pinned CMake/Ninja/test/lint tools and the fingerprint-checked LLVM major through
the existing installers. The native workflow uses GCC/libstdc++, compiler warnings as errors,
clang-tidy on every target, the compiler-backed architecture checks, real executable/reference
suites and native packaging. It also runs the relocated-package smoke test. Imaging libraries
come from the verified source lock and explicit feature configuration, never system substitutes.

The first run prepares the development image and acquires missing locked sources explicitly;
the ensuing CMake workflow remains offline. Existing host source receipts may seed a read-only
cache, and are verified again before use. Persistent Docker volumes isolate Linux `out`, `.cache`
and `dist` from host build products. Cache names include the checkout and tool/source/feature pins;
changing those pins selects fresh native state. Incremental runs reuse validated sources and native
builds, while always rerunning the checks. The current working tree, including uncommitted edits,
is what gets tested.

Complete diagnostics are retained in `.cache/linux-gate/latest.log`. The output identifies that
file on failure. This checks the Docker engine's native Linux architecture; it does not establish
Windows, the other Linux architecture, signing, old-OS execution or sanitizer/fuzz campaigns.
Those CI jobs remain required. Docker is development infrastructure, never an application runtime
dependency.

## Sanitizers

```sh
cmake --workflow --preset sanitize
```

The sanitizer preset requires the pinned LLVM clang on Linux and macOS, and instruments **first-party code**, not the whole dependency graph; it makes no claim of complete codec instrumentation. Every AddressSanitizer or UndefinedBehaviorSanitizer report is fatal, so undefined behavior fails a test instead of printing and continuing, and the build adds implicit-conversion, bounds and hardened standard-library checks. Both sanitizer presets run independently over the whole suite in required PR jobs and in the nightly workflow.

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

Install the pinned Python-distributed tools into the external build-tool environment described in [build setup](build.md) with `python tools/install_build_tools.py --lint`. clang-tidy's major version must equal the pin, because findings differ between releases; CMake refuses any other. `python tools/install_llvm.py` installs the required major and reports the actual patch version (apt.llvm.org with a fingerprint-checked key on Linux, Homebrew `llvm` on macOS, the SHA-256-verified official installer on Windows); on macOS, `brew install llvm` is equivalent. Homebrew does not preserve every historical formula name, so the major-version gate remains the authority. Shared presets always enable clang-tidy and warnings-as-errors, and the gates reject any shared preset that turns either off. Every first-party translation unit, including the reference runner and the fuzz entry point, is compiled in the normal build so that it is linted.

The gates allow nothing to be grandfathered:

- **File size.** Code files are limited to 300 physical lines (production), 400 (tests and fuzzers) and 200 (build scripts). There is no waiver mechanism: split the file. Function size and complexity are limited by `readability-function-size`/`readability-function-cognitive-complexity` and by Ruff's `C901`/`PLR09xx` rules.
- **In-source suppressions.** Every `NOLINT(...)`, diagnostic pragma, `clang-format off`, `# noqa: ...`, `# type: ignore[...]`, warning-disabling CMake flag or `SKIP_LINTING` must name its rules and be registered in `tests/exceptions/registry.json` as `"path:tool/rule@<digest>": {"explanation": "..."}`, where the digest covers the complete source line (and both marker and target for `NOLINTNEXTLINE`), so the entry survives edits elsewhere in the file but must be reviewed when that line itself changes. Blanket, file-wide and range-wide forms (`// NOLINT`, `NOLINTBEGIN`/`NOLINTEND`, `# noqa`, `# ruff: noqa`, `# mypy:`) are rejected outright, and registry entries that no longer match a suppression fail as stale.
- **Configuration suppressions.** Every disabled clang-tidy check and every ignored Ruff code carries its written reason in the configuration file itself. Nested `.clang-tidy` files may only disable checks with reasons; nested Ruff, mypy or clang-format configurations are rejected, as are mypy per-module overrides and escape hatches.
- **Coverage.** A `.cpp` file that no target compiles, or a target that skips `de_apply_options()`, fails the gates because it would never be linted.

## Fuzzing

Fuzzing is strict and engine-agnostic; see [design decisions](decisions.md) and [fuzz/README.md](../fuzz/README.md). Each harness checks correctness properties against independent references, not only for crashes. Fuzz and sanitizer builds make every ASan and UBSan report fatal, add implicit-conversion and bounds checks, and harden the standard library.

```sh
python tools/install_llvm.py --fuzzing          # or: brew install llvm (macOS)
python tools/deps.py fetch                   # acquire the complete reviewed source lock
CC=clang-23 CXX=clang++-23 cmake --workflow --preset fuzz
```

On macOS, use `CC=$(brew --prefix llvm)/bin/clang CXX=$(brew --prefix llvm)/bin/clang++`; Apple's clang has no libFuzzer runtime. The `fuzz` preset builds the fuzz targets, CLI11/JSON, and sanitizer/coverage-instrumented PNG/zlib, then fuzzes every target for `DE_FUZZ_SECONDS` (default 60). For a separate bounded run with retained evidence:

```sh
python tools/run_fuzzers.py --target cli --binary out/fuzz/app/fuzz/de_fuzz_cli --work out/fuzz/work --seconds 1800
python tools/run_fuzzers.py --target cli --binary out/fuzz/app/fuzz/de_fuzz_cli --work out/fuzz/work --seconds 600
```

AFL++ uses the same harnesses: `python tools/install_aflplusplus.py`, then `CC=afl-clang-fast CXX=afl-clang-fast++ cmake --preset fuzz-afl` and `run_fuzzers.py --engine afl`. CI fuzzes every pull request with both libFuzzer and AFL++. The nightly workflow runs both engines for 30 minutes per target and also runs the whole test suite independently under ASan/UBSan and TSan. Campaigns use the CTest registration, including the box-mean harness. Every normal build replays each seed corpus and recorded regression as the `fuzz-replay-*` tests, on every compiler.

The public Ubuntu quality and nightly jobs explicitly select four concurrent fuzz targets.
[GitHub's runner specification](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
provides four cores and 16 GB for those runners; four libFuzzer processes each retain the 2 GiB
RSS/malloc ceilings, leaving room for runtime and operating-system overhead. AFL++ retains its
ASan-compatible settings and finite harness allocations, without an equivalent RSS-limit claim.
Local campaigns default to two workers. Each PR target still receives 60 seconds and each nightly
target 1,800 seconds; planning and CTest use the same selected concurrency and retain complete
per-target execution evidence. Workload admission precedes compiler installation and the build.

Campaign completeness, bounded parallelism, corpus ownership and retained evidence are specified
in [fuzzing](fuzzing.md). Each run owns a fresh directory; automatic corpus merging is removed.
The PNG fuzz build instruments libpng/zlib as well as first-party code. Required PR checks retain
evidence artifacts; no source or regression is automatically rewritten by a successful run.
