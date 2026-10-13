# CI source caching and replay ownership

## Verified source reuse

Dependency caches contain only `.cache/deps/archives`, `.cache/deps/sources` and `.cache/deps/receipts`. Their exact OS-specific key binds the source lock and acquisition/verification implementation; no prefix-match fallback is admitted. Active writer claims are excluded. [Dependency policy](dependencies.md) owns verification and recovery.

Acquisition runs after every restore. Locked Git release objects, archive digests and complete inventories must agree before build setup. Changed existing sources or missing receipts fail; absent sources use ordinary locked acquisition. An exact miss is saved only after successful acquisition. Cache keys and restore success do not establish source trust.

Native build trees, installed prefixes, objects, product binaries and previous results are not restored. Each configuration owns its native products and private prefix. The Intel LLVM tool cache is separate and requires its own source/recipe, readiness and byte-identity checks. Compiler reuse is not binary reproducibility. See [configuration ownership](build.md#configuration-ownership).

## Configuration-private replay work

`de_fuzz_replay_driver` is an OBJECT library containing the shared `fuzz/support/replay_main.cpp` driver. It is compiled and linted once per configuration with the required warnings, hardening and sanitizer flags. Every manifest harness retains its own target options, direct links and corpus-replay registration. No product or result is shared between configurations.

Complete discovery, actual compiler/linter work, seed provenance, requested fuzz exposure, fresh results and relocated package inspection remain required. Work sharing does not justify omitting a harness, assertion, compiler boundary or required platform. Actual elapsed time depends on machine and workload; no speedup or completion-time guarantee is claimed.

## Tooling fixture ownership

Each tooling module runs in a fresh bounded interpreter. Workers clear inherited `GIT_*` repository overrides before discovery so temporary Git fixtures own their repositories; parent state and deliberate fixture overrides remain intact. The strict runner reconciles every discovered case and rejects skipped or expected-failure work. [Quality](quality.md) owns execution limits and outcome admission.
