# Changelog

Notable changes to this project are documented in this file. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

- Preserve double scalar precision through known sRGB/gamma interpretation and alpha composition;
  keep the native color-engine precision boundary explicit and correct the independent I01 oracle.
- Apply static PNG framing to stored-grayscale input, validate EXIF before physical precedence,
  and reject unsupported decoder policies/limits instead of silently substituting them.

- Reject temporary owners at borrowed-view/callable/converter boundaries, use closed processing
  success alternatives and typed publication observation, and report completed NLM resource
  charges from the native reservation owner. Tile traversal avoids uint32 extent wraparound.
- Bound publication file tables and owned staging entries; reject missing writers and native
  path components that escape their relative filename role.
- Consolidate percentile selection into caller-owned scratch, removing an uncharged input-sized
  copy and a duplicated I01 selection implementation.

- Enforce native build configuration identity, private dependency providers and complete selected
  feature audits; retire ambiguous test/toolchain controls without cache migration. Bind Docker
  state to architecture/image/recipes and serialize preparation. Verify archive source bytes
  against locked artifacts and ignore inherited Git overrides; require ready LLVM tool caches.

- Remove superseded PNG-only acquisition, image-only publication, generic bundle inspection/read
  APIs and unused page-selection scaffolding without compatibility aliases. Keep current source
  admission and bundle transactions, narrow reconciliation reads to the root record, and derive
  codec ceilings from one shared source authority.
- Make decimal conversion independent of the embedding process locale with an explicit classic
  locale; document conversion bounds and correct stale capability/verification documentation.

## [0.5.0] - 2026-10-01

- Add opt-in D01 16-bit NLM-L1 luminance denoising with exact protection, bounded native tiles,
  preparation reservations and truthful stage observations; compose I01 before D01 and quantize once.
- Replace unreadable dense gray ICC curves with serialized standard parametric sRGB profiles.
- Make a clean record/response format-3 break; remove obsolete record readers without migration.

### Added

- Bounded 8-bit Huffman baseline/progressive JPEG input for continuous `preserve`/`gray` and opt-in I01, including gray/RGB/YCbCr, bounded ICC/EXIF/JFIF interpretation, exact orientation and oriented PNG protection masks. Signature-based admission hashes and decodes one immutable source snapshot. Strict corruption, scan/marker/resource refusal and native cancellation use charged decoder buffers; output remains the verified PNG bundle. JPEG `bw`, arithmetic/lossless/higher-precision/CMYK/multi-image/gain-map input and JPEG output remain unsupported. See [JPEG](docs/jpeg-processing.md).
- Production JPEG fuzzing with the actual instrumented static archive, independently constructed coefficient fixtures, and relocated-package JPEG processing/verification. A developer-only resource probe measures allocations, RSS and runtime separately.

### Changed

- **Breaking (wire):** responses use schema version 3, adding source/decode observations and the explicit operation/format matrix. New run records use format version 3 with required source and denoising observations; obsolete records are rejected without migration. B02/B03/I01 numerical definitions and method versions are unchanged.
- Shared raster metadata separates container declarations from ICC/orientation/resolution. The bounded EXIF reader is reused without changing PNG color/pHYs precedence or binary stored-sample semantics.

## [0.4.0] - 2026-09-29

### Added

- `process` converts continuous-tone PNG images without an enhancement filter. Static grayscale, indexed, RGB and alpha images are accepted, including Adam7-interlaced files, and 16-bit sample precision is kept unless `--bit-depth 8` asks for reduction. New options select `--output-mode preserve|gray`, `--bit-depth auto|8|16`, `--alpha white|black|reject` and `--profile-policy embedded|srgb`. Profile interpretation (cICP, ICC, sRGB, gAMA/cHRM), linear-light alpha compositing, grayscale luminance, EXIF orientation and physical resolution are handled explicitly, and every assumption, warning and depth reduction is reported in the response. Animated PNG, malformed or unsupported color declarations, and unknown critical chunks are rejected. See [PNG processing](docs/png-processing.md).
- Continuous-tone output is decoded again and compared with every intended sample and required metadata before it is published. A mismatch returns `E_OUTPUT_VERIFY` (exit 5) and publishes nothing.
- `process --illumination surface|auto` applies opt-in I01 surface illumination correction to continuous-tone output; illumination stays off by default, and `methods` and `version --json` now list I01. A background surface is fitted to eligible linear-luminance samples and its true residual is checked. The `--background-strength`, `--background-max-gain`, `--background-target`, `--background-cell`, `--background-quantile` and `--background-smooth` options tune it. By default the fitted background is corrected completely, up to a doubling of any sample. Cells darker than any admissible gain could correct are treated as dark content, so solid fills are not brightened to the gain cap. `auto` may skip an unsuitable image and records the applicability predicates it used. If a fit cannot be completed the command returns `E_METHOD_INAPPLICABLE` or `E_NUMERICAL` (exit 4) instead of a fabricated correction or a silent fallback. See [illumination](docs/illumination.md).
- `--protect-mask PATH` takes a 1-bit or 8-bit grayscale PNG that excludes samples from the illumination measurement and keeps their values unchanged. The mask uses oriented source coordinates and is validated even when illumination is off or has no effect.
- Every published result is a processing bundle: `result.png`, a `run.json` record and, when `--protect-mask` was given, `assets/protect-mask.png`. The record states the build, the identified source bytes, the admitted request, the execution observations, the protection in force and the verified output. The files are committed together with one exclusive rename, so no result is published without its record. Input, output and mask bytes are identified with SHA-256, and the response reports the run identity and the record's digest. See [processing bundles](docs/bundles.md).
- `docenhance verify DIRECTORY [--json]` reads a bundle back, including after it has been moved, and checks that it holds exactly what its record declares. Missing, altered, oversized, undeclared and symbolic-link entries, and malformed records, are refused as an input failure at exit 3, and nothing found is executed. Agreement between a bundle and its record is not authentication of the document or of who produced it.

### Changed

- **Breaking (CLI):** `process` defaults to `--output-mode preserve`, which converts the image without any enhancement filter. Binary output now requires an explicit `--output-mode bw`, and `--binarize` is rejected in the other modes; with `bw`, an absent `--binarize` means Sauvola. Scripts that relied on `--binarize` alone must add `--output-mode bw`. B02 and B03 keep their released sample semantics and their 1/2/4/8-bit grayscale-without-transparency input domain, and no alias infers the old behavior.
- **Breaking (output layout):** A result directory now holds `run.json` (and `assets/protect-mask.png` when a mask was supplied) alongside `result.png`. Consumers that assumed `result.png` was the only entry must accommodate the additional files.
- **Breaking (API/JSON):** Responses distinguish binary from continuous-tone results. A continuous success reports `operation: continuous`, typed `conversion` descriptors, assumptions and warnings and a complete `illumination` stage record, with no method ID; a binary success still reports `method` and `method_version`. Successful `process` responses gain a `record` object. Processing errors retain the stage observations available at the time of failure. The generated command-contract edition is 7.0 and `schema_version` remains 1. Consumers validating against the 0.3.0 schema must update to the current one.
- Binary output is now decoded again and compared with the intended samples and metadata before it is published, as continuous-tone output is. No output sample changes. `verified` in a response is derived from the comparison that ran rather than set once publication returned.
- Continuous-tone processing has a 1 GiB charged-buffer ceiling and converts bounded rows; the binary ceiling remains 128 MiB. Neither is a process-RSS guarantee. JPEG and TIFF input, presets and recipes remain unsupported.
- **Breaking (build):** Every shared CMake preset now declares the compiler it is validated with. `dev`, `sanitize`, `tsan` and the fuzz presets require the LLVM clang release pinned in `deps/tools.json`; `release` declares the platform's own toolchain and still refuses a family outside GCC, Clang/AppleClang and MSVC. Configuring with any other compiler fails instead of silently producing a build with weaker diagnostics than the required CI jobs run. Set `CC` and `CXX` for a fresh build directory as [build and developer workflows](docs/build.md) describes.
- `DE_BUILD_JOBS` can now be set in the environment. One count bounds the external dependency builds, the architecture check and the number of tests run at once; an explicit `-DDE_BUILD_JOBS` or preset value still takes precedence.

### Internal

- A fuzz harness covers the run-record reader, the one parser that reads a document this program did not write, and asserts that whatever it accepts is within the bounds the reader claims to enforce. Further bounded harnesses cover PNG metadata and pixels, ICC profiles, protection-mask PNGs and illumination, and isolated fuzz campaigns instrument the actual libpng, zlib and Little CMS archives. Regression reproducers are retained.
- Independent dense-system, real-PNG, preservation, resource, cancellation and model/mask tests cover I01.
- The architecture manifest gained `de_bundle` and `de_color` layers and the checker builds each source file and public header in parallel while reporting in serial order.
- UTF-8 paths are handed to `std::filesystem::path` as their own bytes wherever a path stores `char`, instead of being copied through `char8_t` first. The admitted spelling is unchanged on every platform; the removed conversion tripped UndefinedBehaviorSanitizer's implicit-conversion check inside libc++ on non-ASCII filenames.
- CI gives each native and sanitized job the runner's cores, the suite's tests can run as concurrent processes, and the nightly fuzz campaign is budgeted for its fifteen targets. Actions were updated to newer pinned releases.

## [0.3.0] - 2026-09-24

### Added

- `process --binarize sauvola` applies B02 local thresholding to a single 1/2/4/8-bit grayscale PNG without transparency and writes an 8-bit black-and-white `result.png`; its odd window, `k`, and normalized `R` options are validated, with reflected borders and bounded scratch storage. Other formats and methods beyond B02/B03 remain unsupported.
- Processing can be cancelled cooperatively with POSIX SIGINT/SIGTERM or Windows CTRL_C/CTRL_BREAK. A confirmed cancellation returns `E_CANCELLED` (exit 130) and `not_started` or `not_published`; a request after the final precommit check cannot undo an authorized publication. Blocking operations may delay cancellation, and forced termination has no cooperative cleanup guarantee.

### Changed

- **Breaking (CLI):** `process` now admits only options for its selected `fixed` or `sauvola` method; explicitly empty numeric values are errors rather than defaults. Existing B03 fixed-threshold sample and equality rules are unchanged.
- **Breaking (C++ API):** Application dispatch and CLI embedding now require an explicit processing port, and the port receives a validated method request plus cancellation control. Integrators using the previous dispatch or CLI signatures must update their adapters.
- **Breaking (JSON):** Successful processing responses require `method_version`, and `methods ID` returns only the selected implemented method. Consumers validating responses against the 0.2.0 schema must update to the current schema; `schema_version` remains 1.
- **Breaking (paths):** Command text and admitted paths must be well-formed UTF-8; invalid bytes and embedded path NULs are rejected without normalization or replacement. Integrators passing arbitrary filesystem bytes must provide valid UTF-8 spelling.

### Fixed

- **Breaking (publication):** Output commit now uses native atomic no-replace operations, so a destination that appears during processing is refused rather than replaced; cleanup touches only owned staging. Ambiguous commit or cleanup, and an unreported processor exception after execution begins, return `E_PUBLICATION_UNKNOWN` (exit 7, `unknown`). Inspect output before retrying instead of treating these results as proof that nothing was published.
- Response delivery now writes the rendered bytes once and checks the selected stream after flushing; a write or flush failure returns process exit 5 without rerunning processing. A complete JSON response may still precede that exit, so consumers must consider both the response and process status.
- PNG decoding now limits encoded input size, charges codec allocations to the page budget, preserves stored grayscale sample values when expanding low bit depths, and rejects malformed input without publishing an incomplete image.
- Reported output paths retain literal backslashes on POSIX and Unicode spelling on Windows; Windows command-line arguments are converted to UTF-8 before admission.
- Direct C++ processing now rejects invalid or overlapping plane views, retains buffer accounting when an allocation outlives its budget handle, and contains worker or thread-launch failures after joining started workers.

### Internal

- Source architecture checks now enforce declared target links, headers, allocation and exception boundaries; lint exceptions are tied to specific code, and unintended C++ module scanning is disabled.
- Native replays and libFuzzer/AFL campaigns share one target manifest, with bounded parallelism, retained corpus provenance and evidence. Required CI independently runs ASan/UBSan and TSan and covers Linux x86-64/ARM64, macOS ARM64/Intel, and Windows x86-64; the nightly budget accommodates all ten targets.

## [0.2.0] - 2026-09-22

### Added

- `process` now performs B03 fixed-threshold binarization for grayscale PNG input without alpha (1, 2, 4, or 8 bits), producing an 8-bit grayscale `result.png` in a newly published output directory.
- `methods` reports B03 and `version --json` reports PNG as supported capabilities.

### Changed

- **Breaking:** The command-line contract is reduced to `process`, `methods`, and `version`; `plan`, `inspect`, `presets`, and unsupported processing options are no longer accepted. Integrators must invoke the explicit B03/PNG operation described above.
- **Breaking:** JSON responses no longer contain lifecycle metadata. Consumers must use [command-response.schema.json](schemas/command-response.schema.json) and stop reading the removed fields.
- Source releases now derive their GitHub release notes from the matching dated changelog section and verify the published source archive and checksum against the tagged revision.

### Internal

- The shared box-mean primitive now rejects overlapping storage and initializes large reflected windows by period, preserving deterministic output while avoiding radius-proportional setup work.
- The quality workflow includes Linux x86-64/ARM64, macOS ARM64/Intel, and Windows x86-64; source releases retain source-only provenance rather than publishing binaries.

## [0.1.0] - 2026-09-21

- First release.
