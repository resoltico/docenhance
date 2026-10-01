# Status

The executable admits **static PNG and bounded 8-bit Huffman baseline/progressive JPEG** into **continuous-tone PNG representation** with opt-in **I01 quantile
log-surface illumination** and **D01 bounded 16-bit NLM-L1 luminance denoising**, and **B02 Sauvola** and **B03 fixed-threshold binarization**.
It is not a complete restoration suite.

## Implemented product paths

`process INPUT --out-dir DIRECTORY` defaults to preserve-mode continuous-tone PNG conversion, with
no enhancement filter by default. Static gray, palette, RGB and alpha PNGs support 8/16-bit precision, bounded
profile interpretation, linear-light compositing, exact metadata orientation and minimal canonical
output metadata. Output rows and metadata are independently verified before exclusive publication.
JPEG admits gray/RGB/YCbCr through the same interpretation/I01 path; `bw` remains grayscale PNG only.
See [JPEG source admission](jpeg-processing.md) for complete coding, metadata, resource and failure rules.
I01 can be selected explicitly or through its opt-in automatic predicates, with original-depth
1/8-bit grayscale protection masks in oriented coordinates. Fitting, linear application and output
verification share explicit resource/cancellation contracts; see [illumination](illumination.md).
See [PNG processing](png-processing.md) for precise domains, limits and explicit assumptions.
D01 follows I01 before final quantization; protection is exact and native execution occurs once.
See [denoising](denoising.md) for numerical, resource, cancellation and record contracts.

`--output-mode bw` activates B02/B03; default selection is Sauvola. Their existing stored-gray
sample semantics and 1/2/4/8-bit nontransparent PNG input domain are unchanged, and broader input
support is not implied for that branch. See [typed binarization](binarization.md).

The application validates a typed request; the production host owns execution; the CLI and report
layers own transport and presentation. Buffer lifetime, borrowed views, scheduler failure handling,
codec allocations and exclusive publication have explicit contracts and regression tests.
Cooperative cancellation is carried through the pipeline, with native interrupt handling and a
publication cutoff; see [cancellation](cancellation.md).

Each result is published as a bundle: the image, a `run.json` recording the build, the identified
source bytes, the admitted request, the execution observations, the protection in force and the
verified output, and the canonical mask when one was supplied. The files are committed together, so
no result is published without its record. `docenhance verify DIRECTORY` reads one back against a
complete supported record, closed inventory and decoded artifact properties without executing
anything it finds. Staged bundles use that same validation; publication reconciles this run's
identity and manifest digest, retaining uncertainty and preserving known committed effects. Binary output is now read back and compared
before publication, as continuous output already was. Agreement between artifacts and their record
is not authenticity; see [processing bundles](bundles.md).
See [architecture](architecture.md) for exact limits and the publication trust/durability boundary.

## Reusable components

C++23/CMake target boundaries, a verified offline source lock and isolated native dependency builds;
strict warnings and linters; generated method/argument contracts; deterministic scalar primitives;
checked aligned planes; budget accounting; bounded indexed scheduling; a box-mean primitive;
reference/property tests and manifest-declared engine-independent fuzz harnesses, including
raw binary/continuous PNG decoding, raw ICC parsing/transforms, independently generated exact-sample PNG checks and a direct-window Sauvola oracle. See [fuzzing](fuzzing.md).

Sauvola and the box-mean primitive use the internal scheduler; the public CLI does not expose
`--threads`, batching or arbitrary recipes. OpenCV core/photo execute D01 behind the bounded denoising adapter. Leptonica and TIFF remain
development-probe packages without production processing admission.

## Verification is commit-specific

A local build or successful reference suite is not certification of all platforms. A created PR is
not evidence that its CI passed.

Required CI includes Linux x86-64/ARM64, macOS Intel/ARM64 and Windows x86-64 native builds,
real-executable contracts, structural/tooling checks, fuzzing, and independent ASan/UBSan and
TSan suites. Native sanitizer coverage is first-party coverage; isolated fuzzing additionally instruments JPEG, PNG, zlib and Little CMS.
The macOS deployment target is 14.0; actual execution on every older supported OS, signing,
notarization, performance/peak-RSS measurement and document-quality benchmarks remain release
validation work, not capabilities or results claimed by this source tree.

## Not implemented

Other complete methods, multipage processing, TIFF input,
dewarping, deskewing, automatic method selection, OCR, batching, presets and streaming/tiling
for arbitrarily large pages. No neural inference, GPU requirement, network service, GUI,
database or plugin framework is introduced. Planned numerical definitions remain in the method
reference and must earn capability admission through implementation and executable tests.
