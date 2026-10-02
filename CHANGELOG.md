# Changelog

Notable changes to this project are documented in this file. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- Contract generation checks method-option attribution in both directions and derives D01 identities, format/mode reporting and the response envelope version from reviewed contracts. Protection help now identifies both I01 and D01. Denoising schemas share definitions and reject settings without a selected method; wire/record versions and numerical method definitions are unchanged.

- **Breaking (C++/authoring):** capability entries use `contract::InputSupport` instead of `app::InputSupport`; update native consumers. The unused CLI `contract_version` field is removed, and obsolete or unknown authoring fields are rejected without aliases or migration.

### Internal

- Real executable checks compare complete help descriptors and admitted default records with the reviewed contract, and reject empty values for every declared value option. Documentation distinguishes verification refusal before commit from integrity failure after known publication.

## [0.5.0] - 2026-10-02

### Added

- Bounded 8-bit Huffman baseline/progressive JPEG input supports gray/RGB/YCbCr continuous `preserve`/`gray` processing, ICC/EXIF/JFIF interpretation, exact orientation and oriented PNG protection masks. Signature admission identifies and decodes one immutable source snapshot. Corruption, decoder warnings and exceeded scan, marker or resource limits are refused; decoder buffers are charged and cancellation is supported. Output remains a verified PNG bundle. JPEG `bw`, arithmetic/lossless/higher-precision/CMYK/multi-image/gain-map input and JPEG output remain unsupported. See [JPEG](docs/jpeg-processing.md).
- Opt-in D01 16-bit NLM-L1 luminance denoising provides exact protection, bounded native tiles and preparation reservations, with completed stage and resource observations. Use `--denoise nlm` and the reviewed tuning options. I01 runs before D01, with one final quantization and no native replay during verification. Protection preserves destinations while neighbors remain comparison context. This is luminance denoising, not lost-detail recovery; charged buffers and native reservations do not bound process RSS or wall-clock latency. See [denoising](docs/denoising.md).
- A generated [run-record schema](schemas/run-record.schema.json) accompanies the response schema. Complete typed validation also checks relationships beyond JSON Schema.

### Changed

- **Breaking (wire and records):** responses use schema version 3 with source/decode and denoising observations and an explicit operation/format matrix; run records use format version 3. Update response consumers and processing/verification adapters to the current contracts. Obsolete records are rejected without migration or compatibility readers. B02/B03/I01 numerical definitions and method versions remain unchanged.
- **Breaking (bundle verification):** `verify` and staged publication validate the complete current record, closed inventory, decoded image/mask properties and canonical profiles, rather than digest agreement alone. Malformed claims and contradictory artifacts are refused. Ambiguous publication reconciles this run's identity and record digest; known commit success remains `completed` even if later integrity inspection fails with `E_OUTPUT_VERIFY` (exit 5). `E_PUBLICATION_UNKNOWN` (exit 7, `unknown`) remains unsafe to retry blindly. Verification does not authenticate a document or recreate its historical processing. See [bundles](docs/bundles.md).
- **Breaking (input admission):** stored-grayscale PNG input now receives the shared static-container checks, rejecting animation, trailing bytes and invalid framing while retaining B02/B03 stored-sample meaning. Selected EXIF resolution is validated before valid pHYs/JFIF precedence can hide malformed fields; orientation and valid PNG precedence are unchanged. Unsupported programmatic decoder policies and limits are rejected rather than substituted.
- **Breaking (C++ ownership):** borrowed buffer/plane views, scheduler callables and color/denoising converters reject temporary owners. Callers must retain live backing owners throughout use. Processing success and publication observations use closed typed alternatives; moved-from planes and surface models are empty/inactive.
- **Breaking (build):** native configuration binds compiler, SDK, architecture, instrumentation, features and recipes to each build tree/private prefix. Use fresh trees when those inputs change; unbound old caches are refused without migration. Dependency providers and selected features are audited, unused probe packages are not built, and ambiguous test/toolchain controls are removed. Analysis requires the reviewed LLVM major and ready tool caches; see [build](docs/build.md).
- **Breaking (developer gates):** `python tools/check_all.py` and commit hooks on macOS/Windows require Docker for the native Linux release/test/package workflow. Docker state is serialized and isolated by daemon architecture, image and recipes. Missing Docker fails the gate; the native Linux architecture exercised does not replace the other required CI platforms. See [quality](docs/quality.md).
- **Breaking (packaging tools):** source archives require a committed Git tree and exclude dirty/untracked workspace changes; commit intended release files before packaging. Relocated package inspection requires `--build` pointing to the independently tested application build. Builds from extracted source archives remain supported.

### Fixed

- Grayscale output ICC profiles serialize standard parametric sRGB curves instead of dense curves that the pinned reader could not reopen. Produced grayscale images can be interpreted in subsequent processing.
- Known sRGB/gamma interpretation and linear-light alpha composition retain double scalar precision, correcting one-level errors in 16-bit conversion. The native ICC/chromaticity engine retains its documented float32 internal precision boundary.
- Closed response pipes return delivery failure (process exit 5) through normal process termination. A committed bundle remains published; delivery never retries processing. Consumers must inspect both the response and process status. Verification cancellation is admitted with `not_started`; bounded codec/manifest checkpoints and checked flush/close preserve real-error precedence. Blocking native calls, forced termination and crash durability remain outside cooperative cancellation guarantees. See [cancellation](docs/cancellation.md).
- Publication validates bounded file tables and staging entries, refuses missing writers and escaping native path components, and cleans only owned entries. Percentile selection uses caller-owned scratch instead of an uncharged input-sized copy; I01 uses the same nearest-rank implementation. Decimal conversion uses the classic locale independently of the embedding process locale.

### Removed

- Superseded PNG-only acquisition, image-only publication and generic bundle inspection/read interfaces, plus unused page-selection scaffolding, are removed without aliases. Integrators must use current source admission and complete bundle transactions.

### Internal

- Required verification rejects skipped, disabled, missing and zero-work tests; it reconciles actual compilation, executable discovery and fresh execution evidence. Fuzz completion independently checks duration, engine and binary/manifest identity. Independent numerical/codec fixtures and relocated processing exercise the advertised methods; bounded campaigns are not exhaustive safety proofs.
- Relocated package inspection compares binary, contracts, schemas and original upstream license bytes against independent build/source evidence and checks actual OS imports. Native archives omit host extended-attribute entries, and license regeneration removes stale owned notices. The declared-source SPDX inventory does not certify binary composition or legal clearance; native reproducible compilation is not claimed.
- Dependency source inventories are checked against immutable locked archive bytes, ignoring inherited Git overrides. Source archive manifests and payloads use the same committed blob snapshot. JPEG/OpenCV fuzzing instruments the selected native closure; developer resource probes report allocations, RSS and runtime separately. The independent I01 reference no longer shares float32 ingress rounding with the implementation.

## [0.4.0] - 2026-09-29

### Added

- `process` converts continuous-tone PNG images without an enhancement filter. Static grayscale, indexed, RGB and alpha images, including Adam7, are accepted; 16-bit samples are preserved unless `--bit-depth 8` requests reduction. Options include `--output-mode preserve|gray`, `--bit-depth auto|8|16`, `--alpha white|black|reject` and `--profile-policy embedded|srgb`. Processing handles cICP/ICC/sRGB/gAMA/cHRM, linear-light alpha composition, grayscale luminance, EXIF orientation and physical resolution, reporting assumptions, warnings and depth reductions. Animated PNG, malformed or unsupported color declarations and unknown critical chunks are rejected. See [PNG processing](docs/png-processing.md).
- Continuous-tone output is decoded and compared with every intended sample and required metadata before publication. A mismatch returns `E_OUTPUT_VERIFY` (exit 5) and publishes nothing.
- `process --illumination surface|auto` enables I01 surface illumination correction for continuous-tone output; illumination is off by default. `methods` and `version --json` list I01. The fitted background surface uses eligible linear-luminance samples and checks its true residual. Tune it with `--background-strength`, `--background-max-gain`, `--background-target`, `--background-cell`, `--background-quantile` and `--background-smooth`. See [illumination](docs/illumination.md).
- I01 defaults to full fitted-background correction, capped at doubling any sample; cells too dark for an admissible gain are treated as dark content, avoiding brightened solid fills. `auto` may skip unsuitable images and records its applicability predicates. Failed fits return `E_METHOD_INAPPLICABLE` or `E_NUMERICAL` (exit 4), not fabricated corrections or silent fallbacks.
- `--protect-mask PATH` accepts a 1-bit or 8-bit grayscale PNG that excludes samples from illumination measurement and preserves their values. Masks use oriented source coordinates and are validated even when illumination is off or has no effect.
- Published processing bundles contain `result.png`, `run.json` and, when `--protect-mask` is used, `assets/protect-mask.png`. One exclusive rename commits the result and record together. The record identifies the build, source bytes, admitted request, execution observations, protection and verified output; SHA-256 identifies input, output and mask bytes. Responses include the run identity and record digest. See [processing bundles](docs/bundles.md).
- `docenhance verify DIRECTORY [--json]` checks bundles, including relocated ones, against their records. Missing, altered, oversized, undeclared or symbolic-link entries and malformed records are refused at exit 3; nothing found is executed. Agreement with a record does not authenticate the document or its producer.

### Changed

- **Breaking (CLI):** `process` defaults to `--output-mode preserve`, with no enhancement filter. Binary output requires `--output-mode bw`; `--binarize` is rejected in other modes, and omission in `bw` selects Sauvola. Scripts using `--binarize` alone must add `--output-mode bw`; no alias infers the old behaviour. B02/B03 retain their sample semantics and 1/2/4/8-bit grayscale-without-transparency input domain.
- **Breaking (output layout):** result directories contain `run.json` and, when supplied, `assets/protect-mask.png` alongside `result.png`. Consumers must no longer assume that `result.png` is the only entry.
- **Breaking (API/JSON):** continuous successes report `operation: continuous`, typed `conversion` descriptors, assumptions, warnings and a complete `illumination` stage record, without a method ID. Binary successes retain `method` and `method_version`. Successful `process` responses add `record`; errors retain available stage observations. Consumers must update from the 0.3.0 schema. Command-contract edition is 7.0; `schema_version` remains 1.
- Binary output, like continuous-tone output, is decoded and compared with intended samples and metadata before publication. Sample values are unchanged; `verified` reflects the comparison performed, not merely successful publication.
- Continuous-tone processing uses bounded rows and a 1 GiB charged-buffer ceiling; binary processing retains its 128 MiB ceiling. Neither guarantees process RSS. This release does not support JPEG or TIFF input, presets or recipes.
- **Breaking (build):** shared CMake presets require their validated compiler. `dev`, `sanitize`, `tsan` and fuzz presets use the LLVM clang release in `deps/tools.json`; `release` uses the platform toolchain within GCC, Clang/AppleClang or MSVC. Other compilers are refused. Set `CC` and `CXX` for a fresh build directory as [build and developer workflows](docs/build.md) describes.
- The `DE_BUILD_JOBS` environment variable bounds dependency builds, architecture checks and concurrent tests. Explicit `-DDE_BUILD_JOBS` or preset values take precedence.

### Internal

- Bounded fuzz harnesses cover run records, PNG metadata and pixels, ICC profiles, protection masks and illumination. Run-record fuzzing checks that accepted input stays within declared bounds; isolated campaigns instrument the actual libpng, zlib and Little CMS archives. Regression reproducers are retained.
- Independent dense-system, real-PNG, preservation, resource, cancellation and model/mask tests cover I01.
- The architecture manifest gained `de_bundle` and `de_color` layers and the checker builds each source file and public header in parallel while reporting in serial order.
- On paths using `char` storage, UTF-8 bytes pass directly to `std::filesystem::path` instead of through `char8_t`. Admitted spelling is unchanged; the removed conversion triggered libc++ implicit-conversion checks under UndefinedBehaviorSanitizer on non-ASCII filenames.
- Native and sanitized CI jobs use the runner's cores and concurrent test processes; nightly fuzzing budgets for fifteen targets.

## [0.3.0] - 2026-09-24

### Added

- `process --binarize sauvola` applies B02 local thresholding to a single 1/2/4/8-bit grayscale PNG without transparency and writes an 8-bit black-and-white `result.png`. Odd-window, `k` and normalized `R` options are validated; processing uses reflected borders and bounded scratch storage. Other formats and methods beyond B02/B03 remain unsupported.
- Processing supports cooperative cancellation through POSIX SIGINT/SIGTERM and Windows CTRL_C/CTRL_BREAK. Confirmed cancellation returns `E_CANCELLED` (exit 130), with `not_started` or `not_published`. Cancellation after the final precommit check cannot undo authorized publication; blocking operations may delay cancellation, and forced termination has no cooperative cleanup guarantee.

### Changed

- **Breaking (CLI):** `process` now admits only options for its selected `fixed` or `sauvola` method; explicitly empty numeric values are errors rather than defaults. Existing B03 fixed-threshold sample and equality rules are unchanged.
- **Breaking (C++ API):** Application dispatch and CLI embedding now require an explicit processing port, and the port receives a validated method request plus cancellation control. Integrators using the previous dispatch or CLI signatures must update their adapters.
- **Breaking (JSON):** Successful processing responses require `method_version`, and `methods ID` returns only the selected implemented method. Consumers validating responses against the 0.2.0 schema must update to the current schema; `schema_version` remains 1.
- **Breaking (paths):** Command text and admitted paths must be well-formed UTF-8; invalid bytes and embedded path NULs are rejected without normalization or replacement. Integrators passing arbitrary filesystem bytes must provide valid UTF-8 spelling.

### Fixed

- **Breaking (publication):** native atomic no-replace commits refuse destinations that appear during processing; cleanup touches only owned staging. Ambiguous commit/cleanup or an unreported processor exception after execution begins returns `E_PUBLICATION_UNKNOWN` (exit 7, `unknown`). Inspect output before retrying: these results do not prove that nothing was published.
- Response delivery writes rendered bytes once and checks the stream after flushing. Write or flush failure returns process exit 5 without repeating processing. A complete JSON response may precede that exit; consumers must check both response and process status.
- PNG decoding bounds encoded input, charges codec allocations to the page budget, preserves stored grayscale values when expanding low bit depths, and rejects malformed input without publishing an incomplete image.
- Reported output paths retain literal backslashes on POSIX and Unicode spelling on Windows; Windows command-line arguments are converted to UTF-8 before admission.
- Direct C++ processing now rejects invalid or overlapping plane views, retains buffer accounting when an allocation outlives its budget handle, and contains worker or thread-launch failures after joining started workers.

### Internal

- Source architecture checks now enforce declared target links, headers, allocation and exception boundaries; lint exceptions are tied to specific code, and unintended C++ module scanning is disabled.
- Native replays and libFuzzer/AFL campaigns share a target manifest, bounded parallelism, retained corpus provenance and evidence. Required CI independently runs ASan/UBSan and TSan across Linux x86-64/ARM64, macOS ARM64/Intel and Windows x86-64; nightly fuzzing budgets for ten targets.

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
