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
The CLI and interpretation contracts reserve both native slots to avoid competing nested work within their per-contract deadlines.
Other unit and fuzz replay cases retain bounded concurrency; no fixture or deadline is relaxed.

Run heavyweight native and sanitizer test phases one at a time on a shared developer machine.
Launching several suites multiplies their process bounds and can cause memory compression and
deadline failures. Hosted jobs retain parallel execution on separate runners; local contention
does not justify increasing deadlines or omitting cases.
Set `DE_NATIVE_TEST_JOBS=1` for one native test process on a constrained host; the default is two.
The supervisor refuses values outside 1..2 and retains complete reconciliation and deadlines.

Full Python tooling coverage remains in each native and sanitizer suite under the reviewed
whole-suite requirement. Its fixture CMake builds generally use the platform toolchain without
product sanitizer instrumentation; running them in both sanitizer modes does not establish
additional ASan/TSan coverage. Native platform repetitions remain necessary for compiler,
filesystem and packaging behavior. The strict tooling runner uses two fresh spawned module interpreters and reconciles exact case
identities with full parent discovery; skipped and expected-failure cases cannot count as passes.
Workers clear inherited `GIT_*` variables before discovery so temporary Git fixtures retain their
own repository identity under hooks; parent state and deliberate fixture overrides are preserved.
CTest reserves both slots for this nested work. Module timings identify slow fixtures without
omitting cases. Dependency-free references compile separate translation units with bounded
`--jobs` workers and link the complete object set once, retaining strict and sanitizer flags.

Executable contract tests admit the response schema once per fresh test process and validate every
new response against it. They do not reuse command results; invalid later payloads, malformed schema
initialization and exit-status disagreement remain rejection controls. Native include classification
resolves its layer roots once per compiler trace and resolves every observed header freshly. Native
compiler preprocessing, AST checks, header probes and ownership diagnostics still run in full.

The independent Otsu reference recomputes class statistics at each occupied-bin plateau start.
Empty-bin candidates have the same populations and score as that start, so their earliest eligible
threshold and global relative tolerance are retained. A separate exhaustive 4,095-candidate oracle
checks dense byte populations, sparse histograms, single-bin fallbacks and near ties. Required fuzz
campaign durations, input domains, sanitizer settings and complete-target reconciliation are unchanged.

Windows CI runs the target-admission CMake controls before the long native build, so a platform-specific guard regression fails promptly. The complete native tooling suite still runs afterward. Source-property lookup preserves CMake's declared directory spelling, including Windows DOS short names; filesystem alias expansion must not disconnect a suppression from its source.

The outer native-suite watchdog allows 30 minutes for the whole graph, including the separately weighted compiler/AST check;
the supervisor reserves its final minute for cleanup and evidence reconciliation. Discovery and
admission share the execution deadline with the child suite. On POSIX, interruption unwinds the
owned child group, allowing nested runners a bounded grace before killing remaining group members.
Windows cleanup covers the direct child; it does not establish descendant termination.
This watchdog is not a processing-latency guarantee.

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

The gate requires the exact cached development image computed from the locked base and tool
recipe. Prepare that image as a separate, explicitly supervised setup task; a missing image fails
rather than pulling a substitute or starting an unowned daemon build. The prepared host dependency
cache and its original locked archives are prerequisites. No network acquisition runs in the gate.
Only lock-selected verified source directories and receipts seed an empty shared source cache;
archives stay on a read-only host mount. Existing complete caches are verified, while incomplete,
foreign or mismatched state fails without overwrite, repair or deletion.

The source mount is read-only. Native volumes keyed by the inspected immutable image ID own Linux `out`, `.cache` and `dist`;
Ruff/Mypy caches stay in writable cache subdirectories. Each run's temporary directory lives
outside the checkout in the owned container's private `/tmp/docenhance-linux-tmp` directory. The
current working tree, including uncommitted changes, is what gets tested, so keep host inputs
stable while checks run. Recipe tags still identify prepared images, while a changed actual image ID selects fresh native
volumes without adopting or deleting earlier volumes. Locked sources remain shared independently
of the native image. Full native release/package coverage remains unchanged.

One overall deadline uses the reviewed native CI timeout; time spent in readiness and execution
reduces the same remaining budget. Daemon cleanup is reserved inside that deadline. The Docker CLI
has bounded diagnostics; omitted output is a failure, even when the child exits zero. Logs remain
under `.cache/linux-gate`, and `latest.log` is an atomically replaced convenience hard link. A
failed CLI still requires exact name/label/ID reconciliation of its daemon container. Unknown
ownership retains the writer claim and both the primary and cleanup failures; it never authorizes
a retry, volume deletion or pruning. These are operational bounds, not minimum-space or RSS claims.

This checks the Docker engine's native Linux architecture; it does not establish
Windows, the other Linux architecture, signing, old-OS execution or sanitizer/fuzz campaigns.
Those CI jobs remain required. Docker is development infrastructure, never an application runtime
dependency.

Linux gate diagnostics admit a new run only while the retained tree can reserve its 2 MiB log
and 64 KiB command metadata within 64 MiB of unique file payload and 4096 entries. Hard-linked
`latest.log` does not duplicate payload. Admission refuses a full tree without deleting prior
evidence; preservation and disposal require explicit operator action. These operational ceilings
are not a minimum build-space claim.

The gate creates its private temporary directory exclusively and removes only that directory
on ordinary shell exit. Temporary files are not stored in a persistent volume. After an
untrappable interruption, the existing writer claim and exact owned-container reconciliation
govern cleanup; uncertain daemon ownership blocks another gate until inspected. Diagnostics
remain in the bounded host evidence directory independently of container removal.

## Sanitizers

```sh
cmake --workflow --preset sanitize
```

The sanitizer preset requires the pinned LLVM clang on Linux and macOS, and instruments **first-party code**, not the whole dependency graph; it makes no claim of complete codec instrumentation. Every AddressSanitizer or UndefinedBehaviorSanitizer report is fatal, so undefined behavior fails a test instead of printing and continuing, and the build adds implicit-conversion, bounds and hardened standard-library checks. Both sanitizer presets run independently over the whole suite in required PR jobs and in the weekly workflow.

## The tools

Every linter is pinned in `deps/tools.json`:

| Tool | Scope | Configuration | Run |
| --- | --- | --- | --- |
| clang-tidy 23 | Every first-party C++ target, during the native build | `.clang-tidy` (strict profile), `tests/.clang-tidy` | `cmake --workflow --preset dev` |
| Compiler warnings | Every first-party target, `-Werror` / `/WX` | `cmake/ProjectOptions.cmake` | the build |
| clang-format 23 | Every first-party C/C++ file | `.clang-format` | `python tools/check_format.py [--fix]` |
| Ruff (all rules) | Every Python file | `ruff.toml` | `python -m ruff check` and `python -m ruff format --check` (independent of Git ignore patterns) |
| mypy (strict) | Every Python file | `mypy.ini` | `python -m mypy` |
| Quality gates | Everything above | `tools/check_gates.py` | `python tools/check_gates.py` |

Install the pinned Python-distributed tools into the external build-tool environment described in [build setup](build.md) with `python tools/install_build_tools.py --lint`. clang-tidy's major version must equal the pin, because findings differ between releases; CMake refuses any other. `python tools/install_llvm.py` installs the required major and reports the actual patch version (apt.llvm.org with a fingerprint-checked key on Linux, Homebrew `llvm` on macOS, the SHA-256-verified official installer on Windows); on macOS, `brew install llvm` is equivalent. Homebrew does not preserve every versioned formula name, so the major-version gate remains the authority. Shared presets always enable clang-tidy and warnings-as-errors, and the gates reject any shared preset that turns either off. Every first-party translation unit, including the reference runner and the fuzz entry point, is compiled in the normal build so that it is linted.

The gates allow nothing to be grandfathered:

- **File size.** Code files are limited to 300 physical lines (production), 400 (tests and fuzzers) and 200 (build scripts). There is no waiver mechanism: split the file. Production filenames beginning `test_` still use the production limit. Function size and complexity are limited by `readability-function-size`/`readability-function-cognitive-complexity` and by Ruff's `C901`/`PLR09xx` rules. The original `readability-function-size` check enforces body limits without a suppression mechanism. The separate `google-readability-function-size` check enforces the five-parameter limit; only a genuinely fixed native ABI can require a code-bound parameter exception. Such an exception cannot hide body size violations.
- **In-source suppressions.** Every supported `NOLINT(...)`, scoped diagnostic pragma, bounded `clang-format off`, `# noqa: ...`, `# type: ignore[...]` and warning-disabling CMake flag must name its rules and be registered in `tests/exceptions/registry.json` as `"path:tool/rule@<sha256>": {"explanation": "...", "occurrences": N}`. The full SHA256 preserves bound source text, including whitespace in literals. Same-line markers bind their source line; `NOLINTNEXTLINE` binds both marker and target line. Compiler warning and formatter regions bind all text through the matching restoration. Identical sites must match their declared occurrence count. Old truncated keys, duplicate JSON keys, malformed entries, extra sites and stale bindings are refused. A content hash and rationale text establish binding and reviewability, not the truth of the explanation or proof of human review. Hidden `_Pragma`/`__pragma` compiler controls, sanitizer instrumentation opt-outs and blanket, file-wide or unbounded range forms (`// NOLINT`, `NOLINTBEGIN`/`NOLINTEND`, `# noqa`, `# ruff: noqa`, `# mypy:`, formatter-off without restoration) are rejected outright, and registry entries that no longer match a suppression fail as stale.
- **Configuration suppressions.** Every effective named clang-tidy or Ruff configuration exception carries a current reason and a central registry entry bound to the complete configuration and its global or per-file scope. Every actual clang-tidy `CheckOptions` setting also has its own qualified option approval and complete configuration binding, including positive policy settings; adding an option or changing its value cannot reuse approval for disabled checks. Ruff docstring choices are explicit exact-rule exclusions; an implicit convention cannot silently disable additional rules. Unknown lint-option routes, wildcard exception codes and duplicate YAML mappings are refused. Root/nested clang-tidy fields, Ruff inheritance/formatting fields, the mypy profile and semantic clang-format settings have closed admission; secondary flags cannot undo strictness or hide formatter exclusions. The size/complexity checks and their reviewed budgets cannot be disabled, ignored or changed through root or nested configuration. Other nested `.clang-tidy` relaxations need reasons; nested Ruff, mypy or clang-format configurations are rejected, as are mypy per-module overrides and escape hatches.
- **Coverage.** Complete native workflows compare discovered translation units with the actual compiler database and reject target options that do not retain warnings and clang-tidy. Comments and unreachable CMake branches cannot establish compilation; source-only checks do not claim that evidence.

These measures bound files and individual functions; they do not prove that a class or a group of
small functions has one coherent responsibility. Cross-file aggregation, excessive delegation
and domain coupling still need the separate design/challenge review required by `AGENTS.md`.
A passing gate supports its checked property, not a perfect architecture score.

## Fuzzing

Fuzzing is strict and engine-agnostic; see [design decisions](decisions.md) and [fuzz/README.md](../fuzz/README.md). Each harness checks correctness properties against independent references, not only for crashes. Fuzz and sanitizer builds make every ASan and UBSan report fatal, add implicit-conversion and bounds checks, and harden the standard library.

```sh
python tools/install_llvm.py --fuzzing          # or: brew install llvm (macOS)
python tools/deps.py fetch                   # acquire the complete reviewed source lock
CC=clang-23 CXX=clang++-23 cmake --workflow --preset fuzz
```

On macOS, use `CC=$(brew --prefix llvm)/bin/clang CXX=$(brew --prefix llvm)/bin/clang++`; Apple's clang has no libFuzzer runtime. The `fuzz` preset builds every harness and the instrumented native archive closure described in [fuzzing](fuzzing.md), then fuzzes every target for `DE_FUZZ_SECONDS` (local default 60). The following explicit manual runs retain their own evidence; they are not CI defaults:

```sh
python tools/run_fuzzers.py --target cli --binary out/fuzz/app/fuzz/de_fuzz_cli --work out/fuzz/work --seconds 1800
python tools/run_fuzzers.py --target cli --binary out/fuzz/app/fuzz/de_fuzz_cli --work out/fuzz/work --seconds 600
```

AFL++ uses the same harnesses: `python tools/install_aflplusplus.py`, then `CC=afl-clang-fast CXX=afl-clang-fast++ cmake --preset fuzz-afl` and `run_fuzzers.py --engine afl`. CI fuzzes every pull request with both libFuzzer and AFL++. The weekly workflow runs both engines for five minutes per target and also runs the whole test suite independently under ASan/UBSan and TSan. Campaigns use the CTest registration, including the box-mean harness. Every normal build replays each seed corpus and recorded regression as the `fuzz-replay-*` tests, on every compiler.

The public Ubuntu quality and weekly jobs explicitly select four concurrent fuzz targets.
[GitHub's runner specification](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
provides four cores and 16 GB for those runners; four libFuzzer processes each retain the 2 GiB
RSS/malloc ceilings, leaving room for runtime and operating-system overhead. AFL++ retains its
ASan-compatible settings and finite harness allocations, without an equivalent RSS-limit claim.
Local campaigns default to two workers. Each PR target receives 15 seconds and each weekly
target 300 seconds; planning and CTest use the same selected concurrency and retain complete
per-target execution evidence. Workload admission precedes compiler installation and the build.

Campaign completeness, bounded parallelism, corpus ownership and retained evidence are specified
in [fuzzing](fuzzing.md). Each run owns a fresh directory; automatic corpus merging is not supported.
The PNG fuzz build instruments libpng/zlib as well as first-party code. Required PR checks retain
evidence artifacts; no source or regression is automatically rewritten by a successful run.

## CI source caching and replay ownership

## Verified source reuse

Dependency caches contain only `.cache/deps/archives`, `.cache/deps/sources` and `.cache/deps/receipts`. Their exact OS-specific key binds the source lock and acquisition/verification implementation; no prefix-match fallback is admitted. Active writer claims are excluded. [Dependency policy](dependencies.md) owns verification and recovery.

Acquisition runs after every restore. Locked Git release objects, archive digests and complete inventories must agree before build setup. Changed existing sources or missing receipts fail; absent sources use ordinary locked acquisition. An exact miss is saved only after successful acquisition. Cache keys and restore success do not establish source trust.

Native build trees, installed prefixes, objects, product binaries and previous results are not restored. Each configuration owns its native products and private prefix. The Intel LLVM tool cache is separate and requires its own source/recipe, readiness and byte-identity checks. Its outer key fingerprints the actual macOS version and selected developer directory after Xcode selection, matching the installed generation; a stale outer hit cannot repeatedly rebuild an unsaved inner generation. Only `bin/clang-tidy`, `bin/clang-query`, matching builtin resource headers and their digest receipt survive the Intel source build; source and object trees live in the installer temporary directory. Static LLVM/Clang linking and the retained resource headers keep the tools independent of that temporary build. Unexpected cache entries fail readiness verification. Header inspection admits at most 4,096 entries, 64 MiB of logical bytes and 1,024 UTF-8 bytes per relative path; receipts admit 8 MiB. These finite allowances retain the full builtin header package rather than guessing a smaller compiler include closure. Compiler reuse is not binary reproducibility. See [configuration ownership](build.md#configuration-ownership).

## Configuration-private replay work

`de_fuzz_replay_driver` is an OBJECT library containing the shared `fuzz/support/replay_main.cpp` driver. It is compiled and linted once per configuration with the required warnings, hardening and sanitizer flags. Every manifest harness retains its own target options, direct links and corpus-replay registration. No product or result is shared between configurations.

Complete discovery, actual compiler/linter work, seed provenance, requested fuzz exposure, fresh results and relocated package inspection remain required. Work sharing does not justify omitting a harness, assertion, compiler boundary or required platform. Actual elapsed time depends on machine and workload; no speedup or completion-time guarantee is claimed.

## Tooling fixture ownership

Each tooling module runs in a fresh bounded interpreter. Workers clear inherited `GIT_*` repository overrides before discovery so temporary Git fixtures own their repositories; parent state and deliberate fixture overrides remain intact. The strict runner reconciles every discovered case and rejects skipped or expected-failure work. [Quality](#) owns execution limits and outcome admission.

## Native execution diagnostics

The native supervisor retains complete fresh CTest output and exact JUnit bytes in lossless,
verified archives. JUnit identities, status and nonzero Catch assertion checks still decide test
admission. CTest's abbreviated `system-out` is not the complete allocation trace. Failed and
interrupted runs retain their available diagnostic bytes and are never relabeled as passing.
The supervisor avoids echoing the same large failure output into another capture stream.
Catch displays invisible string characters explicitly in XML and stops a failed case at its
first assertion failure. Passing cases retain all assertions; stopped failures cannot satisfy
complete-suite admission.

`tools/native_journal.py` owns source freshness and bounded journal framing.
`tools/native_output.py` owns raw-file admission, archive byte limits and round-trip checks. These are finite evidence-admission limits, not instantaneous filesystem
quotas or process-RSS bounds. Archive reconciliation uses time reserved inside the native
supervisor's existing deadline; cleanup can exhaust that allowance and cause evidence refusal.
The native and sanitizer jobs upload only the owned compressed diagnostics, receipts and
partial/error artifacts with seven-day retention. Uploading an archive is not test execution or
cross-platform qualification.

Native allocation tracing uses one fixed memory chunk and a separate caller-weighted logical
admission bound. A preopened owned sidecar retains full chunks without extra observer workers;
after all workers join, replay emits every original event in order. Trace overflow, storage
failure or an incomplete replay refuses the test. The allocation ledger, trace storage and
charged processing/native reservations remain separate quantities.

## Retiring local build generations

Content-addressed Docker images and volumes isolate configurations but remain until explicitly
removed. Inspect exact volume names with `docker volume inspect NAME` and current references
with `docker ps -a --filter volume=NAME`. Review contents through a read-only mount, preserve
needed failure records, package evidence and fuzz findings, then remove only the inspected
inactive generation with `docker volume rm EXACT_NAME`. Source volumes contain reacquirable
locked bytes, but active writer claims still require reconciliation before removal. Keep the
current generation; a prefix-wide or global prune can destroy unrelated work and unique findings.
Ordinary `out/` build trees have the same explicit ownership requirement. Rebuildable objects
are distinct from retained results; a successful build does not authorize deleting all evidence.

## Cancellation control scope

C++ tests cancel before admission and at interior numerical, codec and publication checkpoints.
They assert worker joins/error precedence, source preservation, workspace refunds, owned cleanup,
precommit cancellation and success/refusal/uncertainty after a late request. The decoder tests enumerate
every checkpoint for both ordinary and Adam7 input; encoding tests enumerate checkpoints through
successful publication. Negative schema tests reject contradictory cancellation responses.

A separate test executable links the same production interrupt bridge, CLI and host. Its readiness
handshake lets a parent deliver genuine SIGINT/SIGTERM on POSIX or targeted CTRL_BREAK on Windows
before dispatch. POSIX tests also verify inherited ignored dispositions and restoration. In-process
checkpoint tests cover exact processing phases; the process test is not a claim of hitting each phase
with a timed OS signal. The manifest-declared cancellation fuzzer generates deterministic stop points
without filesystem effects or uncontrolled process signals, checking refunds and successful outputs
against existing independent sample references. Normal method/reference suites continue checking
unchanged no-stop numerical behavior.

These are coverage definitions, not a statement that every platform has already passed. Actual results
belong to the tested commit and its CI logs. Cooperative cancellation is not a hard realtime guarantee:
foreign-library work, allocator calls, blocking reads/writes and filesystem operations can delay a
checkpoint. A blocked delivery stream can delay exit too. No cleanup guarantee is made for SIGKILL,
TerminateProcess, console close/logoff/shutdown, crash or power failure. Charged-buffer accounting is
not a process-RSS bound. Partial internal buffers are discarded, not certified as faithful documents.


## Denoising verification scope

The [current denoising guide](methods/denoising.md) owns D01/D02 arithmetic, reflection, protection,
quantization and workspace semantics. Actual first-party resource, independent-reference, moved-owner,
codec-profile and cross-worker controls cover those owners. The [native dependency
policy](dependencies.md) still owns source identity, isolation, attribution and feature closure.

## Evidence and qualification

Required healthy and negative controls cover actual admitted plans, immutable completed facts,
borrow readiness, finite reservations/refunds, interruption and truthful publication/delivery.
The fixed eager pipeline retains only actual completed earlier facts and the failed family.
[Architecture](architecture.md), [pipeline](pipeline.md), [record](record.md) and the method guides
own those properties; tests exercise their real owners rather than a second observation model.

Exact-commit required platform and minimum-runtime evidence, relocated packages, native feature and
import closure, both strict engines, retained regressions and sanitizer controls remain required.
A workflow definition, local pass or identity hash is not execution, trust or natural-document evidence.
[Document quality](document-quality.md) separately requires cleared provenance, independent physical
pages, complete per-class denominators, blinded review and separate harms, uncertainty and abstention.
Source/package eligibility and signed/notarized native distribution retain their separate requirements.
