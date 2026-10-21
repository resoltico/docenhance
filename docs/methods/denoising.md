# Denoising

## D01 bounded NLM

D01 is the explicit first-party NLM method, version 2. The common fixed pipeline applies it after
illumination and before restoration, contrast, sharpening and terminal quantization. It operates on
CPU-local scalar luminance and has no native-library, I/O or process-global state. No fallback method,
resolution, precision or smaller window is substituted when execution is inapplicable or out of memory.

### D01: Admission and mathematics

`--denoise nlm` selects D01; the default is `off`. Private parameters require `nlm`:
`--nlm-h` finite [0.1,25], default 3; `--nlm-patch` odd [3,15], default 7;
`--nlm-search` odd [7,41], default 21, at least patch; `--denoise-blend` finite [0,1],
default 0.5. Active denoising requires search to fit the smaller post-geometry dimension. Zero blend
and entirely protected inputs bypass preparation after parameter, source and mask validation.
The shared PNG/JPEG/TIFF input admission and output modes are specified by the codec guides and
[PNG processing](../formats/png.md). Both continuous output and terminal binary classification can
follow the same D01 operation; a binary output does not reinterpret a photometric protection mask.

The entering plane is finite binary64 linear luminance Y in frame C after metadata orientation,
quarter-turn rotation, source interpretation, straight-alpha composition and selected illumination.
For gray sources, interpretation retains the scalar directly. For color sources,
Y = 0.2126 R + 0.7152 G + 0.0722 B. Let f = sRGB_encode(Y), using the shared binary64 transfer,
and Q = floor(65535 f + 0.5). D01 retains an immutable uint16 Q plane for every patch comparison.

For patch side p, set n = p*p, eta = h/255 and normalization = n*65535*eta, in that order.
The table contains D = ceil(3*eta*n*65535) binary64 weights, with the multiplication order shown.
For exact integer patch L1 distance d < D, weight(d) = exp(-t*t), t = d/normalization.
Distances d >= D have zero weight. The distance sums all p*p absolute uint16 differences;
there is no distance binning, integer normalization or integer rounding of the resulting mean.
REFLECT_101 extends the immutable global Q plane for both patch centers and patch samples;
a length-one dimension reflects to its only sample.

For each unprotected destination, visit search offsets with dy outermost and dx innermost, both
in increasing order from -(search-1)/2 to +(search-1)/2. Accumulate the binary64 numerator
sum(weight*neighbor_center_Q) and denominator sum(weight) in that order. The self comparison has
weight one, so the denominator is positive. Their quotient is the fractional weighted mean m.
The exact integer patch sum is at most 225*65535 and fits uint32.

If m == Q, retain entering Y exactly. Otherwise compute
fc = clamp(f + (m-double(Q))/65535, 0, 1), then fn = (1-blend)*f + blend*fc.
If fn == f, retain entering Y exactly; otherwise set Y = sRGB_decode(fn). Protected destinations
retain entering Y exactly, while protected neighbors remain comparison context. Only the final
continuous output transports retained interpreted original RGB to final Y and quantizes once.
Binary output instead forms terminal Q16 once and classifies every pixel, including protected
photometric destinations. Brightening can reduce saturation; D01 is not chromatic denoising,
semantic mark recognition, JPEG deblocking or recovery of lost detail.

### D01: Execution, ownership and resources

The pure pipeline eagerly completes each selected stage before the next stage begins. A D01
failure preserves the actual earlier completed Facts and reports the failed family separately;
a disabled selection has no Facts. A later failure retains completed D01 Facts. Asset byte
acquisition precedes rendering, so an acquisition failure does not invent a numerical prefix.
The [bundle contract](../record.md) owns publication states, final cutoff and response delivery.

Shared charged owners are the immutable Q plane, the finite binary64 weight table, uint32 x/y
reflection-index planes and three uint64 observation rows indexed by band. D01 divides the image
into bands of at most 64 rows. Each scheduled band owns double numerator/denominator rows,
uint32 horizontal-distance rows, one difference row and one column-sum row. Rolling integer sums
avoid re-enumerating each full square patch; every band still visits all search offsets in the
specified order. Independent bands may run concurrently through the supplied scheduler, without
changing a pixel's accumulation order. Band observations combine in increasing band index.

For exact payload accounting let P_t(w,h) = h*64*ceil(w*sizeof(t)/64), the owned plane size including
64-byte row alignment. Let b = ceil(H/64), r = (patch+search-2)/2 and k <= min(64,H). Shared D01
payload is P_uint16(W,H) + P_double(D,1) + P_uint32(W+2r,1) + P_uint32(H+2r,1)
+ P_uint64(b,3). A live k-row band owns P_double(W,2k) + P_uint32(W,k+patch-1)
+ P_uint32(W+patch-1,1) + P_uint32(W,1). Concurrent live bands multiply only the latter term;
source snapshots, original RGB, current Y, protection and other pipeline owners remain charged
against the [selected processing ledger](../architecture.md#values-ownership-and-budgets). These
formulas describe payload, not allocator overhead or process RSS.

Checked plane allocation charges actual aligned bytes and refunds owners on every return/unwind.
D01 has no native reservation, second full-page filtered-Q plane or disk spill. The maximum table
size follows the admitted h and patch maxima; a smaller table would discard specified nonzero
weights. The 64-row decomposition bounds concurrent scratch without changing patch neighborhoods
or introducing image seams. Insufficient available budget returns E_RESOURCE, even for an otherwise
valid source. The admitted source and parameter maxima bound work, not elapsed wall-clock time.

Cancellation is checked before allocations and at bounded intervals in reflection/table preparation,
Q gathering, distance generation, accumulation and mapping. No uninterrupted full search campaign
or foreign call hides inside D01. Scheduling joins workers and returns real worker errors with the
existing precedence; the [cancellation contract](../architecture.md#cooperative-cancellation) owns those guarantees. Concurrent
requests have independent charged owners and do not mutate a global worker policy.

### D01: Records and verification

The typed request retains D01 identity/version and h, patch, search and blend. Actual completed
Facts retain disposition/reason, eligible/protected/evaluated/changed counts, corrected_samples
and weight_table_entries. Corrected counts fractional m != Q before blending; changed counts fn != f.
Zero blend and no eligible samples have zero table entries and no evaluations. Off remains null.
The full record is embedded in a successful command response. Failure responses carry only actual
completed Facts and the failed family. Warnings derive from actual request/context at the response
boundary and are not stored method configuration or persistent warning arrays.

Only current response and record forms are admitted; obsolete forms have no compatibility reader.
Typed Fields own serialization and native schema generation. Decode-back comparison verifies the
actual encoded output against the final expected samples without re-executing D01. Read-only bundle
verification establishes artifact/record agreement, not numerical correctness, authenticity or the
meaning of the source document.

### D01: Independent evidence and limits

The independent direct-window reference enumerates exact integer L1 patches and binary64 exp/mean
math without rolling sums or production calls. Its controls distinguish a fractional mean from an
integer-rounded mean, exact cutoff inclusion/exclusion, wrong I01/D01 order, premature 8/16-bit
quantization and incorrectly removed protected context. Composition retains original RGB and Y
through one final quantizer. References cover borders, band boundaries, patch/search extrema,
source precision, color/alpha, all metadata orientations, masks, no-ops and terminal binary output.
Exact-budget/refund, allocation failure, scheduler equivalence, deterministic cancellation and actual
stage-prefix failures belong to concrete C++ controls. Source authoring alone is not evidence that
these controls or platform workflows have passed; current results belong in status and release evidence.

The fixed-seed synthetic P04 fixture requires at least 10% flat-region variance reduction and retains
disconnected punctuation at a fixed measurement threshold. Already-good pages, weak marks and colored
annotations have separate controls. Synthetic outcomes do not certify natural handwriting or document
readability. Held-out quality evaluation follows [document quality](../document-quality.md), with provenance,
human review, harms and abstention reported separately. Measure runtime, charged high-water bytes and
RSS independently with the development tools described in [build](../build.md).

Canonical continuous gray output uses the D50 gray ICC profile with the parametric sRGB decoding
curve. Pixel quantization uses the binary64 sRGB definition; ICC signed16.16 parameter precision is
a separate boundary. Serialized continuous outputs may be reopened as inputs under the codec contract.
NLM assumes approximately additive white Gaussian noise in perceptual luminance; admission does not
certify that a source meets that model. D02 is the explicit floating-point alternative described in
[TV-L1 denoising](#d02-floating-point-tv-l1).

## D02 floating-point TV-L1

D02 is an explicit alternative selected by `--denoise tvl1`; denoising defaults to off.
It follows eagerly completed illumination, before restoration, contrast, sharpening and final quantization.
The common pipeline retains interpreted original RGB and current binary64 Y in frame C; D02 uses
sRGB perceptual luminance f=E(Y).
Input, primal, extrapolated primal and two dual planes use float64, with float64 reductions.
There is no integer analysis plane or eight-bit bottleneck.

### D02: Reviewed design

Minimize isotropic forward-difference TV(u)+lambda*sum(abs(u-f)), subject to 0<=u<=1.
Forward gradients are zero at the last column/row. The algebraic adjoint adds incoming edge
components and subtracts outgoing components; terminal dual components are always zero.
Use tau=sigma=0.25 and theta=1, with the specified unit-ball dual projection and clamped
L1 proximal update. Extrapolated values may lie in [-1,2]; they are not clamped to [0,1].

A full dual pass reads the unchanged extrapolated plane and updates each dual vector independently.
Only after it finishes does a full primal pass read the frozen new dual field. Primal updates use
only that pixel's old primal/input, so the old value can be retained in a scalar register while
updating the primal and extrapolated planes. This has the same simultaneous-iterate semantics as
double buffering, without mixed raster ordering. Five charged aligned float64 planes cost roughly
40 bytes/pixel. Retain input/result through the joined D02 map, then release them before later stages.
Account actual padding and shared-ledger peak, including source/assets, original RGB, current Y, mask
and codec/profile owners; eager illumination workspaces have already refunded.
The [selected processing ledger](../architecture.md#values-ownership-and-budgets) is not an RSS limit. Refuse insufficient resources; do not tile,
downsample, lower precision, reduce iterations or substitute a denoiser.

Parameters: lambda [0.05,20], default 1.5; iterations [10,1000], default 150; tolerance
[1e-8,1e-3], default 1e-5; shared blend [0,1], default 0.5. NLM and TV private options are
mutually exclusive and require their exact selector. All output modes share this same photometric
operation; binary output subsequently quantizes/classifies the final Y.
Validate source/mask even for zero blend and fully protected no-ops; those do not allocate a solver.

Measure both maximum primal update and maximum per-pixel Euclidean dual update each iteration.
At iterations 20,30,... require both <= tolerance at two consecutive checkpoints. Earliest
`tolerance_met` is iteration 30. Otherwise finish at the cap with `iteration_limit` and
W_TV_ITERATION_LIMIT, including caps 10/20 and nonmultiples of ten. If a second passing checkpoint
coincides with the cap, tolerance_met wins. This is a practical stopping test, not a convergence
certificate. Report initial/final objective; do not promise per-iteration objective monotonicity.
Objectives describe the full solver field before output protection and blending, not the final
encoded image. Evaluated samples exclude protected destinations; corrected samples count u!=f,
and changed samples count actual working-Y changes after blending.

Protection excludes photometric destinations: all entering samples remain solver context. Protected Y
retains its entering value exactly. Blend in perceptual space; equal candidate/input or equal blended
value retains entering Y. Continuous output transports interpreted original RGB once to final Y.
Binary output classifies every pixel, including protected photometric destinations. Encode and compare
final expected samples without rerunning the solver or double-counting D02 observations.

The TV penalty can smooth isolated marks along with noise. With nonzero blend, dark mark centers
can become lighter even while flat-region variance decreases.

Cancellation is explicit execution control: observe once per iteration and at bounded intervals
inside transfers, gradient/projection, adjoint/proximal, objective and reconstruction passes.
Discard partial buffers and preserve genuine-error precedence, actual completed earlier-stage Facts
and the existing publication cutoff. Successful iteration-limited solves return their final iterate;
the cap alone is not a numerical failure or a convergence certificate. If cancellation interrupts
the final objective or later application, an already observed cap remains reportable even though
no completed method result is returned. Private failure context retains the actual observed stop;
it is not a fabricated completed solver value.

Typed denoising alternatives and parameter variants derive the runtime catalog. D01 exact-distance
table fields remain specific to NLM; D02 nullable solver fields cannot be mixed with those Facts.
Actual completed D02 Facts retain method/disposition/reason, counts, context/corrected samples and
nullable solver observations. Disabled selections are null; algebraic bypasses have no solver.
Warnings derive once at the response boundary rather than being stored in persistent Facts. The [current response
and record contracts](../record.md#source-observations-and-format-compatibility) refuse obsolete versions. No package or layer
edge is added: pure TV math belongs to de_methods and the existing host composes execution.

### D02: Independent reference model

Check the adjoint by independent edge scattering, including 1x1, one-row/one-column and non-square
fields. Compare simultaneous double-buffer reference iterations against the two-pass implementation;
poison terminal dual values in rejection tests. Constant fields must remain exact. Verify bounds,
objective reduction on a defined noisy fixture after 400 iterations, low-contrast 16-bit distinctions,
and short-cap/noncheckpoint/final-checkpoint stopping cases. Never assert per-step monotonicity.

Test protected context versus a spatially constrained solver and exact protected output, zero blend,
all-protected validation, resource peak/one-byte-short refusal and cancellation at every bounded
checkpoint without sleeps. Real executable tests must cover I01/I02 composition, color/alpha,
8/16-bit precision, stage counters, iteration-limit warnings, bundle round trips and malformed
partial reports. Parser/solver fuzzing and complete native/sanitizer/CI runs precede merge.
