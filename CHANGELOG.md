# Changelog

Notable changes to this project are documented in this file. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Continuous-tone PNG conversion accepts static grayscale, indexed, RGB and alpha images, retaining 16-bit sample precision unless reduction is explicitly requested. Profile interpretation, linear-light alpha compositing, grayscale luminance, exact EXIF orientation and physical-resolution handling are explicit and reported.
- Continuous-tone output is independently decoded and checked against every intended integer sample and required metadata before publication. Verification failure returns `E_OUTPUT_VERIFY` (exit 5), without publishing an incomplete result.
- Bounded PNG/color-profile fuzz harnesses exercise the same production adapters; isolated campaigns verify instrumentation of the actual libpng, zlib and Little CMS archives.
- Opt-in I01 surface illumination uses eligible linear-luminance quantiles, a log-grid fit solved by multigrid-preconditioned conjugate gradients with a checked true residual, a source-relative or numeric target and capped gain. Cells darker than any admissible gain could correct are treated as dark content and spanned from surrounding paper, so solid fills are not brightened to the gain cap. Defaults correct the fitted background completely up to a doubling of any sample. `--illumination auto` records explicit applicability predicates; default illumination remains off.
- Original-depth 1/8-bit grayscale PNG protection masks exclude samples from measurement and preserve their values entering the photometric stage. Masks use oriented source coordinates and are validated even for no-op processing.
- Independent dense-system, real-PNG, preservation, resource/cancellation and model/mask fuzz tests cover I01. A failed fit returns `E_METHOD_INAPPLICABLE` or `E_NUMERICAL` (exit 4), not a fabricated correction or silent fallback.

### Changed

- **Breaking (CLI):** `process` defaults to `--output-mode preserve`, with no enhancement filter. Binary processing now requires explicit `--output-mode bw`; an absent binarizer in that mode defaults to Sauvola. B02/B03 retain their released stored-grayscale sample semantics and input domain. There is no inference or compatibility alias.
- **Breaking (API/JSON):** Validated processing alternatives distinguish binary methods from continuous output. Continuous success reports `operation: continuous` and typed conversion descriptors, assumptions and warnings, without a fabricated method ID. Continuous results now include a complete typed illumination-stage record; processing errors retain available stage observations. The generated command-contract edition is 7.0.
- Continuous processing uses a 1 GiB charged-buffer ceiling and bounded conversion rows; the binary ceiling remains 128 MiB. Neither ceiling is a process-RSS guarantee. JPEG/TIFF input, presets and recipes remain unsupported.

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
