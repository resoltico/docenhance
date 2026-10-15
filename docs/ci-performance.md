# CI source caching and replay ownership

## Verified source reuse

Dependency caches contain only `.cache/deps/archives`, `.cache/deps/sources` and `.cache/deps/receipts`. Their exact OS-specific key binds the source lock and acquisition/verification implementation; no prefix-match fallback is admitted. Active writer claims are excluded. [Dependency policy](dependencies.md) owns verification and recovery.

Acquisition runs after every restore. Locked Git release objects, archive digests and complete inventories must agree before build setup. Changed existing sources or missing receipts fail; absent sources use ordinary locked acquisition. An exact miss is saved only after successful acquisition. Cache keys and restore success do not establish source trust.

Native build trees, installed prefixes, objects, product binaries and previous results are not restored. Each configuration owns its native products and private prefix. The Intel LLVM tool cache is separate and requires its own source/recipe, readiness and byte-identity checks. Its outer key fingerprints the actual macOS version and selected developer directory after Xcode selection, matching the installed generation; a stale outer hit cannot repeatedly rebuild an unsaved inner generation. Only `bin/clang-tidy`, `bin/clang-query`, matching builtin resource headers and their digest receipt survive the Intel source build; source and object trees live in the installer temporary directory. Static LLVM/Clang linking and the retained resource headers keep the tools independent of that temporary build. Unexpected cache entries fail readiness verification. Header inspection admits at most 4,096 entries, 64 MiB of logical bytes and 1,024 UTF-8 bytes per relative path; receipts admit 8 MiB. These finite allowances retain the full builtin header package rather than guessing a smaller compiler include closure. Compiler reuse is not binary reproducibility. See [configuration ownership](build.md#configuration-ownership).

## Configuration-private replay work

`de_fuzz_replay_driver` is an OBJECT library containing the shared `fuzz/support/replay_main.cpp` driver. It is compiled and linted once per configuration with the required warnings, hardening and sanitizer flags. Every manifest harness retains its own target options, direct links and corpus-replay registration. No product or result is shared between configurations.

Complete discovery, actual compiler/linter work, seed provenance, requested fuzz exposure, fresh results and relocated package inspection remain required. Work sharing does not justify omitting a harness, assertion, compiler boundary or required platform. Actual elapsed time depends on machine and workload; no speedup or completion-time guarantee is claimed.

## Tooling fixture ownership

Each tooling module runs in a fresh bounded interpreter. Workers clear inherited `GIT_*` repository overrides before discovery so temporary Git fixtures own their repositories; parent state and deliberate fixture overrides remain intact. The strict runner reconciles every discovered case and rejects skipped or expected-failure work. [Quality](quality.md) owns execution limits and outcome admission.

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
