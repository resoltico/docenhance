# Morphological illumination: design and separate challenge

This records the reviewed I02 design. Capability admission requires the implementation and
verification below. I02 is an explicit alternative to I01, never an automatic fallback.

## Contract and integration

`--illumination morph` selects I02 for continuous preserve/gray PNG output from the existing
PNG/JPEG/single-page TIFF admission. `auto` retains I01 exclusively. Shared strength, gain and
target defaults retain the current repository values (1, 2 and source). Morph alone accepts
`--background-radius auto|1..256`; auto resolves half-up rounding of min(W,H)/50 clamped to [8,128].
Surface/auto reject radius presence; morph rejects cell, quantile and smooth presence. Off and
binary processing reject background options. Validate all parameters and the existing oriented
protection mask even for zero strength or unit gain.

Interpretation/orientation/alpha remain at the existing converter boundary. Analysis uses finite
linear luminance. Protected samples are excluded from statistics and temporarily filled with
eligible luminance Y90, never inpainted in output. Source reads for analysis do not count output
observations. Invalid unprotected RGB is an error. No eligible pixels is a no-change operation;
fewer than min(16,W*H) eligible pixels is explicit method inapplicability.

For radius r use square dilation followed by square erosion, both with REFLECT_101; then use
Gaussian sigma=max(0.5,r/2), radius ceil(3*sigma), and REFLECT_101 in each separable pass. A
length-one axis reflects to its only sample. The discrete Gaussian coefficients are exp(-i*i /
(2*sigma*sigma)); ordered numerator and coefficient sum define normalization in float64.
A constant neighborhood returns its exact entering value instead of rounding an identity through
weighted summation. Use the existing deterministic lattice and nearest-rank statistics:
stride=max(1,ceil(max(W,H)/1024)), cap 1,048,576; if no eligible lattice point exists, use the first
eligible row-major sample. Background statistics use the same eligible locations.
Source target is smoothed background B90; an explicit target overrides it. An all-black field is
valid and has zero source target, producing no change rather than a fabricated positive estimate.

Share I01's gain floor 0.02, clamp(target/max(B,0.02),1,max_gain), exponent strength, saturation
accounting and neutral-axis luminance transport. Protected destinations bypass application exactly.
Prepare once, retain an immutable scalar field and use it for encoding and independent verification.
Keep contiguous output completion, real-error precedence, final publication cutoff and D01 ordering.

Typed illumination and requested-parameter alternatives distinguish Surface from Morphology.
Status/reason names describe illumination rather than surface fitting. I01 solver/grid observations
remain method-specific; I02 reports resolved radius, sigma/support, analysis fill, sample count,
stride/fallback, background quantiles/range, resolved target, retained bytes and preparation charge.
No-op reports do not claim field preparation; failed stages retain partial diagnostics. Method-owned
validation rejects foreign I01 fields, impossible geometry and invalid scalar/resource domains at
the application boundary; incomplete background reductions may retain unfinished ranges. Closed
response and record contracts move together to version 6; obsolete forms have no reader/migration.
Method versions retain I01=1 and introduce I02=1.

## Chosen implementation and resource contract

Use first-party float64 separable kernels in the existing numerical layer. This preserves the
blueprint equations without adding OpenCV allocation/exception/global-diagnostic obligations to
illumination. The blueprint's native-primitive route was considered; the existing NLM native
adapter has a separate responsibility and is not broadened into an unrelated processing manager.
No new package, layer edge or exception boundary is needed.

Whole-page fields avoid composed-halo ambiguity. Two charged float64 planes ping-pong through
horizontal/vertical dilation, erosion and Gaussian smoothing. A monotone ring deque uses at most
2r+1 indices; Gaussian support uses at most 769 coefficients. Global sample scratch is at most
8 MiB, source RGB blocks are bounded by the existing linear block size. Release sample scratch
before field preparation and the second field before background statistics. Retain one field for
output replay. Charge actual aligned plane/storage bytes; report peak shared-ledger charge, not RSS.
The existing 1 GiB budget includes source, mask, converter, I02 and later D01 lifetimes. Refuse
insufficient memory without reducing radius, precision, density or image dimensions. Large valid
pages can exceed the working budget. Check cancellation inside transfers, extrema/deque work,
Gaussian output pixels (each dot product has at most 769 coefficients), measurement blocks and application,
with fixed bounded observation intervals.

## Separate skeptical QA

A separate slow square-neighborhood experiment agrees with the separable closing for 1x1, 1x9,
7x1, 3x5 and 7x9 inputs at radii 1, 3 and 16. It verifies exact constant preservation and retains
a broad dark mark at radius 1 as a documented limitation. Gaussian normalization experiments cover
radii 1, 2, 8, 128 and 256, support <=384, and exact constant-one results. Closing is not opening;
negative/broad marks are not promised to disappear. A tiled alternative would require dependency
radius 2r+ceil(3*sigma) for the complete composition, so a single-operation halo was rejected.

The challenge requires source/mask immutability, protected analysis fill rather than output fill,
sparse-mask lattice fallback, no NaN masking, method-specific option presence and report identity,
no automatic I02 selection, ownership refunds on every failure and immutable verification replay.
Independent numerical tests must compare slow two-dimensional extrema/Gaussian references against
production kernels, including degenerate axes, maximal radius, borders, constant/ramp/impulse and
wide-mark cases. Real executable references cover grayscale/color, precision, masks/orientation,
I01/I02/D01 composition, no-ops, diagnostics, records and refusals. Raw method/CLI fuzzing and real
compiler, sanitizer, native package and exact-commit CI workflows are required before merge.
