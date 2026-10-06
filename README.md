# DocEnhance

**Local document-image restoration. CPU only. No neural networks.**

A C++ command-line project for improving the readability of contemporary and historical document images: handwriting, print, and mixed pages. Original code is MPL-2.0-licensed.

> **Capability boundary: PNG/JPEG/TIFF input to continuous-tone PNG representation, opt-in I01/I02 illumination and D01 denoising, and two binarizers.**
> `process` defaults to color-managed PNG output with no enhancement filter. Static grayscale,
> palette, RGB and alpha PNGs retain 8/16-bit precision under the documented profile, transparency
> and metadata policies. Explicit `--output-mode bw` selects B02/B03 on their narrower stored-gray
> input domain. All output is published into a new directory. See [PNG processing](docs/png-processing.md)
> [JPEG admission](docs/jpeg-processing.md), [bounded single-page TIFF/BigTIFF](docs/tiff-processing.md),
> and [typed binarization](docs/binarization.md). Batch processing, presets and
> other restoration methods remain unsupported. I01 surface/auto and explicit I02 morph illumination and protected
> regions are specified in [illumination](docs/illumination.md); the default remains off.

## Start here

Use the [documentation map](docs/README.md) to find the authority for a change. Read
[status](docs/status.md) for verified capability boundaries, [architecture](docs/architecture.md),
[build instructions](docs/build.md), [quality gates](docs/quality.md), and the
[roadmap](docs/roadmap.md) before extending the program.

## Modern native architecture

CMake **4.4.3** and Ninja **1.13.2** are the recommended build tools. The project uses CMake preset schema **12**, configure/build/test/package workflows, target-scoped settings, explicit source lists, header file sets, strict first-party diagnostics, optional sanitizers and IPO, and an isolated dependency superbuild. There are no handwritten Makefiles, global include/link-directory settings, implicit system-library fallbacks, or downloads during normal configure/build commands.

The dependency lock pins OpenCV **5.0.0**, Leptonica **1.87.0**, Little CMS **2.19.1**, CLI11 **2.7.2**, libjpeg-turbo **3.2.0**, libpng **1.6.58**, libtiff **4.7.2**, zlib **1.3.2**, nlohmann/json **3.12.0**, PicoSHA2 **1.0.1**, and test-only Catch2 **3.16.0**. See [dependency provenance](docs/dependencies.md).

OpenCV 5 changed its dependency graph: `photo` requires `geometry`, which requires `flann`. The explicit allowed module closure is `core`, `flann`, `geometry`, `imgproc`, `photo`—not the obsolete three-module assumption. Neural/GPU/GUI/video backends and optional external acceleration downloads are disabled.

## Build

Prerequisites: a current C++23 compiler and standard library, Git, Python 3.12 or later **for development tooling only**, CMake 4.4 or later, and Ninja 1.13 or later. `dev`, `sanitize` and `tsan` require the supported LLVM clang major declared in `deps/tools.json` rather than the host `c++`; install it with `python tools/install_llvm.py --compiler` and name it in `CC`/`CXX`. `release` builds with the platform's own toolchain. Native packages do not require Python. NASM is optional for libjpeg-turbo's x86 SIMD; the upstream non-SIMD fallback is permitted.

Install and activate the external tool environment in the [build guide](docs/build.md), then run
from the source root (a Git checkout or an extracted source archive):

```sh
# Explicit, separately authorized online acquisition; checks immutable release pins.
cmake -P cmake/AcquireDependencies.cmake

# The analysis compiler; on macOS use "$(brew --prefix llvm)/bin/clang[++]".
export CC=clang-23 CXX=clang++-23

# Offline configure → isolated native dependency build → application → tests.
cmake --workflow --preset dev

# Select the platform compiler for a fresh release tree (not the exported analysis compiler).
# macOS: CC=/usr/bin/clang CXX=/usr/bin/clang++
CC=gcc CXX=g++ cmake --workflow --preset release
```

The executable is written to `out/dev/app/bin/docenhance` (`docenhance.exe` on Windows). Use a Visual Studio C++ developer shell on Windows. The complete [build guide](docs/build.md) covers tool installation, compiler selection, sanitizers, package smoke tests and failure recovery.

## Quality gates

Every quality check is strict and pinned. GitHub runs the native validation set and a separate
source-archive workflow; neither workflow publishes a binary release:

```sh
python tools/install_build_tools.py --lint   # once, in a virtual environment
python tools/install_llvm.py --compiler      # the supported LLVM tools and sanitizer runtimes
python tools/check_all.py                    # source checks and Docker Linux native verification on macOS/Windows
cmake --workflow --preset dev                # build and test, with clang-tidy on every target
cmake --workflow --preset fuzz               # strict fuzzing of the parsers and the command line
```

clang-tidy 23, clang-format, Ruff with every rule and strict mypy run as errors, never warnings.
Findings are fixed rather than suppressed: a suppression must name its rule and be registered with a
reason, file-size limits have no waivers, and no translation unit or target may escape linting.
Sanitizer builds abort on any report, and manifest-declared fuzz harnesses check correctness properties against
independent references under libFuzzer and AFL++, with their corpora replayed by every build. See
[quality gates](docs/quality.md) and [design decisions](docs/decisions.md).

## Current CLI

```sh
out/dev/app/bin/docenhance --help
out/dev/app/bin/docenhance version --json
out/dev/app/bin/docenhance methods --json
out/dev/app/bin/docenhance process --help --json
```

`methods` reports I01 surface and I02 morphological illumination, D01 denoising, B02 Sauvola and B03 fixed-threshold binarization; `version --json` reports PNG/JPEG/TIFF with an explicit operation/format matrix. JPEG is continuous-only; binary processing remains grayscale PNG. The complete contract is in the [CLI reference](docs/cli-contract.md), and its
strict capability boundary is documented in [current CLI behavior](docs/cli.md).

## Project layout

```text
src/<layer>/          One directory per layer, with its public headers under
include/docenhance/   Layers and their allowed edges: spec/architecture.json, docs/architecture.md
spec/                 Reviewed authoring contracts: the CLI, the planned methods, the layer graph
schemas/              Generated JSON response schema, enforced by executable tests
cmake/                Modern native build, isolation, policies, packaging
cmake/opencv-hooks/   Reject nested OpenCV downloads
cmake/presets/        Shared schema-12 preset definitions
deps/                 Immutable source lock and explicit feature policy
tests/                Unit, numeric, CLI and acquisition/security tests
fuzz/                 Strict fuzz harnesses, corpora and regressions (libFuzzer, AFL++)
tools/                Build-only standard-library Python automation
docs/                 Status, architecture, decisions, build, quality, CLI, bundles and roadmap
.github/              CI, source packaging, issue/PR templates and updates
```

## Verification status

Validation is commit-specific. The required GitHub jobs exercise structural checks, native tests
and package smoke tests on Linux x86-64/ARM64, macOS Intel/ARM64 and Windows, plus sanitizer and
fuzz campaigns. See [status](docs/status.md) for capability and verification boundaries. Numerical
reference agreement is not certification of readability or fidelity on arbitrary real documents.

## Publishing source

This repository supports source releases only. For a `v*` tag, the `Package source archive`
workflow validates and builds a deterministic source tarball, attests it, and publishes the source
release. Its GitHub release body is the exact dated section of [CHANGELOG.md](CHANGELOG.md), not a
separate notes file or generated summary. The publisher never edits an existing release; it rejects
different prose or source assets. Source publication is not publication of prebuilt binaries or proof of document-enhancement quality.

## Contributing and security

See [CONTRIBUTING](CONTRIBUTING.md), [AGENTS](AGENTS.md), the [security policy](.github/SECURITY.md) and the [code of conduct](.github/CODE_OF_CONDUCT.md). Preserve originals, reject unsupported operations explicitly, and never advertise a method merely because a placeholder compiles.

## License

Project-owned code is licensed under [MPL-2.0](LICENSE). Copyright notices identify the
holders of individual files; dependencies retain their own licenses. See
[licensing and distribution](docs/licensing.md) and [third-party notices](THIRD_PARTY_NOTICES.md)
for scope, attribution and distributor obligations.

Explicit luminance denoising composes after illumination and before final quantization:

```sh
docenhance process scan.jpg --out-dir denoised --denoise nlm --nlm-h 3 --denoise-blend 0.5 --json
```

D01 is bounded 16-bit NLM-L1, off by default. Protected pixels retain their entering linear samples;
protected neighbors remain context. It is not JPEG restoration or a guarantee of mark preservation.
See [the complete denoising contract](docs/denoising.md). Records and responses use format 3;
obsolete records are rejected without migration.
