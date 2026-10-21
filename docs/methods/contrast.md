# Levels, gamma and floating-point CLAHE contrast

## Design

Contrast is explicitly selected with `--contrast levels` (C01), `--contrast gamma` (C02) or
`--contrast clahe` (C03), defaulting to off. All admitted PNG/JPEG/TIFF output modes, including
binary, share the fixed pipeline. Contrast follows illumination, denoising and restoration and
precedes sharpening and the terminal output boundary. Private options require their exact selected
method. No additional package, recipe graph or layer permission is needed.

Each stage eagerly updates the owned binary64 linear luminance plane Y through borrowed
`methods::Working` views. Gray interpretation supplies Y directly; RGB interpretation supplies
Y=0.2126 R+0.7152 G+0.0722 B. For an entering sample use perceptual luminance f=E(Y).
All alternatives blend g=(1-a)f+a*f_c and decode g to the next linear Y. Equal candidates or equal
blended values retain the entering Y exactly. Protected destinations bypass the map, and protected
samples are excluded from contrast fitting. Computation uses frame F, currently rotated frame C;
supplied masks are admitted in B. See [geometry](../pipeline.md).

Stages do not repeatedly transport RGB. Preserve-color output reconstructs from the original
interpreted RGB available through the retained source/interpreter and transports its luminance to
final Y once, on the neutral axis. Gray output quantizes final Y directly. Binary output quantizes
final Y once to terminal Q16 and classifies every pixel into logical 0/1 and physical PNG1, including
protected photometric samples. See [PNG processing](../formats/png.md) and
[binarization](binarization.md). No intermediate integer image is an enhancement input.

## Levels and gamma

Levels uses every unprotected sample, without a histogram, lattice or downsampling. Percentiles
are nearest rank: sorted index max(0,ceil(p*n)-1), including p=0 and p=1. Defaults are low=0.5
percent and high=99.5 percent, domains [0,10] and [90,100], with high>low. Let lo=Q(low/100)
and hi=Q(high/100). If hi-lo<1e-6, retain entering Y with reason `insufficient_dynamic_range`.
Otherwise f_c=clamp((f-lo)/(hi-lo),0,1). Input f<lo or f>hi counts as clipped; equality with a
bound does not. This deliberate tail clipping can discard weak information.

Gamma has default exponent 1.2 and domain [0.25,4]. Use f_c=f^gamma, with exact zero/one
endpoints. Gamma above one darkens midtones; below one brightens them. It does not apply an
independent power to RGB channels and is not a recovery algorithm. Gamma one is an exact identity.
Blend defaults to one and is finite [0,1].

Parameter/source/mask validation precedes algebraic no-ops. Working-view admission and eligibility
counting still occur for bypasses. Zero blend, no eligible samples and gamma one perform no fitting
or candidate mapping. Levels with one eligible sample or flat quantiles still gathers and sorts its
entering samples, returns the fitted range, then retains exact Y without evaluating destinations.
Protected samples cannot influence the fitted thresholds.

Levels gathers E(Y) once into one charged eligible-sample copy and sorts it once using an
interruptible worst-case O(n log n) heap sort. Only the low/high values survive fitting; the copy
is released before mapping. Gamma allocates no method payload workspace. Candidate passes use at
most 64 fixed row bands, independent of worker count, with bounded counter metadata.

## Contextual histogram mapping

C03 defaults to an 8x8 contextual grid (each count 2..32), pre-redistribution clip multiplier
2 (finite 1..8), and blend one. For nonzero blend and any eligible samples, each tile must have
at least 16 pixels per direction. Boundaries are floor(i*W/columns) and floor(j*H/rows).
Zero blend and no eligible samples bypass pixel-dependent tile applicability after validation.
Private grid/clip options require CLAHE.

Each tile counts eligible perceptual samples in min(1023,floor(1024*f)). Fewer than 16 samples
or a true sample range below 1/4096 selects exact identity. Otherwise clip counts to
L=max(1,floor(clip*N/1024)). Redistribute excess E by adding floor(E/1024) to every bin and
one to the first E mod 1024 bins. The final counts may exceed L. CDF knots at k/1024 are
T_0=0 and T_k=sum(hist_i for i<k)/N; T_1024=1. No second clipping pass occurs.

Interpolate each map between intensity knots, then bilinearly blend neighboring maps using
the actual pixel centers of their integer tile extents: (left+right-1)/2. Outside the outer
centers use nearest maps. Identity maps return f exactly; equal interpolated results retain
that exact value. Protected samples do not enter the histogram or range. Eligible neighbors
can influence maps across protected shapes, but protected destinations retain entering Y.

Fitting gathers the entering scalar Y plane once in row-major order into all histograms after
E(Y). The three-double count/range rows are released after fitting. Immutable knots and bounded
identity flags remain live only through this stage's application. Mapping completes and releases
the maps before sharpening, terminal reconstruction, encoding or verification. There is no
verification refit or second measurement. `identity_tiles` belongs only to C03 Facts.

## Resources, completed facts and cancellation

Let R(b)=64*ceil(b/64), the owned-plane row alignment in bytes. Levels' additional charged payload
is R(8*n), for n eligible samples, with no RGB transfer block. For T=columns*rows, CLAHE fitting
holds T*(R(8*1025)+R(8*3)) bytes: currently 8,320*T. Application retains only T*R(8*1025),
currently 8,256*T, until the stage returns. Gamma has no additional charged plane. These are stage
workspace charges, not the total processing high-water mark. The resident scalar Y/protection,
retained decoded source/interpreter and other actually live codec/output allocations share the
[selected processing ledger](../architecture.md#values-ownership-and-budgets). Earlier stages' temporary fields do not remain resident as replay models.
Refusal never changes the sample domain, grid, depth or method. Charged bytes are not process RSS;
ordinary metadata, scheduler/thread resources and allocator overhead remain outside this ledger.

A successful selected stage returns one immutable method-specific Facts value before the next
stage starts. Counts record eligible/protected/evaluated/changed samples. C01 additionally retains
nullable `levels` and strict low/high clipping counts; C03 retains `identity_tiles`. Fractions can
be calculated from those counts when the denominator is nonzero; no clipped-fraction or preparation
charge fields are stored. Charged high-water bytes belong to execution diagnostics. In the
[current response/record format](../record.md#source-observations-and-format-compatibility), disabled
selections and their completed slots are null. A selected no-op has actual completed Facts and a
specific reason. A failing stage has no completed Facts; actual earlier completed stages survive
with the optional failed family, and later stages remain unstarted. Output comparison cannot
mutate, replay or recount completed Facts.

Cancellation is execution control, with bounded checkpoints in eligibility, allocation, gathering,
sorting and mapping. Numerical mapping and gathering observe wide-row interiors at 1,024-sample
intervals; fitting also checks tile boundaries. Started workers join before owners release their
storage. An interrupted in-place Y plane is not a valid output and is discarded; the encoded source
remains unchanged. Real-error precedence and the final publication cutoff retain
[cancellation](../architecture.md#cooperative-cancellation) and [bundle](../record.md) contracts. Numeric kernels contain no I/O,
allocation expressions or exception handling.

## Reference properties

Challenge rank rounding, ties, single eligibility, p=0/1 and equality at the clipping endpoints.
Distinguish an eligible constant field from globally varying protected samples. Verify the strict
1e-6 boundary and exact preservation for identities and fully protected images. Check gamma's
power convention and endpoints on independent 8/16-bit and color references.

CLAHE references must discriminate uneven dimensions, exterior centers, redistribution, protected
range exclusion, exact identities and sub-byte output levels. Separate stitched tiles, 8-bit
equalization/up-conversion, a second clip pass or protected range observations violate this contract.
Require actual changes in composition controls and an independent floating oracle through one final
quantizer, rather than quantized intermediate CLI output. Check actual earlier Facts, no refitting
and immutable final comparison, exact budget/one-byte-short refusal, deterministic cancellation and
refunds, malformed reports/records and both fuzz engines. These are required controls; source
changes alone establish no execution result, platform qualification or natural-document benefit.
