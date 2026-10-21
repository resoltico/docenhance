# Thresholded unsharp masking

S01 is explicit `--sharpen unsharp`, after illumination, denoising, restoration and contrast and before
the terminal output boundary. It is off by default and is never selected implicitly. All admitted
PNG/JPEG/TIFF preserve, gray and binary outputs share this stage. Private options require
`--sharpen unsharp`, even at amount zero.

The validated parameters are sigma 0.8 (finite 0.3..3 pixels), amount 0.5 (finite 0..2), and
threshold 1 (finite 0..20 equivalent 8-bit perceptual intensity points). Threshold is divided by
255 internally; neither input nor output is converted to eight bits for this computation.

For entering binary64 linear luminance Y, let f=E(Y). Form the normalized discrete Gaussian blur
b of f with radius `ceil(3*sigma)`. Each coefficient is `exp(-k*k/(2*sigma*sigma))`; horizontal
and vertical passes use float64 and REFLECT_101. A singleton dimension reflects by replication.
Then:

```
d = f - b
r = sign(d) * max(abs(d) - threshold/255, 0)
raw = f + amount*r
candidate = clamp(raw, 0, 1)
```

There is no second blend. If candidate equals entering f, retain the entering Y exactly; otherwise
store E^-1(candidate) in the owned scalar Y plane. Stages do not repeatedly transport RGB. At the
terminal boundary, preserve-color reconstruction uses the original interpreted RGB available from
the retained immutable source/interpreter and transports to final Y once. Gray output quantizes
final Y directly. Binary output quantizes final Y once to Q16, then classifies every destination
into logical 0/1 and physical PNG1; protection does not exempt a pixel from classification.
See [PNG processing](../formats/png.md) and [binarization](binarization.md).

All entering samples, including protected neighbors, are blur context. Protected destinations
retain their stage-input Y exactly and are excluded from excursion/clipping observations.
Protection is not segmentation. Computation uses frame F after exact geometry; supplied masks
remain in B and follow G02 into processing coordinates. See [geometry](../pipeline.md).

Amount zero is an algebraic bypass after domain and working-view validation and eligibility
counting. No eligible destinations also returns unchanged. Neither case allocates a Gaussian
field. Constants remain exact, including at threshold zero. An actually entered selected S01 with
positive amount emits `W_SHARPENING`, including completed constants, thresholded identities and
fully protected images, and actual entered failure context. A selection left unstarted by an earlier
failure does not emit this warning. Increased
edge contrast is not blur inversion or evidence of additional detail, and clipping can discard
information.

## Design and separate challenge

The first-party image-layer Gaussian primitive supplies the shared border/constant behavior.
Its general radius limit remains 384 (769 coefficients); S01's sigma limit gives radius at most
nine and uses a fixed nineteen-double coefficient array. No additional native adapter, package or
architecture permission is required. Discrete coefficients and separable arithmetic are explicit;
an independent mathematical reference evaluates direct 2D convolution.

The eager stage gathers E(Y) into one full perceptual field, including protected context, then uses
a second float64 plane for the horizontal pass. The vertical pass replaces the first field with
the final blur. The horizontal temporary is released when fitting returns. The fitted blur is
immutable through application against the entering Y, then released before the stage returns.
Only the updated Y and immutable completed Facts continue downstream. Final reconstruction,
encoding and independent integer/metadata comparison do not retain or refit this blur, rerun a
method or recount interpretation/stage observations. No full RGB transfer plane or quantized
intermediate is needed for the stage.

Challenge singleton/short dimensions against radii up to nine, exact constants, soft-threshold
equality, both signs and strict clipping endpoints. Include protected impulses as context,
exclusion of protected excursions, source immutability, row padding and wide-row interiors,
16-bit ramps and ordered composition with every contrast alternative. Wrong domains stay errors
at amount zero. Require active changes as well as protected/no-op controls. Resource refusal and
numerical errors never downsample or become automatic skips. Source authoring does not establish
that the new caller path, any platform or natural-document benefit has been qualified.

## Resources, cancellation and records

Let R(b)=64*ceil(b/64). For width W and height H, each charged float64 field occupies
P=H*R(8*W) bytes. Fitting's additional payload is exactly 2*P; application retains one P-byte
blur until it returns. The unpadded two-field size is 16*W*H bytes. There is no stage RGB transfer
block or retained verification model. The nineteen-double coefficient array and at most 64 fixed
row-band observation slots are bounded ordinary metadata. All field allocations use the
[selected processing ledger](../architecture.md#values-ownership-and-budgets) alongside the actually
resident Y/protection, source/interpreter and
codec/output storage. Earlier stages release their temporary fields before this stage starts.
This charge budget is not a process-RSS or wall-clock limit.

Allocation, gathering, Gaussian passes and mapping have bounded cancellation checkpoints.
Gathering/mapping check at 1,024-sample row intervals; Gaussian passes check at 128-pixel intervals,
and each S01 dot product uses at most nineteen coefficients. Cancelled/partial internal planes
are not valid results. Source bytes remain unchanged, workers join and temporary owners refund
charges on return or unwind. Real-error precedence and the final publication cutoff retain their
[cancellation](../architecture.md#cooperative-cancellation) and [bundle](../record.md) contracts.

The [current response and record format](../record.md#source-observations-and-format-compatibility)
contains closed S01 parameters and actual completed Facts. Disabled selection and its completed
slot are null. Selected bypasses return `no_change` with `zero_amount` or `no_eligible_samples`,
zero evaluated/context/corrected/clipping counts and null `pre_clamp`. A completed active traversal
records eligible/protected/evaluated/changed counts and full-canvas `context_samples`.
`corrected_samples` counts raw!=f before clamping. `pre_clamp` is the finite minimum/maximum raw
over evaluated eligible destinations. `clipped_low_samples` counts strictly raw<0 and
`clipped_high_samples` strictly raw>1; equality is not clipping. A clipping fraction can be derived
from their sum divided by evaluated samples when nonzero; it is not a stored wire field.

These values are checked against the actual extent, parameters and each other. Counts and warnings
come from typed Facts; charged high-water bytes belong to execution diagnostics. A failure retains
actual earlier completed Facts and the failed family, without a fabricated completed S01 value;
private runtime context can retain entry into positive sharpening for its warning. Later encoding
or publication failure retains already completed S01 Facts. Read-only artifact/record agreement
never reruns sharpening and does not certify authenticity or original document meaning.
