# Thresholded unsharp masking

S01 is explicit `--sharpen unsharp`, after illumination, denoising, restoration and contrast and before final
quantization. It is off by default and is never selected implicitly. Binary processing rejects
sharpening arguments. Private options require `--sharpen unsharp`, even at amount zero.

The validated parameters are sigma 0.8 (finite 0.3..3 pixels), amount 0.5 (finite 0..2), and
threshold 1 (finite 0..20 equivalent 8-bit perceptual intensity points). Threshold is divided by
255 internally; neither input nor output is converted to eight bits for this computation.

For entering perceptual luminance `f`, form the normalized discrete Gaussian blur `b` with radius
`ceil(3*sigma)`. Each coefficient is `exp(-k*k/(2*sigma*sigma))`; horizontal and vertical passes
use float64 and REFLECT_101. A singleton dimension reflects by replication. Then:

```
d = f - b
r = sign(d) * max(abs(d) - threshold/255, 0)
raw = f + amount*r
candidate = clamp(raw, 0, 1)
```

There is no second blend. The candidate uses the existing neutral-axis color transport; entering
RGB is retained exactly when candidate equals entering f. All entering samples, including
protected neighbors, are blur context. Protected destinations retain their stage-input RGB
exactly and are excluded from excursion/clipping observations. Protection is not segmentation.

Amount zero is an algebraic bypass after domain validation. No eligible destinations also returns
unchanged. Neither case allocates a Gaussian field. Constants remain exact, including at threshold
zero. Every positive requested amount emits `W_SHARPENING`, including constants, thresholded
identities and fully protected images. Increased edge contrast is not blur inversion or evidence
of additional detail, and clipping can discard information.

## Design and separate challenge

The existing I02 first-party Gaussian primitive moves to the image layer and serves both methods.
This avoids a second border/constant implementation and an extra OpenCV adapter/permission edge.
Its current coefficient bound remains unchanged. Discrete coefficients and separable arithmetic
are explicit; the mathematical reference independently evaluates a direct 2D convolution.

Preparation gathers one full perceptual field in row-major order, including protected context,
then uses a second float64 plane for the horizontal pass. The vertical pass replaces the original
field with the final blur. This immutable blur survives output encoding and verification; it is
not refitted or recalculated during verification. The temporary plane and RGB transfer are released
before reconstruction. Earlier stage observations execute once; all subsequent prefix reads use
verification mode. An extra full RGB frame or blur of an already quantized output would add cost
or lose precision without satisfying the contract.

The skeptical pass checks singleton/short dimensions against radii up to nine, exact constants,
soft-threshold equality, both signs and strict clipping endpoints. It also checks protected impulses
as context, exclusion of protected excursions, source immutability, arbitrary block interiors,
16-bit ramps and ordered composition with every contrast alternative. Wrong domains stay errors
at amount zero. Resource refusal and numerical errors never downsample or become automatic skips.

## Resources, cancellation and records

Preparation charges two aligned float64 planes and one shared 4,096-pixel RGB float64 transfer
block. The two unpadded fields are `16*W*H` bytes; actual refusal includes row alignment and the
98,304-byte transfer. Only one aligned field remains in the model. Coefficient storage is a fixed,
bounded nineteen-double array. Charges use the shared 1 GiB processing budget alongside decoded
input and retained upstream models; this is not a process-RSS or wall-clock limit.

Allocation, gathering, Gaussian passes and reconstruction have bounded cancellation checkpoints.
Cancelled/partial fields are not valid results; all owners refund their charges. Worker joins,
error precedence and the final publication cutoff retain their existing contracts.

Response and record version 12 includes closed S01 parameters and observations, with no older readers or
migration. `pre_clamp` is the finite minimum/maximum over evaluated eligible destinations, null
when none were evaluated. `clipped_low_samples` counts strictly raw<0; `clipped_high_samples` counts
strictly raw>1; `clipped_fraction` is their sum divided by evaluated samples, null for no evaluation.
`context_samples` counts the complete entering canvas used to fit the blur. Counters and resource
minimums are checked against source/output extents; fractions and warnings derive from typed facts.
Immutable replay verifies output samples; it does not certify the original document's authenticity.
