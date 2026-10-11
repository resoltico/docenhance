# Changelog

Notable changes to this project are documented in this file. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- G02 `--rotate 0|90|180|270` applies exact clockwise quarter-turns after continuous G01 metadata orientation, with identical protection-mask permutations and physical X/Y density swapping for 90/270 degrees. Stored-gray binary processing admits explicit turns before thresholding while retaining its ignored-metadata-orientation policy. Existing continuous metadata orientation is documented separately from explicit rotation. Masks remain supplied/stored in metadata-oriented frame B; PSFs and pixel windows use the post-geometry frame. Perspective, deskew and dewarping remain unsupported. See [geometry](docs/geometry.md).

- Explicit R01 known-PSF Fourier restoration (`--deblur wiener`) follows denoising before contrast. Gaussian, clockwise bilinear motion and raw grayscale 8/16-bit PNG kernels use reflected padding, a centered FFT origin and padded-mean preservation. Protected destinations remain exact. Resource limits refuse the requested operation, and native transforms can delay cancellation. `W_RESTORATION_INFERENCE` states that the supplied PSF is an unverified model; restoration is not evidence of recovered source detail. See [restoration](docs/restoration.md).

- B01 global Otsu (`--output-mode bw --binarize otsu`) uses a 4,096-bin stored-gray histogram, deterministic smallest-threshold ties, recorded single-bin fallback and exact black/white polarity. It retains the existing 1/2/4/8-bit grayscale PNG without transparency domain, B02/B03 semantics and Sauvola default; continuous inputs and protection masks are not admitted. See [binarization](docs/binarization.md).

- Explicit C03 floating-point CLAHE (`--contrast clahe`) completes the contrast alternatives after illumination and denoising. It uses 1,024 eligible-sample histogram bins, mass-conserving clipping/redistribution, exact sparse/flat tile identities and interpolation at actual contextual centers. The default grid is 8x8 with at least 16 pixels per tile dimension; the clip multiplier is 2. Protected destinations remain exact, and histogram discretization does not reduce 16-bit output to 8-bit levels. See [contrast](docs/contrast.md).

- Explicit S01 thresholded unsharp masking (`--sharpen unsharp`) follows contrast with float64 Gaussian REFLECT_101 borders and a soft threshold. It preserves protected destinations, reports eligible pre-clamp excursions and clipping, and emits `W_SHARPENING` for positive amount. Output is not evidence of additional detail. See [sharpening](docs/sharpening.md).

### Changed

- Human CLI help now includes reviewed domains/defaults, value refusals identify supplied options, and errors expose truthful publication state with recovery guidance. Warnings explain representation changes, TV-L1 stopping and sharpening excursions. Current help describes bounded TIFF/BigTIFF and the implemented continuous pipeline.
- A literal `--json` string value no longer enables JSON after successful parsing: both `--out-dir --json` and `--out-dir=--json` produce human text unless a separate JSON flag is parsed. Callers relying on the old presentation side effect must add the flag; syntax-failure JSON fallback remains. Path identities and processing admission are unchanged.

- **Breaking (machine contracts):** command responses and processing records advance from version 7 to 12, adding required G02 rotation requests and conversion/binary rotation observations, closed R01 PSF/regularizer/blend requests, normalized kernel identity/phase/padding/resource observations and retained supplied-PSF path spelling, inference warnings, closed C03 grid/clip and S01 unsharp parameters, required `identity_tiles` observations (zero for other contrast methods), sharpening excursion/clipping/warning observations and required method-specific B01 threshold/fallback observations. Update schema consumers and geometry/contrast/binarization visitors; obsolete records are refused without a compatibility reader or migration.

- **Breaking (C++ models):** `methods::ContrastModel` owns its charged immutable CLAHE maps and is move-only. Move prepared models into their lifetime owner and keep them live through output verification.

- **Breaking (build outputs):** the executable moves to `<owning-build>/bin/docenhance` (`.exe` on Windows), and native archives/checksums/CPack staging move to `<owning-build>/packages`. The first-party CMake state and compilation database remain in `app`. Use fresh build trees and private prefixes; retained outputs are not migrated or copied to compatibility locations. The standard developer-tool installer includes pinned Ruff for real source-exclusion verification in native suites.

- **Breaking (package verification):** invoke `python tools/package_smoke.py --build out/release`, or the corresponding owning superbuild. Generated CPack metadata selects the current archive, so retained older archives and source distributions cannot redirect verification. Positional archive arguments and app-child build paths are rejected. Source archives continue to default to `dist`.

- **Breaking (build admission):** before compiler/dependency setup, checkout-internal build directories must physically reside beneath root `out`; source-root and other source subdirectories are rejected. Truly external builds remain supported. Root artifact exclusions retain checks and committed-source packaging for legitimate nested `out`, `.cache` and `dist` sources.

- **Breaking (lint exception approvals):** registry entries now require preserved-text full SHA256 scope bindings and explicit occurrence counts. Compiler/formatter regions bind through restoration, and effective configuration exclusions are centrally registered as exact rules with reviewed scopes. Old approvals, hidden suppression routes and malformed or duplicated registry records are refused without a fallback. Contributors must review the affected code and rationale before recording a changed binding; a hash does not establish review quality.

### Fixed

- Corrected the sharpening independent fuzz oracle at the piecewise sRGB transfer boundary, comparing all three linear RGB channels without relaxing numerical tolerances. Kept both original AFL++ reproducers as regression inputs.

- Verification supervisors now clean their owned POSIX process groups after timeouts and termination, including descendants that outlive their direct parent. Linux gates reconcile their uniquely labelled daemon container independently of the Docker CLI and retain the writer claim when creation or cleanup remains unconfirmed. Windows cleanup covers the direct child only; unconfirmed cleanup fails verification. Committed-source packaging avoids a Git pipe deadlock on large blob inventories.

- Zero-width/height PNG input now returns `E_INPUT` (exit 3) before resource arithmetic in continuous and binary modes; genuinely excessive positive dimensions retain `E_RESOURCE` (exit 4). No resource ceiling or numerical default changes.

### Internal

- Bound the libjpeg-turbo OSV-2026-1068 review to exact advisory evidence, locked source and audited omission of the TurboJPEG compression API. Added source-bound OSV observation reports, bounded crash diagnostics, validated failure-artifact archives and protected nightly queuing.

- Linux gate caches separate verified locked sources from native build recipes, avoiding repeated source copies when compiler/build configuration changes. Native and fuzz execution retain bounded waits and independent interruption/permission failure controls. Successful fuzz runs discard staged/generated inputs after process joins, retaining seed provenance, logs and statistics; failed engine runs preserve investigation inputs.

- Fuzz regression oracles distinguish JSON flag/value roles and reject accidental zero-dimension decoding or processing after refused admission. AFL tooling rejects startup seed failures and inherited engine overrides, retains engine failure diagnostics and findings when statistics are absent, and failure audits reject executable identity changes.

- Quality-gate and nightly CI reuse verified locked dependency sources through exact OS-specific source caches. Acquisition still validates every restored source before use, and cache misses are saved only after successful acquisition. Application/native build configurations, private dependency prefixes, project binaries and test evidence remain fresh; the existing verified Intel LLVM tool cache remains separate. Native corpus replay compiles and lints its shared driver once per configuration instead of 23 times, retaining the same options, hardening, sanitizer instrumentation and all 23 replay tests. Tooling workers clear inherited Git repository overrides before discovery, keeping temporary fixture writes out of the caller's checkout under hooks while preserving parent state and deliberate fixture overrides. Complete platform, sanitizer, fuzz and package coverage remains required; hosted end-to-end savings are unmeasured. See [the measured CI baseline and design](docs/ci-performance.md).

- Native R01 resource checks observe actual CPU FFT allocations and inject allocation failures. The private OpenCV recipe establishes ownership before initialization in four DFT factories and uses matching-signature forwarding for all six typed kernels, containing partial-initialization leaks and undefined indirect calls while retaining the locked original sources and notices. The dependency audit binds the corrected copy to actual compilation.

- Verification admits each test process's response schema once while validating every actual response, reuses canonical layer roots within each native compiler trace while resolving every observed header freshly, and skips duplicate Otsu reference plateaus without changing their earliest-threshold semantics. Exhaustive oracle equivalence, path ownership controls, all fixtures and required fuzz exposures remain checked. See [quality](docs/quality.md).

- Verification compiles independent reference sources concurrently and runs tooling modules in fresh bounded processes, reconciling every case and rejecting skipped or expected-failure work. CI starts independent execution roles together, uses observed build cores and selects four fuzz workers on suitable hosted runners. Full fixtures, per-target exposure times, failure propagation and final coverage gates remain required. Actual CMake source/target admission requires effective compiler and lint protection; body size and complexity limits cannot be suppressed. Fixed foreign callback ABIs use a separate parameter-only check. Production files cannot gain a larger limit by being named `test_*`. See [quality](docs/quality.md).

## [0.7.0] - 2026-10-07

### Added

- Bounded single-page TIFF/BigTIFF input produces verified preserve/gray PNG bundles through the shared color, alpha, orientation and enhancement pipeline. It admits unsigned 1-bit gray, 8/16-bit gray/RGB, palettes, explicit alpha, strips/tiles and separate planes with the reviewed compression modes. Multipage input, unsupported coding and oversized decode units are refused without downsampling or precision fallback. Encoded input is limited to 128 MiB and 40 million pixels; binary output remains grayscale PNG only. See [TIFF admission](docs/tiff-processing.md).

- Explicit I02 morphological illumination (`--illumination morph`) uses float64 grayscale closing followed by Gaussian smoothing, with REFLECT_101 borders, protected-sample analysis fill and exact output protection. Radius is `auto` or 1..256; automatic illumination remains I01 only. Field, applicability and resource diagnostics are reported. See [I02](docs/morphological-illumination.md).

- Explicit D02 TV-L1 denoising (`--denoise tvl1`) follows illumination, using full-field float64 primal-dual updates, exact gradient/adjoint boundaries and perceptual blending. All entering samples remain solver context; protection bypasses output changes exactly. Resource refusal never substitutes a tiled or lower-precision solver. Iteration exhaustion returns a usable iterate with `W_TV_ITERATION_LIMIT`, not a tolerance or convergence claim. See [TV-L1](docs/tvl1-denoising.md).

- Explicit C01 percentile levels (`--contrast levels`) and C02 gamma (`--contrast gamma`) follow illumination and denoising before final quantization. Levels measures every eligible perceptual sample using nearest-rank quantiles and reports strict tail clipping; gamma applies f^G with exact endpoints. Both share perceptual blending and color transport, preserve protected samples exactly and report validated flat/identity behavior. Tail clipping can discard weak information. See [contrast](docs/contrast.md).

- The new methods retain immutable models/results for output verification, bounded cancellation checkpoints and preparation-charge diagnostics. The shared 1 GiB charged-buffer budget is not a process-RSS or wall-clock limit; output agreement does not certify document authenticity.

### Changed

- **Breaking (machine contracts):** command responses and processing records advance from version 3 to 7, with closed TIFF source observations and method-specific I01/I02 illumination, D01/D02 denoising and C01/C02 contrast requests and diagnostics. Update schema consumers and C++ visitors for `image::TiffSource`, `image::TiffDeclarations` and the enhancement parameter/model/report alternatives. Obsolete records are refused without a compatibility reader or migration.

- **Breaking (native build):** TIFF is now a required production dependency, with hash-bound private adaptations for charged JPEG allocation and complete Deflate/checksum decoding. Recreate build trees and private prefixes for the changed recipe; the private prefix is not a general-purpose TIFF SDK. TIFF joins JPEG, PNG, zlib and Little CMS in the instrumented codec fuzz closure.

- **Breaking (C++ values):** decoded/output sample depth and orientation use `image::SampleDepth` and `image::Orientation`. Construct wire values through their validating factories and use explicit bit/code accessors. `ErrorCode::unavailable` is replaced by `not_implemented`; the unused exit-6 enumerator is removed.

- **Breaking (C++ interfaces):** publication, immutable bundle snapshots and PNG artifact observations have separate headers. Replace `io/bundle.hpp` includes with the relevant `io/publication.hpp`, `io/bundle_snapshot.hpp` or `io/png_artifact.hpp`; filename helpers are in `io/paths.hpp`, native inventory/snapshot bounds in `io/artifact_limits.hpp` and shared artifact byte bounds in `core/limits.hpp`. UTF-8 admission moves to `core/utf8.hpp`; portable artifact paths share `core/bundle_path.hpp`. Use `core::valid_utf8`, `core::valid_path` and `core::bundle_max_file_bytes` at their new owners. Staged-file identity belongs to publication, while `io/digest.hpp` handles in-memory content. Generated method metadata is `methods/reviewed_methods.hpp`; obsolete header names are removed.

### Fixed

- **Breaking (bundle staging):** artifact names now share the reader's portable domain: at most 128 UTF-8 bytes, `/`-separated, with no empty, `.` or `..` components, NUL, backslash or colon. Direct C++ publication callers must use admitted names.

- **Breaking (C++ execution):** reused `host::Processor` instances obtain a run context once per execution instead of retaining one context across calls. Update constructor callers to supply a context function; the default source generates an identity and timestamp for each execution.

- Illumination completion requires one contiguous output traversal; duplicate or skipped blocks fail before adding observations. Help derives implemented methods and input modes from the application's executable capabilities.

- **Breaking (JPEG refusal codes):** unsupported continuous-input JPEG coding, precision, sampling, color interpretation and recognized multi-image/HDR extensions return `E_INPUT`, exit 3, before staging. Scripts must update checks that expected exit 4. JPEG on the binary PNG-only branch still uses `E_NOT_IMPLEMENTED`, exit 4; that code requires `not_started` in typed errors and the response schema.

- Run-record preflight enforces the 16-level container-depth ceiling before consuming later malformed tokens or building the DOM, bounding duplicate-key bookkeeping. D01 observations exceeding the continuous charged-buffer ceiling are rejected.

- Shared percentile sorting now has bounded cancellation checkpoints without changing nearest-rank quantiles.

- A stop request arriving after all scheduled work completes no longer turns a multi-worker result into cancellation. Skipped work, genuine-error precedence, joins and the publication cutoff retain their contracts.

- Generated source-license inventories identify their tool creator and actual generation time, and give distinct document contents distinct SPDX namespaces. Regenerate native package metadata before inspection; obsolete build-info metadata is rejected. SPDX metadata remains CC0-1.0, independently of software licenses.

### Internal

- Numerical reference checks reject non-finite output and exercise poisoned padding and unwritten destinations. Executable references cover the new methods, their composition, resource refusal and deterministic cancellation; the full composition CLI test runs without competing test processes while retaining its 90-second deadline and assertions. Twenty manifest-declared fuzz targets include TIFF, morphology, TV-L1 and contrast, with retained malformed-input regressions.

- Advisory exclusions remain bound to exact source, feature policy and every advisory field except a validated modification timestamp; timestamp-only OSV updates no longer invalidate an otherwise unchanged review. Vulnerability matches remain visible.

- Architecture checks use one restriction baseline with explicit layer permissions, enforce thread ownership and private-header boundaries through the compiler, validate final CMake links and check the CLI fuzz client's actual public closure before campaign launch. AST ownership follows exact compiler-observed files; fatal parsing diagnostics cannot masquerade as zero-match success. Obsolete per-layer restriction declarations are rejected, and lint suppressions must name exact rules rather than wildcard patterns.

## [0.6.0] - 2026-10-04

### Changed

- **Breaking (licensing):** first-party work in v0.6.0 uses MPL-2.0. Earlier MIT releases and public commits retain their grants; upstream licenses remain unchanged. Binary distributors must preserve notices, make corresponding covered source available and tell recipients where to obtain it; see [licensing](docs/licensing.md).

- **Breaking (native build):** compiler hardening is mandatory for project code and compiled dependencies; recreate build trees and private prefixes. The private zlib build excludes unused gzip-file APIs and is not a general-purpose zlib SDK. Package verification requires native binary inspection tools and checks protection markers separately from actual compiler-command propagation. PNG bootstrap, resource refusal and cancellation remain contained with native control-flow guards enabled.

- Source publication requires the latest direct full `Quality gates` run and complete attempt for the exact tagged commit. Wait for successful main-push or manual CI before tagging; PR merge-checkout results, source checks and attestations are insufficient. Pending, failed, skipped, missing or changed evidence refuses publication; later refusal can leave an unpublished draft for reconciliation.

- **Breaking (verification tooling):** `tools/run_native_suite.py --jobs` accepts only 1 or 2 concurrent test processes; larger values are refused. `DE_BUILD_JOBS` controls build/AST workers separately. Per-contract deadlines, fixtures, assertions and complete-result checks are retained. The finite aggregate watchdog allows the complete test/AST graph to finish; it is not a processing-latency guarantee. Recreate build trees/private prefixes for the changed recipe.

- **Breaking (build):** unset ambient compiler search/flag overrides, including `CPATH`, `LIBRARY_PATH`, `CL` and `LINK`, before configuring or building. Keep CMake outside source/build trees; the documented setup uses an external tool environment. Refresh pinned developer packages with `python tools/install_build_tools.py --lint`; workflow validation additionally requires PyYAML and its typing stubs. Python remains development-only. Recreate existing build trees/private prefixes for the changed build recipe.
- **Breaking (Windows verification prerequisites):** the complete native suite requires Developer Mode or symbolic-link creation privilege; missing required fixtures fail instead of silently passing. No additional product runtime privilege is required.
- Native validation packages place their README and complete reviewed Markdown documentation under `share/docenhance/`, beside the schemas and original upstream notices. Use that README location; build instructions require the separate source distribution.

- **Breaking (C++ I/O/Windows paths):** bundle validation uses one `validate` callback; the partial `read_bundle_record` API and publication-observation enum are removed. Windows drive-relative publication paths such as `C:result` are rejected; use a fully qualified drive path or an ordinary relative path. Windows publication requires native open-by-ID support and refuses SMB output destinations; use a supported local volume. POSIX output files now use owner-only creation modes; explicitly grant file permissions when sharing results.

- **Breaking (build/C++ integration):** native builds use a checked private JSON header with allocation-free, bounded recursive destruction. Recreate build trees and private dependency prefixes for the changed recipe, and use that header consistently in integrations. Record DOM admission remains limited to 16 levels; arbitrary-depth JSON is unsupported.

- **Breaking (execution ports/borrowing):** processing and verification results must supply coherent identities, publication states and request/stage observations. Invalid processing returns produce `E_PUBLICATION_UNKNOWN` (exit 7, `unknown`); inspect effects before retrying. Invalid read-only returns produce `E_INVARIANT` (exit 8) without publication. Metadata borrowing rejects temporary owners, and moved-from requests cannot execute. Response validation rejects zero record sizes, empty/oversized confirmation inventories and publication claims outside processing; wire/record versions remain unchanged.
- **Breaking (CLI authoring/C++ metadata):** each option descriptor requires a unique typed invocation-member `binding`; update custom authoring inputs and descriptor consumers. Generated bindings preserve absent versus explicitly empty values and are checked against flag/value spelling. There is no fallback transfer list.
- Contract generation checks method-option attribution in both directions and derives D01 identities, format/mode reporting and the response envelope version from reviewed contracts. Protection help now identifies both I01 and D01. Denoising schemas reject settings without a selected method; wire/record versions and numerical method definitions are unchanged.

- **Breaking (C++/authoring):** capability entries use `contract::InputSupport` instead of `app::InputSupport`; update native consumers. The unused CLI `contract_version` field is removed, and obsolete or unknown authoring fields are rejected without aliases or migration.

### Fixed

- Publication binds relative effects before callbacks and refuses observed replacement of staging, parent directories or created files. Retained native object leases prevent identity reuse during checks; copied record bytes cannot establish publication origin. A matching owned directory after a lost rename reply establishes `completed`, with `E_OUTPUT_VERIFY` if subsequent validation fails. Cleanup remains nonrecursive, and arbitrary equally privileged changes between checks and native operations remain outside the guarantee.
- Source and mask acquisition validate the opened regular file instead of checking a pathname before opening it; POSIX opening cannot block on a substituted FIFO before type admission. Opened objects remain the source of snapshot bytes when names are replaced. Bundle reads reject observed inventory changes and Windows root-binding changes during acquisition.

- Run-record parsing stops at its 1,024-event ceiling instead of continuing with growing discarded-object bookkeeping. JSON cleanup no longer allocates a traversal stack that could terminate the process under memory pressure. Nearest-rank percentile selection bounds worst-case comparisons at O(n log n) by sorting the existing caller-owned scratch, avoiding adversarial quadratic work without changing I01 quantiles.

- Validate binary row ranges before borrowing, preventing out-of-range memory access. Native bundle directory owners refuse inactive/moved access and escaping or NUL entry names; Windows alternate streams are refused. CLI resource-failure handling allocates no diagnostic, so sustained allocation refusal cannot escape the boundary or trigger execution retry.

### Internal

- Independent nightly OSV monitoring queries verified dependency identities and reports every source match. New or changed findings require review; code-bound exclusions distinguish affected upstream source from code excluded by the configured build. TIFF archive version queries and empty-result limitations remain explicit.

- CI exercises the declared Python 3.12 minimum, with setup and static-analysis declarations checked against `deps/tools.json`. Standard and strict tooling discovery agree, required architecture documents are checked, and fuzz setup acquires the complete reviewed source lock. CI and local hooks share the aggregate source gate; parsed workflow checks reject decorative or optional commands, lost matrix coverage and untruthful aggregate success. Formatting refuses empty discovery, and real Git controls challenge pinned AFL source-tag drift. Compiler-backed architecture checks bind trusted paths by resolved identity and read actual header traces, so aliases, spaces and dollar signs cannot hide forbidden layer/package reach.

- Native and isolated-fuzz verification checks every application compilation command for requested fatal sanitizer instrumentation and rejects source opt-outs/recovery. Native sanitizer workflows require real benign/fault detection controls; failures report bounded child diagnostics and retain full logs in the tested build tree. Color fuzzing uses independent structured sample references and treats unexpected conversion errors as findings; native link refusal/cleanup tests fail on missing fixtures instead of silently returning.

- Independent executable references now check complete color/orientation/protection → I01 → D01 → quantization composition, including direct-window integer NLM-L1, maximal windows, native strength rounding and once-only observations. A real producer mutation proves detection of premature 16-bit quantization that processing decode-back and bundle verification can both accept. Processing mathematics and method versions are unchanged.

- CLI parsing binds generated option metadata directly to typed invocation storage, removing parallel maps and handwritten transfer assignments. Distinct codec, admission and publication safety boundaries remain.
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
