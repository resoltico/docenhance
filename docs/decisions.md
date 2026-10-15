# Design decisions

These decisions describe the current implementation and its rationale. Release history belongs in the [changelog](../CHANGELOG.md). [Architecture](architecture.md) owns the layer responsibilities and trust boundaries; reviewed manifests and schemas own machine contracts.

## Language and build ownership

C++23, CMake targets/header file sets, Ninja and an isolated dependency superbuild keep compilation and ownership explicit. C++ modules and arbitrary build frameworks are not part of the contract. Shared presets select `DE_TOOLCHAIN`: analysis requires the admitted LLVM major on Linux/macOS, while release uses the platform toolchain. Configure refuses incompatible compiler/library/SDK/deployment/diagnostic settings rather than falling back.

`deps/lock.json` owns upstream identities, `deps/features.json` features/private adaptations, and `deps/tools.json` tool versions and deployment floors. Acquisition is explicit; normal configure/build verifies cached sources without network acquisition or ambient-library substitution. Each configuration owns its prefix and products. Source identity and configuration consistency do not establish trust or reproducible binaries. [Dependency policy](dependencies.md) owns review, attribution and advisory limits.

The selected libraries serve actual production or test owners. Codec, color and OpenCV adapters contain external types, allocations and exceptions. There is no development link-demonstration executable or unrelated global library allocator/thread policy. Production-adapter controls and actual compilation/import/feature audits establish the relevant boundaries.

## Typed admission and fixed execution

The CLI parses syntax into `app::Invocation`, retaining raw option presence. Application admission constructs the private `ProcessRequest` containing `BinaryPlan | ContinuousPlan`; only continuous plans contain photometric/protection state. The host executes geometry, illumination, denoising, restoration, contrast, sharpening and final quantization at fixed owners. The entry layer composes the host with the CLI. Tests/fuzzers can supply non-I/O ports, without a runtime bypass flag.

No plugin registry, JSON parameter-map engine or arbitrary recipe/DAG execution is needed. Runtime method catalogs come from actual implemented types. Reviewed classification distinguishes selectable, automatic and planned methods; G01 is automatic metadata orientation without a selector. Method arguments alone derive option attribution. Generated syntax/catalog declarations cannot admit an unimplemented operation. Current [format authorities](bundles.md#source-observations-and-format-compatibility) independently own response and record versions; obsolete forms have no reader or migration.

## Errors, observations and delivery

Expected failures use `std::expected`, through `core::Result`. Numeric kernels do not throw/catch or own I/O/process state. Authorized native, scheduling and transport boundaries contain foreign exceptions. Before processing effects, the application prepares a complete unknown-publication fallback whose return move cannot throw. Unreported escaping exceptions cannot imply that processing never started. Returned results retain coherent actual identities, publication and stage observations; contradictions remain unknown.

`methods::StageObservations` owns five family reports. [Stage progress](bundles.md#stage-progress-and-numerical-disposition) separates selection, preparation, observing traversal and numerical disposition. Actual-entry ordering preserves untouched stages and earlier work. Illumination can remain prepared across later identities or refusal; gamma preparation does not consume its prefix. Active successful denoising/restoration/sharpening preparation and measured contrast consume their entering canvas. Failure and replay cannot fabricate or overwrite these facts. No extra observing cursors or event log are required.

The exit status derives from the outcome. The report layer renders; the CLI delivers once with unformatted writes and explicit selected-stream flush. Library calls retain caller stream configuration and signal dispositions. Standalone POSIX SIGPIPE handling allows normal delivery failure without retrying buffered output. A complete JSON response can precede process exit 5; incomplete delivery or exits 5/7 do not prove absence of effects. [Cancellation](cancellation.md) and [bundles](bundles.md) define the exact rules.

## UTF-8 and filesystem identity

Command text and admitted paths must be well-formed UTF-8. Invalid scalar encodings and embedded path NULs fail before I/O, without normalization or replacement. POSIX backslashes remain filename bytes; Windows conversion belongs to the native adapter. Invalid identity text fails serialization; malformed diagnostic text can use an explanatory ASCII message without changing error/publication facts.

Relative effect paths bind once to native directory ownership while report spelling remains intact. Source admission checks opened regular files; bundle entries remain non-link objects in a closed portable namespace. Live leases prevent identifier recycling during ownership checks. Private POSIX modes, Windows open-by-ID/local-volume requirements and observed mutation refusal remain explicit. Native pathname rename/unlink cannot atomically compare a requested object identifier against an equally privileged mutator; trusted-ancestor limits are not a filesystem sandbox.

## Resources, borrowing and execution control

The shared ledger owns charged image/codec blocks, tracks high-water charges and retains accounting until the final buffer owner releases them. Subtraction-before-addition avoids overflow. Small metadata, C stream buffers and OS stacks are outside charged payloads; charged limits are not process-RSS limits. Native adapters reserve finite reviewed workspace and refund it through ownership. Estimates and reservations require independent native allocation/failure controls.

Views and callable/request/converter borrows require live, unmoved lvalue owners. Move readiness is checked. Prepared models retain their immutable fields through encoding and independent verification; reports with PSF vectors move into terminal results rather than allocating diagnostic copies after commit. Codecs keep C++ owners outside foreign jump frames.

Indexed scheduling bounds workers and work partitions. Every started worker joins, including partial launch failure. Genuine errors take precedence over cancellation; the lowest-index task error is selected, with thread-launch resource priority. Completed work is not cancelled by a later stop. Cancellation is request-scoped execution control, never a method option. OS handlers only set a lock-free static latch; bounded normal checkpoints observe it. Blocking native calls have no maximum cancellation-latency guarantee.

## Publication and read-only agreement

Complete bundles are encoded, closed, checked and validated in owned sibling staging. One final checkpoint authorizes an exclusive native no-replace commit. Later stop requests cannot revoke it. Known completed publication survives inspection failure; ambiguous commit/cleanup retains unknown effects. Cleanup touches only owned entries. Atomic visibility is not power-loss durability. Read-only `verify` checks artifact/record agreement, not authenticity or original document meaning. There is no automatic processing retry or crash-leftover recovery.

## Interpretation and numerical composition

PNG/JPEG/TIFF keep separate representation admission behind one read-only encoded snapshot and signature boundary. Stored-gray binarization retains its narrower domain and metadata policy; continuous processing preserves known scalar precision through transfer, alpha, linear composition and final quantization. Native ICC conversion has its separate float32 precision boundary. Unsupported declarations fail rather than being silently clamped. [Container guides](README.md#representation-and-method-guides) own exact domains and metadata precedence.

G01/G02 use checked integer inverse gathers without interpolation or extra full-page rotation buffers. B-frame masks transform identically into processing coordinates; F-frame PSFs and pixel windows follow geometry. Density transpose parity is exact. Perspective, deskew and dewarping are not implemented. [Geometry](geometry.md) owns the mappings.

Illumination fits one immutable surface or morphology field, excluding protected analysis samples and preserving protected entering RGB. I01 uses multigrid-preconditioned CG with exact Galerkin aggregation and true-residual admission; gain-bound dark-content rules use the method's existing scale. I02 uses first-party float64 closing and Gaussian smoothing. Explicit `auto` remains I01 only. No field approximation, lower precision or successful-copy fallback hides refusal.

D01 performs bounded integer NLM-L1 through the native adapter with sequential 256-pixel tiles, finite workspace reservations and corrected Mat ownership. D02 uses full-field float64 TV-L1 with exact gradient/adjoint boundaries; iteration exhaustion returns a usable iterate with a warning, not a convergence claim. [Denoising](denoising.md) and [TV-L1](tvl1-denoising.md) own their mathematics.

C01 measures eligible perceptual levels; C02 applies the specified power with exact endpoints; C03 owns charged immutable 1,024-bin contextual maps, mass-conserving redistribution and actual-center interpolation. Shared blending/neutral-axis transport preserves protected destinations. Row-major observation avoids refitting or recounting. S01 uses the bounded float64 REFLECT_101 Gaussian and thresholded unsharp reconstruction. Tail clipping and sharpening excursions can harm marks; numerical conformance is not document-quality proof.

R01 is explicit known-PSF Fourier inference after denoising and before contrast. Gaussian/motion/raw PNG coefficients, centered displacement, reflected padding, padded-mean restoration and pre-clamp blend define one retained float32 candidate. The positive regularizer is chosen, not a measured noise ratio. The source PSF is neither estimated nor verified; warnings and admitted path spelling remain visible. Native FFT calls are not interruptible internally. Correctly typed dispatch and immediate factory-context ownership are required by the private adaptation. [Restoration](restoration.md) owns the formula, bounds and inference limits.

B01/B02/B03 operate on admitted stored grayscale PNG samples. Otsu fixes 4,096 bins and deterministic earliest ties; Sauvola uses bounded exact integer moments and reflected statistics; fixed threshold uses its declared equality rule. Source/profile interpretation cannot silently change that binary domain. [Binarization](binarization.md) owns the definitions.

## Parsing and independent verification

SAX preflight bounds record bytes, depth, event counts and decoded-key uniqueness before DOM construction. JSON cleanup must not allocate inside noexcept destruction; the checked private header uses ordinary recursive container cleanup under admitted depth and fixed builder shapes. The cache and upstream notices remain intact. Bounded ordering gives percentile selection a worst-case comparison bound without altering nearest-rank semantics.

Source mentions, metadata and successful processes cannot establish complete verification. Native execution reconciles effective compilation/lint, executable discovery, assertion work and fresh results without hidden skips. Both fuzz engines retain source/binary/manifest identity, all seeds/regressions, full target duration and failure artifacts. Relocated package inspection independently compares the tested executable, schemas, upstream notices, native imports and actual processing. [Quality](quality.md), [fuzzing](fuzzing.md) and [build](build.md) own those requirements.

Source publication binds version, changelog and epoch-0 assets to one admitted commit and qualifies exact supplied bytes before remote transport. GitHub repository addressing is explicit. Source caches and configuration-private replay work follow [CI ownership](ci-performance.md), without restoring native products or prior results. Full current platform/minimum-OS, exact-main CI and package evidence are release-specific requirements; workflow declarations and newer-OS execution cannot replace them. Weekly deep checks provide independent ongoing coverage, not a source-publication prerequisite.

## Natural-document claims

[Document-quality evaluation](document-quality.md) freezes corpus/settings/build/panel/assignment and retains raw effects, distinct replays and exact integer/protection agreement. All intended roles and class denominators remain visible; harms, uncertainty and abstention are separate from engineering correctness. Rights, provenance, independent human review and registration chronology cannot be established by hashes. Synthetic controls do not establish natural readability, authenticity or recovered source detail. Signed/notarized native distribution has separate obligations from source publication.
