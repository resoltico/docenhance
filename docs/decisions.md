# Design decisions

These decisions describe the current system and their rationale, not a session history. Change
them with the implementation; record user-visible changes in the [changelog](../CHANGELOG.md).
The [architecture](architecture.md) owns the detailed guarantees and limitations.

## Language and build model

Use C++23, explicit CMake targets and header file sets, Ninja presets and an isolated dependency
superbuild. A second build framework, a pre-standard language baseline or C++ modules would add
portability obligations without resolving a demonstrated runtime requirement. Ordinary translation
units therefore do not enable automatic module scanning. Dependencies are acquired explicitly;
a normal build verifies the cache rather than downloading or substituting host packages.

Every shared preset also names the compiler it is validated with, in `DE_TOOLCHAIN`. The host
`c++` is a moving reference: Apple clang implements fewer `-Wextra` diagnostics than the pinned
LLVM clang and GCC, so an unpinned analysis preset would pass locally on code that the required
jobs reject. The analysis presets therefore require the reviewed LLVM major, and `release`
declares the platform's own toolchain because it builds the shipped binary. Neither contract
falls back to another compiler; configure fails instead.

`deps/lock.json` owns source identities, `deps/features.json` upstream feature policy and
`deps/tools.json` tool versions and deployment floors. Exact pins and receipts detect changed
inputs; they do not prove that upstream code is trustworthy. Source/licence review and package
inspection remain necessary. Keep processing interfaces free of external types and libraries behind their owning adapters.
The reviewed bundle/report JSON field-mapping interface is explicitly permitted by the layer manifest. The native dependency probe is a verification program, not
permission to use every pinned imaging library in production.

## Typed admission and explicit execution

The CLI translates syntax into `contract::Invocation`. Application validation constructs a private
`ProcessRequest`, and the processing port accepts only that admitted value. `de_host` implements
the port; `entry` composes it. Tests and CLI fuzzers inject a deterministic non-I/O implementation,
not a runtime bypass flag. This preserves realistic command coverage without filesystem authority.

Do not prebuild an arbitrary recipe engine or a dependency-injection framework. Extend admission,
execution and capability reporting together when another complete operation exists. The generated
catalog describes options and reviewed method definitions; it does not validate a processing plan.
The B02/B03 PNG operations are real. Unimplemented catalog entries remain unadvertised and return explicit
failure rather than a successful copy or no-op placeholder.

## Exception containment follows authority

Expected failures use `std::expected<T, Error>`, aliased as `core::Result<T>`. Numeric kernels do not
throw or catch. External-library, scheduling and transport boundaries contain exceptional failures
under permissions declared in the layer manifest. The application must also contain exceptions
escaping the processing port: only it knows that execution has begun and effects may have occurred.

Before calling the processor, prepare a complete unknown-publication response. Returning that
response on an unexpected exception requires no fresh diagnostic allocation; a compile-time check
requires the outcome move to be non-throwing. Do not declare the processor `noexcept`: terminating
would lose the very outcome distinction that callers need. Returned expected failures keep their
reported state. Pre-execution validation remains before this boundary, so ordinary argument
rejection still means processing did not start.

An outcome's exit status is derived from its typed payload, not independently writable state.
`de_report` alone renders it. The CLI emits once with unformatted writes, so an embedding caller's
width/fill settings cannot rewrite the serialized bytes. It explicitly flushes only the stream
carrying content, without reconfiguring standard stream ties or exception masks. Buffer insertion alone does not establish delivery: `flush()` can set `badbit` or throw.
A transport failure never causes processing or rendering to run again. This does not acknowledge
consumer receipt, define a new process-signal policy or promise filesystem durability. JSON
`exit_code` describes the rendered outcome, not a later transport failure. A complete success
payload may therefore precede process exit 5; consumers must examine both without blind replay.

## UTF-8 is an admission contract, not a repair policy

Validate UTF-8 scalar encoding without allocation, locale dependence or normalization. Reject
malformed CLI tokens before CLI11 can quote them, and independently validate path strings in direct
application admission. NUL is a valid Unicode code point but not a valid embedded path character.
Unassigned scalar values and noncharacters are not malformed UTF-8; surrogate encodings, overlong
forms, truncated sequences and values above U+10FFFF are malformed.

Do not silently rewrite an identity to satisfy JSON. Strict serialization fails on a malformed
output path, whereas a malformed diagnostic gets a visibly explanatory ASCII fallback. This
separates identity from display text instead of allowing a global serializer replacement option
to change filenames. POSIX joins use `/`; a backslash there belongs to a filename. Windows path
conversion remains at the native adapter. Preserve admitted spelling, including decomposed Unicode.

The validator is checked over every scalar value and against an independent strict JSON decoder
in its own engine-agnostic fuzz target. CLI fuzzing and executable path tests exercise admission,
serialization and native publication together; a validator-only test is not sufficient.

## Memory and schedule are explicit resources

`core::Budget` tracks charged image/codec blocks. Move-only buffers share ownership of the atomic
accounting ledger, so a buffer can outlive its originating budget handle. Checked planes and views
establish extent and stride before kernel access; kernels additionally reject unsupported overlap.
Small standard-library metadata, C stream buffers and OS thread stacks are not charged blocks.
Do not claim that a ban on explicit allocation expressions eliminates every implicit allocation.

The scheduler owns work partitioning and thread creation, not an imaging dependency. Indexed
regions keep the numerical kernel independent from worker count; scoped workers join on failure
and report a deterministic task error. A persistent pool or different scheduler is justified only
by a measured requirement, not by a desire for more abstraction. The present CLI exposes neither
`--threads` nor `--memory-mib`; the binarization host uses its documented internal resource ceiling.

## Publication is an irreversible boundary

Write a complete PNG into exclusively owned sibling staging, close and check the encoded file,
and prepare allocating response metadata before the commit operation. Commit uses a native atomic
no-replace operation, never an existence check as a substitute for that operation. Cleanup touches
only the paths the current attempt owns; foreign entries and ambiguous filesystem errors must not
be hidden by a recursive cleanup or a false safe-retry response.

Atomic visibility is not crash durability. The output parent and its ancestors are trusted; this
is not a hostile-filesystem sandbox. `unknown` publication and a missing or incomplete response
require inspection rather than automatic replay of a potentially completed operation.

## Enforcement must observe the real program

`spec/architecture.json` declares the layer graph once. CMake verifies registrations and direct
links. Compiler dependency output checks include closure, clang-query checks AST restrictions and
standalone compilation checks public headers. Tests must demonstrate forbidden behavior is
rejected, not merely assert that a policy file exists. Compilation or matcher failure is not an
empty success result. Source-only checks on platforms without compatible compiler tooling are
not equivalent to compiler-backed enforcement.

Warnings, clang-tidy, formatting, Ruff, mypy and size limits remain required. Suppressions must be
narrow, code-bound and explained; a new implementation does not inherit an exception because its
filename or line number resembles an old one. Fuzz targets compile the same production targets,
and CTest registration drives campaigns and normal corpus replay. Full ASan/UBSan and TSan jobs
are independent, so a failing sanitizer job does not prevent the other from running.

Use the real JSON Schema validator on executable responses. A permissive homemade subset cannot
establish conformance to a closed response schema. Generated contract files are byte-checked from
their reviewed sources. Required GitHub checks establish the submitted commit's CI state; a local
subset, a historical run or a newly authored workflow cannot establish it.

## Method variants and bounded local statistics

B02 provides a second real operation rather than a reason to build a generic recipe framework.
The admitted request holds a closed variant of privately constructed validated method values.
Raw option presence is preserved until admission; empty or wrong-method values cannot disappear
into defaults. Execution is an exhaustive visitor, and compiled alternatives provide the runtime
method catalog. A compile-time comparison rejects disagreement with generated reviewed metadata.

Sauvola uses exact integer rolling moments rather than full-page floating intermediates. Fixed
strips and slot-owned scratch bound auxiliary memory independently of page height and preserve
worker-count independence. Population variance, normalized R, reflection and equality are explicit
contracts, not library defaults. The complete mathematics, resource plan and separate design QA
are in [typed binarization](binarization.md). The existing float box-mean primitive remains useful
for other operations; it is not forced into a numerically or spatially unsuitable implementation.

The response schema template and method catalog jointly generate the delivered wire schema.
The application attaches the admitted method's identity to the host's published-image result,
so report metadata is neither a hard-coded B03 label nor an unchecked adapter claim.

## Cancellation belongs to execution, not method parameters

An owned stop token and a static interrupt probe are passed explicitly; methods do not read
process-global state. The process bridge only latches supported interrupts with a lock-free atomic
store. Normal execution observes the request, joins workers and cleans owned unpublished output.
A final precommit snapshot authorizes the native operation, whose actual outcome remains authoritative
thereafter. No monitor thread, asynchronous job system or forced-shutdown cleanup promise is needed.
The full contract and separate design QA are in [cancellation](cancellation.md).

## Output representation is not an enhancement method

Admission distinguishes a continuous-tone representation operation from binary segmentation.
The new `color` adapter owns context-local Little CMS resources; `image` owns native-library-free
raster/option/report types; `io` owns PNG framing, metadata and sample decoding. The host composes
those effects. No filter is advertised for a color/profile conversion. B02/B03 remain stored-sample
operations, not consumers of a silently changed color pipeline.

Retain decoded integer samples and convert bounded chunks instead of building redundant full-page
RGB float frames. A single metadata policy handles PNG declarations, including researched cICP
precedence, so native convenience APIs cannot silently override it. The shared publication owner
commits continuous output only after a separate decoder verifies every integer row and intended
metadata. Resource limits, deliberate breaks, independent references and design QA are in
[PNG processing](png-processing.md).

## Opt-in illumination is a measured operation, not a preset

A complete I01 implementation motivates the linear-block interface; a speculative universal
processing graph does not. Fitting precedes staging and returns one immutable log-grid model.
Measurement excludes protected samples and application bypasses them at the linear photometric
boundary. Encoding and independent verification reuse that model but do not count observations
twice. A shared final quantizer keeps the no-filter and enhanced paths consistent.

The fit must succeed on every admitted grid, so the solver's iteration count cannot depend on how
wide an unmeasured region is. Multigrid-preconditioned CG with exact Galerkin aggregation meets
that without a second solver, a direct factorization's memory, or a lowered residual bound.
Dark content is identified by the one scale the method already defines: a cell darker than any
admissible gain could correct is not illumination evidence, so no new tuning parameter is added.
Defaults are chosen from measured fixtures, favoring complete correction bounded by the gain cap.

Automatic predicates can skip an unsuitable image but cannot turn solver/resource failures into
successful skips. Keep illumination off by default; the planned balanced preset is not
implemented. Preserve B02/B03 stored-sample definitions and reject illumination on that branch.
See [illumination](illumination.md) for parameters, coverage, actual residual, bounds and tests.

## A second container generalizes source admission, not processing

PNG and JPEG use one read-only encoded snapshot/identity boundary and signature dispatch. Codec
adapters keep container-specific validation; shared raster metadata separates ICC/orientation/
physical resolution from a closed PNG/JPEG declaration alternative. The shared bounded IFD0
reader does not become a general EXIF framework. JPEG YCbCr expansion yields integer RGB before
the existing color adapter performs profile interpretation and linearization.

The classic libjpeg adapter uses a public, per-request charged memory manager, not its advisory
virtual-array limit as proof of total memory use. Its reviewed native bootstrap has a separate
conservative reservation. Full-resolution accurate integer decoding, upsampling, warning refusal,
scan/marker limits and cancellation are fixed together. Output always uses the existing verified
PNG bundle path. Current records use version 12; obsolete formats are refused without backward compatibility or
migration. Source format is not a numerical method-version change.
See [JPEG](jpeg-processing.md) for the exact domain and executable evidence.

## Native denoising proves fixed operation composition

D01 consumes the frozen I01 result before one shared final quantizer. Two uint16 planes retain the
native correction; original entering doubles are reconstructed from the retained source/model.
No native replay or observation replay occurs during verification. Protected destinations bypass
transport exactly; neighborhood context is still shared with protected pixels.

The native boundary owns effects and catches, while methods retain native-free types and correction
math. Sequential 256-pixel output tiles force one native stripe even at maximum halo, avoiding
process-global thread settings. Native scratch uses a ledger-only reservation with audited bounds,
independent allocation observation and RAII refund. See [denoising](denoising.md) and its separate
[design QA](denoising-design-qa.md). Current records/responses make a clean format break.

## Retire superseded project contracts directly

Use one current project contract, without legacy readers, forwarding APIs or migrations. Retire
PNG-only acquisition and image-only publication in favor of shared source admission and complete
bundles; preserve codec and reconciliation guarantees through the existing owners. Remove unused
multipage parser scaffolding until a complete processing requirement exists. The repo-wide
[contract and architecture audit](contract-audit.md) records the design and separate QA decisions.

## Native build identity and dependency confinement

Configuration is an admitted contract: native compiler family, standard-library capability, SDK,
deployment floor, architecture and required diagnostics must agree before dependency builds start.
One superbuild owns one prefix and its application child. A persisted configuration binding rejects
changed or unbound build trees, rather than mixing installed libraries with another recipe.
Dependency versions come from the source lock; the selected closure drives build, import and
feature/provider auditing. Source receipts supplement immutable archive/Git evidence. These
bindings establish identity and consistency, not source trust or binary reproducibility.

## Borrowing and completed native observations

Reject temporary owners at borrowed-buffer, owning-plane, request, callable and converter
boundaries. Keep views cheap and require the caller's lvalue owner to remain live and unmoved;
shared ownership of every view would hide that contract and add page lifetimes. Small shapes and
method parameters are returned as values. Retained execution control owns its stop token.

The processing port uses binary/continuous success alternatives, and the continuous alternative
is also the rendered outcome's typed value. Required stage observations stay together; semantic
cross-field checks remain at admission. The native reservation owner returns completed resource
observations, so the surrounding tile code neither predicts a charge nor duplicates the estimate.
The separate design and QA are in the [ownership/resource audit](ownership-resource-audit.md).

## Interpretation precision and metadata precedence

Known scalar interpretation normalizes integer samples in double and keeps that precision through
transfer, alpha, compositing and final quantization. The bounded native color engine is a separate
float32 precision boundary exposed through double row formatters. References must describe the
mathematics instead of repeating a private adapter's intermediate rounding.

All PNG operations share strict static framing; stored grayscale remains independent of color
and orientation interpretation. Validate selected EXIF fields before choosing physical metadata,
so a preferred declaration cannot hide malformed lower-priority fields. Strict decoder-policy
admission refuses unsupported values instead of clamping them. The [input/numerical audit](input-numerical-audit.md)
records the design and separate challenge cases.

## Execution control follows observed effects

Verification admission uses the same validate-before-stop rule as processing, while its response
cannot claim publication. Standalone POSIX SIGPIPE handling belongs beside native interruption
setup, without becoming cancellation or changing library callers' process state. Checked stream
completion happens before staged cleanup even on cancellation; actual read/write errors precede
a subsequent stop observation. PNG rereads share bounded native input callbacks, with reader
state outside jump frames. The [execution/publication audit](execution-publication-audit.md)
records the design and separate QA challenges.

## Completion and artifacts need independent observations

A successful test process is necessary, not sufficient: reconcile complete discovery and fresh
results, with actual assertion work and no skipped required cases. Source mentions in build files
are not compiler coverage. Package metadata must agree with the independently tested binary,
reviewed contracts and verified upstream license bytes; its own existence or hashes cannot prove
that agreement. Archive the committed source snapshot, so local work is neither leaked nor given
an inconsistent manifest. The [verification/artifact audit](verification-artifacts-audit.md) records
separate design and challenge passes and real negative controls.

## Reduce coordination at syntax and publication owners

Generated typed option bindings register CLI syntax directly against stable invocation storage.
Keep presence and domain admission separate, with no second option-name transfer list or map.
Publication chooses its representation, operation and observation owners together; binary values
cannot carry a converter, mask or continuous stage owners. Derived record fields are not separate
configuration. Prepared models remain owned by their caller through all row consumers. Keep codec
jump frames, error containment and commit/reconciliation boundaries distinct. See the separate
[simplicity design and challenge](architecture-simplicity-audit.md).

## Validate observations at the execution boundary

Typed success/error alternatives do not make supplied facts coherent. Application dispatch validates
returned identities, request agreement, successful numerical observations and error/publication
pairing. Shared pure validators also serve record verification. Malformed processing returns retain
the prepared unknown outcome; readonly verification failures retain not_started. Requests consume
the source path when moved, native directory access requires active confined ownership, and borrowed
metadata/rows enforce lifetime/range boundaries. The allocation-free CLI fallback remains separate
from fallible rendering. See [the separate design/challenge](execution-ownership-audit.md).

## Bound parser work and adversarial selection

A JSON discard callback does not stop the parser and can suppress closing callbacks required by
external bookkeeping. Admit depth, event counts and unique decoded keys through the library SAX
interface, whose refusal terminates parsing, before constructing a bounded DOM. Release admission
bookkeeping between the passes. The byte ceiling remains before parsing; no separate JSON lexer is needed. Percentile
selection sorts existing scratch instead of using average-linear partitioning; the worst-case
comparison bound matters for hostile sample order. Quantiles and resource ledgers are unchanged.
See [the resource design and separate challenge](resource-limits-audit.md).

JSON cleanup must not allocate from noexcept destruction, including during failed parsing. The
locked header's heap traversal stack violated that boundary under injected allocation refusal.
Use a checked private header with ordinary recursive container destruction, relying on pre-DOM
depth admission and fixed typed builder shapes. Keep upstream bytes/attribution intact, bind the
recipe to fresh private builds, and inspect installed bytes separately from behavioral fault tests.

## Bind filesystem effects to live native ownership

Resolve relative effect paths once without lexical normalization and preserve report spelling.
Check regular-file admission on the opened source handle. Retain native object leases for staging
and owned entries so identifiers cannot be recycled during checks; Windows uses the full supported
128-bit identifier. Create private POSIX modes directly and refuse observed parent/object changes.
I/O establishes publication origin by native directory ownership; the host validates contents through
one callback before and after commit. Retire partial-record reopening and its observation enum.
Keep trusted-ancestor and equally privileged mutation limits explicit: pathname rename/unlink cannot
atomically compare an expected object identifier. See [filesystem identity](filesystem-identity-audit.md).

## Release admission requires direct execution evidence

One reviewed CI coverage authority drives workflow checks and remote release admission. PR head
metadata can identify a branch while its checkout tests a merge; release eligibility therefore
uses direct exact-commit runs and their complete current attempt. Recheck before release effects
and read-back, preserve unpublished drafts on later refusal, and never overwrite mismatched remote
assets or mistake transport failure for proof of no effect. See [publication eligibility](build.md#source-publication-eligibility).

## Protection and advisory review observe different boundaries

Apply one mandatory native compiler policy to project code and compiled dependency code. Inspect
actual compiler commands independently from final executable protection markers: one linked object
can supply a canary symbol without protecting another object's functions. Preserve upstream
configuration and attribution while removing unused gzip-file sources from the private zlib build.

Monitor verified source identities against OSV separately from offline acquisition/build admission.
Report source matches even when mandatory feature closure excludes their affected code. Bind narrow
review dispositions to source, feature policy and all advisory fields except a validated
`modified` timestamp; changing observation time alone is not changed vulnerability evidence.
Refuse malformed timestamps, changed evidence and unknown added fields.
Database coverage and recipe exclusions remain explicit; neither pinning nor an empty result proves
absence of vulnerabilities. See [dependency review](dependencies.md#advisory-observations).

## Enforce owned interfaces rather than broad header aggregation

Filesystem stream/path ownership must not depend on a PNG context. Separate in-memory identity,
reserved-slot identity, publication, immutable bundle snapshots and byte-based PNG observations.
Keep encoding inside its publication transaction and retain typed admission, numerical, native and
record authorities; a generic method framework does not remove their distinct safety obligations.
One architecture restriction baseline has reviewed owner exceptions. Thread ownership requires the
scheduler permission even through transitive headers; private production headers stay within their
owner. Registered clients inherit only a named public closure. Observe final CMake links, not only
registration arguments. See [the design and separate challenge](architecture-boundaries-audit.md).

## Admitted values and computation completion

Decoded sample precision and orientation have validating factories and private construction; raw
container declarations remain distinct. Record admission returns explicit domain failures before
constructing those values. Preserve an error whole when forwarding it, including publication state.
Unimplemented capabilities refuse before execution; unsupported input within an implemented
admission path is an input refusal. A completed schedule does not poll an exhausted queue and invent
a cancellation outcome. Cancellation still stops skipped work and cannot erase genuine errors.

Raw charged storage deliberately has a write-before-read contract. Codec merging/coefficient
initialization stays bounded and interruptible at its owning boundary. Blanket zero filling is not
proof of correct initialization and would remove those checkpoints if substituted mechanically.
Independent numerical references poison padding and old output values and require finite results,
so NaN cannot disappear into an error accumulator. Suppressions name exact checks; fixed foreign
callback parameter-count exceptions do not change mandatory source-file line ceilings.

## Bounded TIFF sources share interpretation and publication

TIFF/BigTIFF adds a source decoder behind the same immutable acquisition, integer raster,
color/orientation/alpha, I01/D01 and verified PNG bundle contracts. One top-level IFD is admitted;
multipage input and unsupported sample/compression domains are refused. Direct strile decoding
retains 16-bit precision; the RGBA convenience API is unsuitable. Per-handle callbacks and the
shared charged JPEG allocator avoid process-global handlers and uncharged nested payloads.
A source experiment showed that expected Deflate output length does not establish checksum
completion; the locked adapter therefore requires complete striles. JPEG admission fixes its
policy after headers, with bounded scans and cancellation. Response/record version 4 closes the
TIFF source alternative and rejects obsolete forms. See [TIFF admission](tiff-processing.md)
for the separate design QA, exact domains, resource accounting and evidence obligations.

## Explicit morphological illumination shares luminance transport

I02 closes eligible linear luminance with a square maximum then minimum and smooths the field
with a normalized Gaussian. Protected samples receive the eligible Y90 fill only for analysis;
output protection remains exact. First-party separable float64 kernels keep this method within
`de_methods`, avoiding an additional native allocator/exception boundary. Two charged page planes
and bounded scratch refuse insufficient memory without approximation. The retained field is reused
for output verification and later D01; auto selection continues to select only I01.

Response/record version 5 introduces closed method-specific illumination parameters and morphology
observations. Current readers reject obsolete records. See [I02 design and separate QA](morphological-illumination.md).

## Full-field TV-L1 keeps floating precision and practical stopping

D02 uses five charged float64 page planes with frozen dual/primal passes, an edge-defined adjoint
and the specified L1 proximal operator. Output protection does not constrain the optimization.
Iteration exhaustion preserves a usable iterate with a warning, never a tolerance or convergence
claim. No native package or graph edge is required. Response/record version 6 distinguishes
D01 native observations from D02 solver diagnostics and rejects obsolete forms. See the
[design and separate numerical challenge](tvl1-denoising.md).

## Contrast freezes eligible levels and preserves gamma endpoints

C01 uses every eligible perceptual sample and an interruptible, bounded-memory ordering step.
C02 applies the specified power to perceptual luminance. Both reuse shared blending and the
neutral-axis transport, with exact protected output and algebraic identities. Scalar mappings
are frozen once and reused during verification. Response/record version 7 adds closed typed
contrast observations and rejects obsolete forms. See [design and separate QA](contrast.md).

## CLAHE freezes contextual maps without a redundant frame

C03 implements the blueprint's eligible-only 1,024-bin floating mapping, exact sparse/flat
identities, pre-redistribution clipping and actual-center interpolation. Streaming in row-major order
counts each sample once and avoids a second full-field copy. Charged immutable maps stay
owned through output verification. The existing perceptual blend and neutral-axis transport remain
the only reconstruction path. Version 8 closes CLAHE parameters and identity-tile observations;
obsolete records are rejected. See [design and separate challenge](contrast.md).

## A configuration owns consumable native outputs

The validated superbuild owner supplies executable and native-package destinations. The app child
still sequences first-party configuration after installed dependencies and owns its compiler database
and tests. Moving all targets or duplicating product binaries would add responsibility without need.
Package verification admits the owner and reads generated CPack naming through CMake; shell globs
and separately reconstructed version/platform names cannot select retained artifacts.

A separate challenge rejected lexical prefix checks (`out-other`), source aliases and an `out` link
into source. Physical containment admits only root-out or truly external configurations before
project setup. Root-only artifact reservation also replaces Ruff's broad default build/dist exclusions,
which otherwise concealed legitimate nested sources. Real CMake/CPack, negative source controls and
committed-archive fixtures exercise these boundaries. Changed bound recipes require fresh trees;
retained products and personal settings are preserved, with no cache migration or old-path reader.

## Unsharp retains one perceptual Gaussian field

S01 follows contrast and reuses the first-party bounded Gaussian primitive in the image layer.
The separate challenge rejects an additional OpenCV adapter and full RGB blur: existing borders,
exact constants and a two-field float64 fit provide the required mathematics with fewer moving parts.
Protected samples remain blur context; destinations and excursion observations follow their own
eligibility contracts. One frozen blurred field serves encoding and verification, with no extra
blend or intermediate quantization. Version 9 closes sharpening requests, reports and warnings and
rejects obsolete records. See [design and independent QA](sharpening.md).

## Global Otsu preserves the stored-gray binary boundary

B01 adds a parameter-free validated binary alternative and a charged histogram and retained threshold.
Quantization uses every decoded stored byte sample, not the blueprint's perceptual plane, eligible
mask or geometry padding. Extending that domain requires a separate reviewed contract. A second
score pass chooses the smallest threshold within tolerance of the global maximum, avoiding
order-dependent approximate ties. Single-bin fallback and exact endpoint polarity are reported.
Version 10 closes those observations and rejects obsolete records; B02/B03 mathematics and
Sauvola default selection remain unchanged. See [binarization](binarization.md).

## Known PSF is explicit inference with one immutable restoration field

R01 follows denoising before contrast. Gaussian, midpoint-bilinear motion and raw grayscale PNG
coefficients resolve one normalized PSF in oriented processing coordinates. A centered kernel
origin, global reflected guard and full complex float32 DFT preserve phase; subtracting/restoring
the padded mean avoids regularizer-induced DC attenuation. Native DFT calls share the bounded
OpenCV backend with NLM and remain noninterruptible during a call. Admission, resolved geometry,
conservative native reservation and before/after checkpoints are separate obligations.

A simpler direct spatial inverse would avoid FFT workspace but would implement a different
regularized model and discard the reviewed frequency equation. A second native FFT package would
add licensing/configuration and cancellation boundaries without need. The existing pinned OpenCV
core provides the transform while first-party code owns normalization, phase, filtering and blend.
The source/model fields remain immutable through verification; protected destinations and one final
quantization retain existing semantics. Inference warnings remain mandatory even for a zero-blend
or constant identity; no image agreement verifies the real camera PSF. See [restoration](restoration.md).

The independent native allocation probe exposed a pinned OpenCV CPU factory ownership defect:
allocation failure during initialization leaked the raw DFT context before its `Ptr` was formed.
A first-party catch cannot destroy a foreign owner it never received. The minimal dependency recipe
therefore establishes `Ptr` ownership before initialization in exactly the four FFT factories,
leaving DCT and all numerical operations unchanged. The original cache stays locked and untouched;
private copies preserve original upstream notices. Source identity/actual compilation admission
and real allocation-failure cleanup are checked separately. Reservation accounting alone cannot
prove native cleanup. No leak suppression or successful-copy fallback is admitted.

## Exact turns preserve named coordinate boundaries

G02 composes clockwise integer permutations after continuous G01 orientation. A lazy inverse
gather avoids a second full color raster and interpolation; a generic warp engine is unnecessary
for exact permutations. Binary G02 retains stored-gray metadata-ignore semantics and permutes
samples before thresholding. Masks are supplied and stored in B, then transformed identically
into C for computation; PSFs and photometric windows refer to F, currently C.

The separate challenge checks asymmetric/mirrored composition, non-square mask dimensions and
unequal density pairs: two transpositions cancel, and a B-frame asset cannot be checked against
C dimensions. G03–G05 stay unsupported until their complete resampling, footprint, bounds and
analysis contracts exist. See [geometry](geometry.md).
