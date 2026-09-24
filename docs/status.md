# Status

The executable implements **continuous-tone PNG representation**, **B02 Sauvola**, and
**B03 fixed-threshold binarization**. It is not a complete restoration suite.

## Implemented product paths

`process INPUT --out-dir DIRECTORY` defaults to preserve-mode continuous-tone PNG conversion, with
no enhancement filter. Static gray, palette, RGB and alpha PNGs support 8/16-bit precision, bounded
profile interpretation, linear-light compositing, exact metadata orientation and minimal canonical
output metadata. Output rows and metadata are independently verified before exclusive publication.
See [PNG processing](png-processing.md) for precise domains, limits and explicit assumptions.

`--output-mode bw` activates B02/B03; default selection is Sauvola. Their existing stored-gray
sample semantics and 1/2/4/8-bit nontransparent PNG input domain are unchanged, and broader input
support is not implied for that branch. See [typed binarization](binarization.md).

The application validates a typed request; the production host owns execution; the CLI and report
layers own transport and presentation. Buffer lifetime, borrowed views, scheduler failure handling,
codec allocations and exclusive publication have explicit contracts and regression tests.
Cooperative cancellation is carried through the pipeline, with native interrupt handling and a
publication cutoff; see [cancellation](cancellation.md).
See [architecture](architecture.md) for exact limits and the publication trust/durability boundary.

## Reusable components

C++23/CMake target boundaries, a verified offline source lock and isolated native dependency builds;
strict warnings and linters; generated method/argument contracts; deterministic scalar primitives;
checked aligned planes; budget accounting; bounded indexed scheduling; a box-mean primitive;
reference/property tests and manifest-declared engine-independent fuzz harnesses, including
raw binary/continuous PNG decoding, raw ICC parsing/transforms, independently generated exact-sample PNG checks and a direct-window Sauvola oracle. See [fuzzing](fuzzing.md).

Sauvola and the box-mean primitive use the internal scheduler; the public CLI does not expose
`--threads`, batching or arbitrary recipes. OpenCV, Leptonica, JPEG and TIFF are linked
and exercised by a development probe, not silently advertised as complete processing support.

## Verification is commit-specific

A local build or successful reference suite is not certification of all platforms. A created PR is
not evidence that its CI passed.

Required CI includes Linux x86-64/ARM64, macOS Intel/ARM64 and Windows x86-64 native builds,
real-executable contracts, structural/tooling checks, fuzzing, and independent ASan/UBSan and
TSan suites. Native sanitizer coverage is first-party coverage; isolated fuzzing additionally instruments PNG, zlib and Little CMS.
The macOS deployment target is 14.0; actual execution on every older supported OS, signing,
notarization, performance/peak-RSS measurement and document-quality benchmarks remain release
validation work, not capabilities or results claimed by this source tree.

## Not implemented

Other complete methods, multipage processing, JPEG/TIFF input,
dewarping, deskewing, automatic method selection, OCR, batching, presets and streaming/tiling
for arbitrarily large pages. No neural inference, GPU requirement, network service, GUI,
database or plugin framework is introduced. Planned numerical definitions remain in the method
reference and must earn capability admission through implementation and executable tests.
