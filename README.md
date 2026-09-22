# DocEnhance

**Local document-image restoration. CPU only. No neural networks.**

A C++ command-line project for improving the readability of contemporary and historical document images: handwriting, print, and mixed pages. Original code is MIT-licensed.

> **Development status: capability-limited preview, not a usable image enhancer.**
> The build graph, dependency acquisition, CLI shell, contracts, architectural boundaries, numerical reference primitives, test infrastructure and packaging are present. Complete image decoders, processing methods and output-bundle publication are **not implemented**. Processing commands fail explicitly without opening input images or writing results. Do not use this version for document processing.

## Start here

Read [status](docs/status.md) for what exists and what has been verified, [build instructions](docs/build.md), [quality gates](docs/quality.md), and the [roadmap](docs/roadmap.md) for what comes next. [Design decisions](docs/decisions.md) explains why the project is built this way.

## Modern native architecture

CMake **4.4.3** and Ninja **1.13.2** are the recommended build tools. The project uses CMake preset schema **12**, configure/build/test/package workflows, target-scoped settings, explicit source lists, header file sets, strict first-party diagnostics, optional sanitizers and IPO, and an isolated dependency superbuild. There are no handwritten Makefiles, global include/link-directory settings, implicit system-library fallbacks, or downloads during normal configure/build commands.

The dependency lock pins OpenCV **5.0.0**, Leptonica **1.87.0**, Little CMS **2.19.1**, CLI11 **2.7.2**, libjpeg-turbo **3.2.0**, libpng **1.6.58**, libtiff **4.7.2**, zlib **1.3.2**, nlohmann/json **3.12.0**, PicoSHA2 **1.0.1**, and test-only Catch2 **3.16.0**. See [dependency provenance](docs/dependencies.md).

OpenCV 5 changed its dependency graph: `photo` requires `geometry`, which requires `flann`. The explicit allowed module closure is `core`, `flann`, `geometry`, `imgproc`, `photo`—not the obsolete three-module assumption. Neural/GPU/GUI/video backends and optional external acceleration downloads are disabled.

## Build

Prerequisites: a current C++23 compiler and standard library, Git, Python 3.12 or later **for development tooling only**, CMake 4.4 or later, and Ninja 1.13 or later. Native packages do not require Python. NASM is optional for libjpeg-turbo's x86 SIMD; the upstream non-SIMD fallback is permitted.

With the prerequisites available, run from the repository root:

```sh
# Explicit, separately authorized online acquisition; checks immutable release pins.
cmake -P cmake/AcquireDependencies.cmake

# Offline configure → isolated native dependency build → application → tests.
cmake --workflow --preset dev

# Build/test/package a development release, not the final 1.0 enhancer.
cmake --workflow --preset release
```

The executable is written to `out/dev/app/bin/docenhance` (`docenhance.exe` on Windows). Use a Visual Studio C++ developer shell on Windows. The complete [build guide](docs/build.md) covers tool installation, compiler selection, sanitizers, package smoke tests and failure recovery.

## Quality gates

Every quality check is strict and pinned. GitHub runs the native validation set and a separate
source-archive workflow; neither workflow publishes a binary release:

```sh
python tools/install_build_tools.py --lint   # once, in a virtual environment
python tools/check_all.py                    # structure, gates, format, lint, types, tooling tests
cmake --workflow --preset dev                # build and test, with clang-tidy on every target
cmake --workflow --preset fuzz               # strict fuzzing of the parsers and the command line
```

clang-tidy 23, clang-format, Ruff with every rule and strict mypy run as errors, never warnings.
Findings are fixed rather than suppressed: a suppression must name its rule and be registered with a
reason, file-size limits have no waivers, and no translation unit or target may escape linting.
Sanitizer builds abort on any report, and four fuzz harnesses check correctness properties against
independent references under libFuzzer and AFL++, with their corpora replayed by every build. See
[quality gates](docs/quality.md) and [design decisions](docs/decisions.md).

## Current CLI

```sh
out/dev/app/bin/docenhance --help
out/dev/app/bin/docenhance version --json
out/dev/app/bin/docenhance methods --json
out/dev/app/bin/docenhance process --help --json
```

`methods` intentionally reports no completed methods. The target interface is fully cataloged in the [CLI reference](docs/cli-contract.md); the distinction between accepted syntax and implemented behavior is documented in [current CLI behavior](docs/cli.md).

## Project layout

```text
src/<layer>/          One directory per layer, with its public headers under
include/docenhance/   Layers and their allowed edges: spec/architecture.json, docs/architecture.md
spec/                 Reviewed authoring contracts: the CLI, the planned methods, the layer graph
schemas/              JSON schema of command responses, enforced by the CLI tests
cmake/                Modern native build, isolation, policies, packaging
cmake/opencv-hooks/   Reject nested OpenCV downloads
cmake/presets/        Shared schema-12 preset definitions
deps/                 Immutable source lock and explicit feature policy
tests/                Unit, numeric, CLI and acquisition/security tests
fuzz/                 Strict fuzz harnesses, corpora and regressions (libFuzzer, AFL++)
tools/                Build-only standard-library Python automation
docs/                 Status, architecture, decisions, build, quality, CLI and roadmap
.github/              CI, source packaging, issue/PR templates and updates
```

## Verification status

The locked dependency build, the real binary, its tests, the package and its relocated smoke test run on macOS arm64 and, in containers, on Linux arm64, with strict linting, sanitizers and fuzzing. GitHub Actions also runs the source-archive workflow; native runner results are tracked in [status](docs/status.md). The workflows package only for validation; no binary release is published.

## Publishing source

This repository supports source releases only. For a `v*` tag, the `Package source archive`
workflow validates and builds a deterministic source tarball, attests it, and publishes the source
release. Its GitHub release body is the exact dated section of [CHANGELOG.md](CHANGELOG.md), not a
separate notes file or generated summary. The publisher never edits an existing release; it rejects
different prose or source assets. Do not represent this preview as a usable image enhancer.

## Contributing and security

See [CONTRIBUTING](CONTRIBUTING.md), [AGENTS](AGENTS.md), the [security policy](.github/SECURITY.md) and the [code of conduct](.github/CODE_OF_CONDUCT.md). Preserve originals, reject unsupported operations explicitly, and never advertise a method merely because a placeholder compiles.

## License

DocEnhance is copyright 2026 Ervins Strauhmanis and MIT-licensed; bundled dependencies retain their own licenses. See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md). The repository contains no vendored dependency sources or compiled libraries. Native-package generation copies original upstream license texts and emits a declared-source SPDX inventory; final binary-composition and distribution review remains a release requirement.
