# Bounded luminance denoising

This is the reviewed D01 implementation contract. Capability admission requires the implementation
and executable evidence below. D02, presets, binary processing and configurable operation graphs
are outside this operation; no fallback method is substituted.

## Admission and mathematics

D01 is explicit `--denoise nlm`; the default is `off`. Private parameters require `nlm`:
`--nlm-h` finite [0.1,25], default 3; `--nlm-patch` odd [3,15], default 7;
`--nlm-search` odd [7,41], default 21, at least patch; `--denoise-blend` finite [0,1],
default 0.5. Active denoising requires search to fit the smaller oriented dimension. Zero blend
and entirely protected inputs bypass native execution after parameter, source and mask validation.
Continuous PNG/JPEG preserve/gray are supported; any denoise option on bw is rejected.

The entering samples are opaque oriented linear sRGB doubles, after the frozen I01 operation.
For each RGB triplet C, Y = 0.2126 R + 0.7152 G + 0.0722 B and f = sRGB_encode(Y), using
`image::numeric`'s existing transfer definitions. Quantize Q = floor(65535 f + 0.5).
The locked OpenCV 5.0.0 single-channel CV_16U fastNlMeansDenoising vector-strength overload,
NORM_L1, defines Qd. Native strength is the actual float `257.0F * float(h)` and is recorded
as that representable value. Native integer normalization, weight cutoff and rounding are the
reference, rather than an independently invented floating NLM implementation.

Use signed int32 subtraction before division: fc = clamp(f + (Qd-Q)/65535, 0, 1).
If Qd == Q, retain entering RGB exactly. Otherwise fn = (1-blend) f + blend fc;
if fn == f, retain entering RGB exactly. Transport to sRGB_decode(fn) with the existing
neutral-axis luminance transport. Protected destinations always retain entering RGB exactly;
protected neighbors remain comparison context. Brightening can reduce saturation; this is not
chromatic denoising, semantic mark recognition, JPEG deblocking or lost-detail recovery.

## Execution and ownership

The host composes interpreted linear source -> immutable I01 view -> prepared D01 view -> one
shared final quantizer. Numeric method types and correction kernels remain native-free. A narrow
`de_denoise` adapter owns OpenCV calls, reservations and exception containment, using only core,
image and methods. The host gains that one edge in spec/architecture.json. Native types never
escape adapter-private files; CLI, application, records and numeric kernels acquire no native effects.

D01 preparation creates budgeted Q and Qd planes once. Preparation traverses the entering view
once, completing I01 application observations before any native tile starts. Retained decoded
source, converter and immutable I01 model can replay the exact entering samples for reconstruction;
replay never refits, requantizes a saved image, reruns NLM or changes stage counters.
The denoising result remains immutable for encoding and independent output verification.
A separate reconstruction traversal establishes D01 working-space effects and completion before
publication; equal final encoded bytes do not erase a genuine working-space modification.
No-op denoising must still complete preceding I01 during the normal output traversal.

Stage reports distinguish disabled, no_change, applied and failed, with complete separate from
publication. Failure preserves completed earlier stages, partial counts and the actual error.
Selected but unreached D01 has incomplete failed observations, never a fictional completed prefix.
Success admission checks requested settings, complete reports and consistent counts. Numeric
status does not promise publication, delivery, meaning or authenticity.

## Tiles, native memory and work

Use sequential 256 x 256 central output tiles. The original blueprint's 512 is deliberately reduced:
with maximum halo r = (search-1)/2 + (patch-1)/2 = 27, native input is at most 310 x 310.
Exterior samples use REFLECT_101 of the global Q plane, including corners; retain only centers.
Native's own padded border is beyond every retained output's dependency domain.

Pinned denoising.cpp sets stripe granularity max(1, area/131072). At 310² this is exactly one;
parallel.cpp calls the body directly for one stripe regardless of the selected platform backend or
thread setting. Consequently one native scratch owner runs per call, without changing process-global
thread policy, application workers or GCD assumptions. Concurrent requests own independent scratch.
A regression verifies this source-bound invariant and native tile/full-image equivalence.

Two padded uint16 full-page planes cost approximately four bytes/pixel. Two reusable first-party
tile planes are also charged. Before each native call reserve its conservative working storage
without allocating a dummy buffer. core::Reservation shares the Budget ledger and refunds by RAII.
For input w,h and patch p/search s, audited native payload is bounded by
2(w+2r)(h+2r) for padded samples + 4s²(1+p+w) for distance arrays + 4*65536 for the
weight table, plus 64 KiB for fixed controls/alignment/strength/Mat objects. Actual native allocations
remain native-owned; reserved memory and directly charged buffers are reported separately.
Allocation failure is contained as E_RESOURCE with reservation refund; no disk backing or reduced
resolution/precision/window fallback exists. Check all extent/product arithmetic before native use;
p <= 15 keeps maximum signed patch-distance sums 225*65535 below INT_MAX.

Cancellation checkpoints occur per linear block, halo row, tile, reconstruction block and downstream
encode/verify/publication. Native tiles cannot be interrupted: latency is at most the remaining tile
call, with p/s bounded above. Check native errors before subsequent cancellation. Bound computational
work by the admitted 40M-pixel source and parameter/tile maxima; report actual runtimes, including
maximum-window tiles. This is a finite work bound, not a wall-clock deadline or process-RSS promise.
The existing final precommit cutoff and truthful publication reconciliation remain authoritative.

## Records and verification

New production records use version 3, with a closed denoising request and execution report; new
responses use schema version 3. Only the current record format is accepted; obsolete formats are refused, with no migration.
Version 3 requires
source decoding, full output verification and matching complete stage observations. Binary records
carry explicit off/disabled D01 only. Writer, complete reader, generated schemas and public response
share one typed serialization. Method identity is D01 version 1; I01/B02/B03 versions are unchanged.
Record native strength, L1, depth16, tile width256, reflection, requested settings, eligible/protected,
quantized correction and actual working-space changed counts, native calls, reserved native peak and the combined preparation charge peak. These peaks describe
completed native calls; a failed attempted call does not manufacture a reservation observation.

## Required evidence

Independent scalar tests cover transfer/quantization, signed correction, blend, transport, exact
between-level no-op/protection, extremes and resource arithmetic. Native comparisons cover tile
seams/exteriors/partial tiles/all patch/search extrema, worker settings and 16-bit patterns.
Exact-budget/refund, allocation failure, deterministic cancellation, concurrent requests and stage
prefix failures require tests. Real executable contracts cover PNG/JPEG, gray/color, PNG8/16,
I01 on/off, orientation/masks, source/mask refusal, option presence, current record refusal and
schema/reader negative fixtures. Decode-back verification reuses prepared results with no native replay.

A fixed-seed P04 equivalent explicit D01 fixture requires >=10% flat-region variance reduction and
retained disconnected punctuation at a fixed measurement threshold. Already-good pages, weak marks,
colored annotations and high-strength damage are assessed separately. Synthetic measurements do not
certify arbitrary handwriting. Measure representative 3/12/24MP executions and native maxima with
build/settings identity, directly charged/reserved preparation peaks, independently observed allocations and RSS.
Instrument the actual pinned OpenCV photo/core closure in isolated fuzz builds, retain reproducers,
and run all required reference/tooling/native/sanitizer/Linux Docker gates before PR submission.

## Output-profile composition

Canonical grayscale output uses a D50 gray ICC profile with the standard parametric sRGB decoding
curve (ICC function type3). Parameters are serialized in signed 16.16 form; no sampled 16-bit curve
is substituted. Pixel quantization still uses the double sRGB definition, so ICC fixed-point
parameter precision and pixel precision are separate. The pinned reader refuses curveType tables
larger than 32767 entries; the former 65530-entry output table could be written and byte-verified
but could not be interpreted on a subsequent processing request. Parametric output resolves that
composition defect and is tested through actual serialized-profile reopening and I01->D01 input.


The native build applies a source-digest-checked Mat ownership correction in a generated translation
unit. The pinned cache is not modified. A unique owner is allocated before pixel storage; failure
of either allocation releases all acquired resources. No NLM weights, neighborhoods, rounding or
parallelism change. The override retains the original upstream license and is part of the explicit
feature configuration. Allocation fault injection covers both control and payload paths.


OpenCV diagnostics are configured once inside the native boundary: a stateless error callback
suppresses foreign stream delivery while native exceptions still propagate to the adapter. Native
parallel-backend code overrides its compile-time log setting, so explicit silent logging is also
required; no per-request thread setting or diagnostic global mutation is performed. Source digests
bind the audited denoising/parallel behavior as well as the Mat ownership override.

See [resource observations](denoising-resource-observations.md) for measured payloads, RSS and
runtime. NLM's noise family assumes approximately additive white Gaussian noise in the perceptual
scalar plane; admission does not certify that a source meets that model.
