# CI performance, source reuse and replay compilation

## Observed baseline

GitHub job/step timestamps and completed job logs were inspected on 2026-10-09. These measurements
describe earlier runs, not validation of these changes or a guaranteed runner latency.

| Quality gates run | Result | Run elapsed | macOS Intel job | Next longest job |
| --- | --- | --- | --- | --- |
| [Main 37821614990](https://github.com/resoltico/docenhance/actions/runs/37821614990) | Success | 40m48s | 40m35s | TSan 29m59s |
| [PR 37815692699](https://github.com/resoltico/docenhance/actions/runs/37815692699) | Success | 44m46s | 43m55s | TSan 30m18s |

Run elapsed uses creation through final update; job durations use start through completion.
The Intel job was the final prerequisite for the required gate in both runs. Both restored the
existing verified LLVM cache: restore took 19/21 seconds and LLVM installation took one second.
These samples contain no cold LLVM source-build measurement.

The [main Intel log](https://github.com/resoltico/docenhance/actions/runs/37821614990/job/113463785582)
separates the release workflow into approximately 30 seconds of outer configuration, 30m26s of
building, 7m56s of tests and three seconds of packaging. Dependency preparation within the build
took about 13m21s, including 8m59s of OpenCV compilation; the first-party build with mandatory
clang-tidy took about 16m39s. The architecture test took 293 seconds and tooling took 110 seconds.
The relocated package smoke test took 13 seconds. In the
[earlier Intel log](https://github.com/resoltico/docenhance/actions/runs/37815692699/job/113443925502),
OpenCV compilation took 292 seconds and the architecture test took 632 seconds, showing substantial
host variability even with warm LLVM tools.

Dependency acquisition took 38/43 seconds on Intel and typically tens of seconds on other jobs.
Reusing sources can avoid network acquisition, but restoration and verification still cost time.
Expected savings are modest and must be measured on subsequent cold/warm runs. Source reuse does
not remove the approximately 30-minute Intel build or its test phase. AFL++ tool compilation,
sanitizer builds and both complete fuzz campaigns also remain.

A local macOS ARM round-trip packed the source-cache payload to 230 MiB in 13.94 seconds,
extracted it in 8.98 seconds and verified the restored lock in 2.99 seconds. This measures local
archive and verifier work, not a hosted Actions cache round-trip. A warm hit still needs roughly
12 seconds of extraction/verification plus host transfer; the initial miss adds packing and upload
to ordinary acquisition. Against the Intel sample's 38-second acquisition step, the net saving
remains unmeasured and may be small or absent. Measure hosted hit/miss steps before claiming a
speedup.

## Design and separate challenge

### Source reuse

Reuse acquired sources instead of native products. Their dependency-source cache contains only
`.cache/deps/archives`, `.cache/deps/sources` and `.cache/deps/receipts`, uses an exact OS key bound
to `deps/lock.json` and the acquisition/verification implementation, and has no prefix-match
fallback. The existing verified Intel LLVM tool cache remains separate. Active writer claims are
excluded. Architecture does not change upstream source bytes;
native configuration identity remains the responsibility of each fresh build and private prefix.

Every restored cache passes unconditional acquisition before build setup. Existing verification
checks Git release objects and source inventories, and derives archive inventories from the
original digest-checked archive. Receipts alone cannot establish source identity. Save an exact
miss only after complete acquisition succeeds; cancellation or failure before that boundary cannot
publish partially acquired state. Changed existing sources or missing receipts fail visibly rather
than being repaired; absent sources are acquired through the ordinary locked path. The cache is
working state, not a second source authority.

The skeptical boundary is a cache hit: neither its key nor the cache action's success proves its
contents. Workflow guards must reject skipped acquisition, broadened cache paths, fallback keys
and saves before successful verification. Source mutation, missing receipts and archive/receipt
mutation must still be rejected by the real verifier. Fresh builds retain actual compilation,
lint, AST, sanitizer, complete test, fuzz exposure and relocated-package evidence at their existing
platform/engine boundaries; the required coverage matrix is unchanged.

Restoring `out`, installed prefixes or prior results would cross a different integrity boundary
and is outside this change. Compiler-cache launchers remain refused by the
[build configuration contract](build.md#configuration-ownership). Parallelizing every dependency
project would multiply child worker counts; the serial superbuild intentionally avoids that
oversubscription. Further performance work should measure native compilation and architecture
validation on a suitable faster Intel runner while retaining the complete required coverage.

### Shared replay driver

The native suite has 23 separately registered corpus-replay executables, each of which previously
compiled and ran mandatory clang-tidy on the same `fuzz/support/replay_main.cpp` driver. Compile
that driver once as the configuration-private `de_fuzz_replay_driver` OBJECT library and include
its object in every replay executable. This removes 22 duplicate compile/lint invocations without
sharing native products between configurations or changing the harnesses, corpora, regressions or
replay registrations.
The driver retains the existing compile options, warnings, hardening, lint and requested sanitizer
instrumentation; every harness retains its own target options and links.

Retained macOS ARM64 `.ninja_log` action durations from `out/g02-release/app`,
`out/g02-sanitize/app` and `out/g02-tsan/app` charged 248.0, 370.1 and 376.0 seconds respectively
in aggregate to the 23 repeated driver compile/lint invocations; these builds used four workers.
Removing 22 of 23 copies corresponds to ideal four-worker work reductions of approximately 59,
88 and 90 seconds respectively. These are estimates of avoided work divided by worker capacity,
not measured end-to-end savings: scheduling, contention and the
remaining dependency/application/AST work determine the actual critical path.

The separate challenge is effective ownership: attaching an object must not remove mandatory
options or allow a driver built under another configuration to supply the replay binaries.
Verify the generated compiler/lint rule, requested sanitizer instrumentation and object inclusion
in all 23 executables, then retain complete replay discovery and fresh results. A smaller source
count or successful linking alone cannot prove those properties. Full native suites, sanitizer
controls and both independent engine campaigns remain unchanged.

### Tooling fixture Git ownership

Fresh tooling workers remove inherited `GIT_*` variables before module discovery, so hook-exported
repository identities cannot redirect temporary-repository Git writes into the caller's checkout.
The parent environment is unchanged, and fixtures can still supply deliberate Git overrides after
discovery. A real sentinel-repository regression checks metadata preservation and correct fixture
ownership; removing worker cleanup redirects the writes and makes the control fail. Process
isolation therefore includes inherited repository identity as well as interpreter state.
