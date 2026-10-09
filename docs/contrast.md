# Levels, gamma and floating-point CLAHE contrast

## Design

Contrast is explicitly selected with `--contrast levels` (C01) or `--contrast gamma` (C02) or `--contrast clahe` (C03),
defaulting to off. It follows the selected illumination, denoising and restoration methods and precedes final
PNG quantization. Continuous PNG/JPEG/TIFF preserve/gray use the current float64 linear-RGB
working contract; no additional package or layer edge is needed. C03 uses the same pipeline and color transport.

For entering RGB, use perceptual luminance f=E(0.2126 R+0.7152 G+0.0722 B). All alternatives
blend g=(1-a)f+a*f_c, decode g to linear target luminance and use the existing neutral-axis
transport. Equal candidates or equal blended values retain entering RGB exactly. Protected
samples bypass output transport exactly, in processing frame F (currently rotated frame C); supplied masks remain in B
at admission. See [geometry](geometry.md).

Levels uses every unprotected sample, without a histogram, lattice or downsampling. Percentiles
are nearest rank: sorted index max(0,ceil(p*n)-1), including p=0 and p=1. Defaults are low=0.5
percent and high=99.5 percent, domains [0,10] and [90,100], with high>low. Let lo=Q(low/100)
and hi=Q(high/100). If hi-lo<1e-6, retain RGB with reason insufficient_dynamic_range. Otherwise
f_c=clamp((f-lo)/(hi-lo),0,1). Input f<lo or f>hi counts as clipped; equality with a bound does
not. Report lo/hi, measured sample count and clipped counts/fractions among evaluated eligible
samples. This deliberate tail clipping can discard weak information.

Gamma has default exponent 1.2 and domain [0.25,4]. Use f_c=f^gamma, with exact zero/one
endpoints. Gamma above one darkens midtones; below one brightens them. It does not apply an
independent power to RGB channels and is not a recovery algorithm. Gamma one is an exact identity.
Blend defaults to one and is finite [0,1]. Private options require their exact selected method;
all contrast options, even explicit off, are rejected on binary output.

Parameter/source/mask validation precedes algebraic no-ops. Zero blend, no eligible samples and
gamma one perform no method pixel work. Levels with one eligible sample or flat quantiles still
measures its entering samples, then retains exact RGB. Protection excludes levels observations;
protected samples cannot influence the fitted thresholds.

Levels allocates one charged eligible-sample copy (8*n bytes plus alignment) and a bounded
4096-pixel RGB transfer block. Gamma requires only the transfer block. Shared charge peaks include
resident decoded, illumination and denoising storage; the 1 GiB charge budget is not an RSS limit.
Refuse insufficient memory without changing the method or sampling domain. Sort the copy once
using an interruptible worst-case O(n log n) heap sort. The shared quantile primitive also gains
bounded checkpoints, replacing uninterruptible sorting at existing illumination callers.

Freeze levels thresholds once. Reuse the same immutable mapping in output verification; neither
recompute quantiles nor count observations twice. Preparation/assessment distinguish a previously
observed prefix from a prefix first consumed for levels analysis or gamma application. A flat
levels measurement still completes its upstream prefix. Errors retain measured partial facts and
completed illumination/denoising observations. Unknown effects remain unknown.

Cancellation is execution control, with bounded checkpoints in mask counting, transfers,
validation, sorting, mapping and reconstruction. Preserve final publication cutoff and error
precedence. Numeric kernels remain free of I/O, allocation expressions and exception handling.

Typed alternatives own admission and the runtime catalog. Response/record version 12 includes a closed
contrast request and stage record, including explicit disabled records. Obsolete forms are rejected;
update consumers and visitors together. No recipes or arbitrary operation graph are introduced.

## Contextual histogram mapping

C03 defaults to an 8x8 contextual grid (each count 2..32), pre-redistribution clip multiplier
2 (finite 1..8), and blend one. For nonzero blend and any eligible samples, each tile must have
at least 16 pixels per direction. Boundaries are floor(i*W/columns) and floor(j*H/rows).
Zero blend and no eligible samples bypass pixel-dependent tile applicability after validation.
Private grid/clip options require CLAHE; binary output rejects every contrast option.

Each tile counts eligible perceptual samples in min(1023,floor(1024*f)). Fewer than 16 samples
or a true sample range below 1/4096 selects exact identity. Otherwise clip counts to
L=max(1,floor(clip*N/1024)). Redistribute excess E by adding floor(E/1024) to every bin and
one to the first E mod 1024 bins. The final counts may exceed L. CDF knots at k/1024 are
T_0=0 and T_k=sum(hist_i for i<k)/N; T_1024=1. No second clipping pass occurs.

Interpolate each map between intensity knots, then bilinearly blend neighboring maps using
the actual pixel centers of their integer tile extents: (left+right-1)/2. Outside the outer
centers use nearest maps. Identity maps return f exactly; equal interpolated results retain
that exact value. Protected destinations bypass transport; protected samples do not enter the
histogram or range. Eligible neighbors can influence maps across protected shapes.

Preparation streams entering RGB once in row-major order into all contextual histograms.
Charged storage is aligned rows of 1,025 doubles and three-double count/range rows times
columns*rows, plus the shared 4,096-pixel RGB transfer block; fixed identity flags are bounded metadata.
Count/range planes are released after fitting; maps remain charged through encoding and verification. Existing resident
prefix buffers count toward the same budget. Refusal does not alter grid, depth or sample domain.
The model owns its maps and is move-only. Verification reuses them without refitting or recounting.
Report identity_tiles (zero for other methods), measured eligible samples and charge peak, including
truthful partial observations on failure. No architecture permission or dependency is added.

The separate design challenge rejected equal-size nominal centers, independent stitched tiles,
8-bit equalization/up-conversion, a second clip pass, protected range observations, and refitting
at verification. A real-boundary challenge rejected tile-major source traversal: the upstream observation
contract requires row-major order. Row streaming removes a redundant full-image scalar copy
without changing histograms or the mapping. Independent executable references cover uneven dimensions, exterior
centers, redistribution, protection, exact identities, color transport and sub-byte output levels.
Deterministic checkpoint tests exercise allocation/measurement/application stops and charge refunds;
exact-budget and one-byte-short cases prove the charged workspace boundary.

## Separate challenge pass

A preimplementation scalar experiment passed rank/tie/endpoint cases, monotonicity over all
65,536 input codes for four gamma exponents, and 10,000 seeded color transports. Maximum
linear-luminance transport error was 2.220446049250313e-16. These are mathematical checks,
not platform or document-quality certification.

Challenge rank rounding, ties, single eligibility, p=0/1 and equality at the clipping endpoints.
Distinguish an eligible constant field from globally varying protected samples. Verify the strict
1e-6 boundary and exact preservation for identities and fully protected images. Test gamma's
actual power convention and monotonic endpoints on independent 8/16-bit and color references.

Try to make composition tests pass while a stage does nothing: require active changes and use
independent floating references, avoiding quantization of an intermediate as an oracle. Check
all illumination/denoising alternatives, prefix counters and immutable verification. Exercise
exact budget/one-byte-short refusal, every deterministic cancellation checkpoint, malformed
partial reports and records. Fuzz sorting, scalar maps, admission and record parsing. Verify
source archives and all required exact-head CI before merge.
