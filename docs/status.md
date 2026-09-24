# Status

The executable implements **B02 Sauvola and B03 fixed-threshold binarization of a single grayscale PNG**.
It is a deliberately limited document-image processor, not a complete restoration suite.
Unsupported formats and methods fail explicitly; linked dependencies do not count as capabilities.

## Implemented product path

`process INPUT --out-dir DIRECTORY --binarize fixed|sauvola [METHOD OPTIONS] [--json]` accepts
1/2/4/8-bit grayscale PNG without transparency. It preserves stored grayscale sample semantics,
expands low bit depths, and publishes one 8-bit `result.png` into a new directory without replacing
an existing destination. [Typed binarization](binarization.md) specifies both methods, defaults,
normalized parameters and the bounded rolling-statistics kernel. Input dimensions, file size and shared
image/codec allocations are bounded. Help, version and method discovery perform no file I/O.

The application validates a typed request; the production host owns execution; the CLI and report
layers own transport and presentation. Buffer lifetime, borrowed views, scheduler failure handling,
codec allocations and exclusive publication have explicit contracts and regression tests.
See [architecture](architecture.md) for exact limits and the publication trust/durability boundary.

## Reusable components

C++23/CMake target boundaries, a verified offline source lock and isolated native dependency builds;
strict warnings and linters; generated method/argument contracts; deterministic scalar primitives;
checked aligned planes; budget accounting; bounded indexed scheduling; a box-mean primitive;
reference/property tests and manifest-declared engine-independent fuzz harnesses, including
raw PNG decoding, independently generated exact-sample PNG checks and a direct-window Sauvola oracle. See [fuzzing](fuzzing.md).

Sauvola and the box-mean primitive use the internal scheduler; the public CLI does not expose
`--threads`, batching or arbitrary recipes. OpenCV, Leptonica, JPEG, TIFF and Little CMS are linked
and exercised by a development probe, not silently advertised as complete processing support.

## Verification is commit-specific

A local build or successful reference suite is not certification of all platforms. A created PR is
not evidence that its CI passed.

Required CI includes Linux x86-64/ARM64, macOS Intel/ARM64 and Windows x86-64 native builds,
real-executable contracts, structural/tooling checks, fuzzing, and independent ASan/UBSan and
TSan suites. Native sanitizer coverage is first-party coverage, not full codec instrumentation.
The macOS deployment target is 14.0; actual execution on every older supported OS, signing,
notarization, performance/peak-RSS measurement and document-quality benchmarks remain release
validation work, not capabilities or results claimed by this source tree.

## Not implemented

Other complete methods, color/alpha/16-bit handling, multipage processing, JPEG/TIFF input,
dewarping, deskewing, automatic method selection, OCR, batching, presets and streaming/tiling
for arbitrarily large pages. No neural inference, GPU requirement, network service, GUI,
database or plugin framework is introduced. Planned numerical definitions remain in the method
reference and must earn capability admission through implementation and executable tests.
