# Status

The executable admits **static PNG and bounded 8-bit Huffman baseline/progressive JPEG, and bounded single-page TIFF/BigTIFF** into **continuous-tone PNG representation** with opt-in **I01 quantile
log-surface illumination** or explicit **I02 morphological illumination** and **D01 bounded 16-bit NLM-L1 luminance denoising** or **D02 floating-point TV-L1**, opt-in **C01 percentile levels / C02 gamma / C03 floating-point CLAHE contrast**, explicit **R01 known-PSF Fourier restoration**, explicit **S01 thresholded unsharp masking**, and **B01 global Otsu**, **B02 Sauvola** and **B03 fixed-threshold binarization**.
It is not a complete restoration suite.

## Implemented product paths

`process INPUT --out-dir DIRECTORY` defaults to preserve-mode continuous-tone PNG conversion, with
no enhancement filter by default. Static gray, palette, RGB and alpha PNGs support 8/16-bit precision, bounded
profile interpretation, linear-light compositing, exact metadata orientation and minimal canonical
output metadata. Explicit G02 `--rotate 0|90|180|270` follows G01 metadata orientation
with exact clockwise sample and protection permutations; see [geometry](pipeline.md). Output rows and metadata are independently verified before exclusive publication.
JPEG admits gray/RGB/YCbCr through the same interpretation and fixed pipeline. PNG, JPEG and TIFF
can all reach preserve, gray or binary output within each codec's admitted source domain.
TIFF admits the reviewed 1/8/16-bit sample, color, alpha and strip/tile domains through the same
continuous pipeline of geometry, illumination, denoising, restoration, contrast and sharpening; see [bounded TIFF admission](formats/tiff.md).
See [JPEG source admission](formats/jpeg.md) for complete coding, metadata, resource and failure rules.
I01 can be selected explicitly or through its opt-in automatic predicates, with original-depth
1/8-bit grayscale protection masks in oriented coordinates. Fitting, linear application and output
verification share explicit resource/cancellation contracts; see [illumination](methods/illumination.md#i01-surface-illumination).
See [PNG processing](formats/png.md) for precise domains, limits and explicit assumptions.
I02 is explicitly selected with `--illumination morph`; see [morphological illumination](methods/illumination.md#i02-morphological-illumination).
The selected denoiser follows illumination before optional [restoration](methods/restoration.md), [contrast](methods/contrast.md) and explicit [sharpening](methods/sharpening.md) before final quantization; protection is exact and preparation executes once.
See [NLM](methods/denoising.md#d01-bounded-nlm) and [TV-L1](methods/denoising.md#d02-floating-point-tv-l1) for numerical, resource, cancellation and record contracts.

`--output-mode bw` activates B01/B02/B03; default selection is Sauvola. Interpretation, G01/G02,
protection and selected photometric methods run through the same pipeline as continuous output.
The terminal boundary quantizes final luminance once to Q16, then classifies every pixel into
logical 0/1 and physical grayscale PNG1. A protection mask preserves photometric values; it does
not exempt pixels from classification. See [typed binarization](methods/binarization.md).

The application validates a typed request; the production host owns execution; the CLI and report
layers own transport and presentation. Buffer lifetime, borrowed views, scheduler failure handling,
codec allocations and exclusive publication have explicit contracts and regression tests.
Cooperative cancellation is carried through the pipeline, with native interrupt handling and a
publication cutoff; see [cancellation](architecture.md#cooperative-cancellation).

Each result is published as a bundle: the image, a `run.json` recording the build, the identified
source bytes, the admitted request, the execution observations, the protection in force and the
verified output, plus a canonical mask and exact supplied PSF asset when selected. The files are committed together, so
no result is published without its record. `docenhance verify DIRECTORY` reads one back against a
complete supported record, closed inventory and decoded artifact properties without executing
anything it finds. Staged bundles use that same validation; publication reconciles this run's
identity and manifest digest, retaining uncertainty and preserving known committed effects. Binary output is read back and compared
before publication, as continuous output already was. Agreement between artifacts and their record
is not authenticity; see [processing bundles](record.md).
See [architecture](architecture.md) for exact limits and the publication trust/durability boundary.

## Reusable components

C++23/CMake target boundaries, a verified offline source lock and isolated native dependency builds;
strict warnings and linters; generated method/argument contracts; deterministic scalar primitives;
checked aligned planes; budget accounting; bounded indexed scheduling; a box-mean primitive;
reference/property tests and manifest-declared engine-independent fuzz harnesses, including
raw binary/continuous PNG, JPEG and TIFF decoding, raw ICC parsing/transforms, independently generated exact-sample PNG checks and a direct-window Sauvola oracle. See [fuzzing](fuzzing.md).

Sauvola and the box-mean primitive use the internal scheduler; the public CLI admits process-only `--threads auto|N` and `--memory-limit MIB`; it does not expose batching or arbitrary recipes. D01 uses first-party binary64 NLM-L1 weights and
accumulation. R01 uses charged binary64 Fourier blocks through the pinned pocketfft adapter.
Native adapter allocation/calibration and platform evidence remain commit-specific obligations.

## Verification is commit-specific

A local build or successful reference suite is not certification of all platforms. A created PR is
not evidence that its CI passed.

Required CI includes Linux x86-64/ARM64, macOS Intel/ARM64 and Windows x86-64 native builds,
real-executable contracts, structural/tooling checks, fuzzing, and independent ASan/UBSan and
TSan suites. Native sanitizer scope follows [effective build and runtime evidence](architecture.md#native-sanitizer-coverage).
The macOS deployment floor is 15.0. Required CI includes actual macOS 15 ARM64/Intel execution
and retains macOS 26 ARM64/Intel forward coverage; declared lanes require exact-commit passing
evidence. Signing/notarization and independent natural-document readability claims are separate
release obligations. The [quality protocol](document-quality.md) refuses missing rights/reviews;
resource measurements describe named machines and inputs, never universal process-RSS limits.

## Not implemented

Other complete methods, multipage processing, perspective rectification,
dewarping, deskewing, automatic method selection, OCR, batching, presets and streaming/tiling
for arbitrarily large pages. No neural inference, GPU requirement, network service, GUI,
database or plugin framework is introduced. Planned work belongs to the [roadmap](roadmap.md) and must earn complete typed admission, implementation and executable tests.
