# Status

What exists, what does not, and what has actually been run. This page is kept current; it is not a
log of past sessions.

**The product is capability-limited, not a usable image enhancer.** Every processing command fails with
`E_NOT_IMPLEMENTED` before opening an input or writing a result.

## What exists

| Area | State |
|---|---|
| Isolated superbuild, presets and packaging | Working: full locked dependency build, tests and relocatable package |
| Pinned dependency acquisition and verification | Working: every source acquired, digest-inventoried and re-verified before each build |
| CLI shell: help, version, capability discovery | Working: the adapter parses, `de_app` decides in types, `de_report` renders, and responses are validated against `schemas/command-response.schema.json` |
| Argument contract: 69 arguments, 17 planned methods | Parsed and documented; generated into help, reference docs and the fuzzing dictionary |
| Numeric and parser reference primitives | Working and property-fuzzed against independent references |
| First image kernel (`methods::box_mean`) | Working: separable moving average with reflected borders, checked against the summed definition and bitwise-stable across worker counts. A shared primitive, not an advertised method |
| Schedule for page-internal parallelism | Working: `--threads` resolved against machine and budget, deterministic indexed work, earliest-failure reporting; no kernel uses it yet |
| Memory model for large pages | Working: budgeted, aligned, move-only buffers and planes; allocation failure is `E_RESOURCE`, and only `de_core` may allocate |
| Imaging-library memory and threading policy | Hooks proven by `dependency-native-link`: OpenCV computes into memory we own, Leptonica allocates through us, the codecs take explicit limits, and no library writes to stdout |
| Architectural boundaries | One manifest (`spec/architecture.json`), enforced on link edges, the real include graph, the real syntax tree, public interfaces and header self-containment |
| Strict linting, quality gates, sanitizers, fuzzing | Enforced in the build and local hooks; configured for GitHub CI ([quality gates](quality.md)) |
| Typed configuration, presets, recipes | Not implemented |
| JPEG/PNG/TIFF codecs and the colour pipeline | Not implemented |
| Enhancement, restoration and geometry methods | Not implemented |
| Batch scheduling, provenance, result publication | Not implemented |
| Signed, minimum-OS-tested distribution | Not available |

Nothing advertises a capability it lacks: `methods` and `supported_formats` are empty, and no
placeholder returns success by copying its input.

## What has been verified

Last local run: 2026-09-22 on macOS arm64. The protected GitHub quality gate also passed on
2026-09-22 across Linux x86-64/ARM64, macOS ARM64/Intel, and Windows x86-64.

| Check | Result |
|---|---|
| `dev` workflow | 16 tests pass, with clang-tidy on every first-party target |
| `sanitize` workflow | Whole suite passes with ASan and UBSan fatal on first report |
| `tsan` workflow | Whole suite passes under ThreadSanitizer, which was proven active: a deliberate race in the schedule test produced four reports and failed the run |
| `release` workflow and relocated package smoke test | Package runs from a fresh directory and declares minimum macOS 14.0 |
| `fuzz` workflow (libFuzzer) | Five targets, 60 s each; a 10-minute-per-target campaign ran ~54 million inputs with no findings |
| AFL++ 5.03c | Built from its pinned commit; four targets, 60 s each, no findings |
| Linux structural job | Ruff, mypy, clang-format, the gates, the source architecture rules, tooling tests, and the GCC and sanitized-clang reference suites, the latter with the pinned clang 23 |
| Linux native job | Full superbuild, 16 tests, packaging and the relocated smoke test; the architecture rules pass there under GCC 13 with the pinned `clang-query` |
| GitHub native matrix | Full superbuild, tests, package and relocated-package smoke test passed on Linux x86-64/ARM64, macOS ARM64/Intel and Windows x86-64 under the protected quality gate |
| Linux leak detection | Confirmed active: a deliberately leaking binary aborted with a LeakSanitizer report |
| Kernel property fuzzing | 50,349 inputs over planes, radii and worker counts with no finding: every sample matched the summed definition, and more workers never changed a bit |
| GCC 16 strict warnings | Every harness and first-party source compiles with the full warning set as errors |
| Imaging dependency policy | Probed on macOS arm64: OpenCV wraps owned memory with no allocation, Leptonica's memory manager is used, Little CMS contexts work, libjpeg and libpng accept limits, and stdout stays free of library diagnostics |
| Architecture rules | Pass, and were checked in both directions: a forbidden link, a forbidden package, a cross-layer include, a fictional link, a missing link, a `throw`, a `catch`, a foreign namespace and a header that needs its includer were each introduced and each was rejected |
| Pinned `clang-query` on Linux | `clang-tidy-23` and `clang-tools-23` install from the fingerprint-checked apt.llvm.org repository and provide `clang-query-23` (23.1.2) |

## Remaining verification

- **GitHub source-archive validation passed** for the `v0.1.0` tag: its source checks, deterministic
  package, provenance attestation and artifact upload completed successfully. The downloaded archive
  passed both checksum and GitHub attestation verification.
- **macOS Intel uses a source-pinned LLVM 23 tool build.** The hosted Intel Homebrew channel provides
  LLVM 22 and LLVM 23 publishes no Intel macOS release archive, so CI SHA-verifies the LLVM 23 source
  archive, builds only clang-tidy and clang-query, and caches that build by version and architecture.
- **macOS 14 and 15 were never run**: the binary is built for 14.0 and asserts it, but has only run
  on the hosted macOS 26 runners.
- **Thread control on macOS is not available through OpenCV**: its `parallel_for_` uses Grand
  Central Dispatch, which ignores a requested thread count. `--threads` is therefore honoured by
  `de_exec` instead; the scheduler and its guarantees are implemented and tested, but no kernel
  uses it yet, and it has never run real image work.
- Memory behaviour under real page sizes — peak usage, fragmentation, tiling for pages larger than
  the budget — has not been measured, because no page is ever decoded yet.
- Signing, notarization, performance, package size and document-quality results: none measured.

Reproducing a Linux job needs only Docker and `python tools/package_source.py`; each job's steps are
the commands in `.github/workflows/`. For example, the fuzz job:

```sh
docker run --rm -v "$PWD/dist:/src:ro" -w /work ubuntu:24.04 bash -euxc '
  apt-get update -q && apt-get install -y -q --no-install-recommends \
    python3 python3-venv git ca-certificates curl gpg sudo make build-essential
  tar -xzf /src/*.tar.gz --strip-components=1
  python3 -m venv /venv && . /venv/bin/activate
  python tools/install_build_tools.py && python tools/install_llvm.py --fuzzing
  python tools/deps.py fetch --dependency cli11 && python tools/deps.py fetch --dependency json
  CC=clang-23 CXX=clang++-23 cmake --workflow --preset fuzz'
```
