# Known-PSF Fourier restoration

R01 is explicitly selected with `--deblur wiener --psf gaussian|motion|kernel`.
It runs on current binary64 linear luminance after illumination and denoising, before contrast,
sharpening and terminal quantization. The common PNG/JPEG/TIFF domain supports continuous output
or terminal binary classification after this same photometric prefix. The PSF describes blur in
post-geometry frame C. No recipe, automatic selection or blind PSF estimation is introduced.
See [geometry](../pipeline.md) for coordinate and exact-rotation rules.

The model is `Y ≈ h*u + n` for a user-specified spatially invariant PSF. The application does
not estimate or verify the camera PSF. `W_RESTORATION_INFERENCE` derives at the response boundary
for completed R01, including identities, or available private context of an entered failed stage.
`W_PSF_AFTER_TRANSFORM` reports applied non-normal orientation, nonzero quarter-turn or preceding
illumination/denoising that actually changed samples. Enhanced strokes are inferred candidates,
not proof of original detail, meaning or authenticity. Positive `K` is a chosen regularizer,
**not a measured noise-to-signal ratio**. Warnings are not stored record configuration.

## Admission and kernels

All restoration options retain explicit presence. With restoration off, PSF and numerical options
are errors. `--psf` is required with `wiener`; Gaussian parameters cannot be supplied for motion/kernel,
and motion parameters cannot be supplied for Gaussian/kernel. `--wiener-k` defaults to 0.01 in
`[1e-5,1]`; `--deblur-blend` defaults to 0.5 in `[0,1]`. Every number follows the strict finite
decimal grammar. Zero blend still validates all options and a supplied kernel before bypassing
numerical restoration. No eligible destinations also bypass Fourier work after PSF validation.

Gaussian `--psf-sigma` defaults to 1 in `[0.3,5]`. Radius is `ceil(3*sigma)`; the square kernel
samples `exp(-(x*x+y*y)/(2*sigma*sigma))` and normalizes its positive sum in binary64.
Motion `--psf-length` defaults to 5 in `[1,31]`, and `--psf-angle` defaults to 0 in `[-180,180]`.
Radius is `ceil(length/2)+1`. Deposit `N=max(64,ceil(32*length))` equally weighted midpoint samples
along the centered exposure segment into four neighbors with bilinear weights, then normalize in
binary64. Positive angle is clockwise from positive x in y-down coordinates.

Kernel `--psf-file` requires a regular static grayscale PNG without transparency, original 8/16-bit
depth, at most 1 MiB encoded bytes and odd width/height in 3..129. Stored unsigned samples are raw
nonnegative coefficients; gamma/profile and orientation do not reinterpret or rearrange them.
Structural integrity remains mandatory. A zero sum is an input error. Normalize in binary64 and
use the central pixel as origin. Centroid displacement greater than 0.25 pixels emits
`W_PSF_OFF_CENTER`; an asymmetric kernel is never recentered. Inputs are opened read-only.

The host acquires immutable encoded source, mask and supplied PSF snapshots before rendering.
Relative paths resolve at acquisition without normalization or a whole-command current-directory
snapshot. A missing PSF can therefore fail before any numerical stage. An acquired malformed PSF
fails decoding at the entered restoration stage and preserves the actual completed prefix. A valid
supplied PSF retains the same charged encoded owner through publication as `assets/psf.png`.
Its role/path/digest/size are persisted; the original input path is not a persistent PSF identity.
The request retains the kernel selection, while PSF Facts retain kind, dimensions, normalized sum
and centroid. A coefficient vector is internal numerical state, not a duplicate record catalog.

## Fourier contract

For `r=max(kernel_halfwidth,kernel_halfheight)`, guard `g=max(32,4*r)` on every side. FFT width
and height are the smallest sizes at least `W+2*g` and `H+2*g` whose prime factors are 2,3,5.
Extra right/bottom samples use global REFLECT_101, including singleton axes; image content is not
zero-padded. Kernel sample `(i,j)` occupies `((j-halfheight) mod Hfft,(i-halfwidth) mod Wfft)`
in a zero kernel frame. This centered displacement defines convolution phase without a generic
image fftshift.

Compute reflected padded mean `mu` with compensated binary64 sums within each row, then compensated
combination in increasing row order. Independent 64-row tasks can fill rows concurrently without
changing this order. Subtract mu, compute `F=DFT(Y_padded-mu)`, `H=DFT(h_padded)`,
`U=conj(H)*F/(|H|^2+K)`, invert with explicit 1/(Wfft*Hfft) scale, restore mu and crop the original
canvas. The locked PocketFFT boundary uses binary64 real transforms and complex<double> half
spectra. Hermitian storage changes representation, not this Fourier equation.
Subtracting/restoring mu prevents systematic DC attenuation by 1/(1+K). Constant linear values
retain the reviewed 1e-12 bound for every admitted K. The mean belongs to the reflected padded
frame and does not promise unchanged cropped-image mean for arbitrary nonconstant input.

Blend original Y with the raw restored candidate before clamping:
`T=clamp((1-a)*Y+a*u,0,1)`. Protected destinations retain entering Y exactly; protected neighbors
remain Fourier context. Map the completed field eagerly, then release its workspace before the
next stage. Continuous output transports retained interpreted original RGB once to final Y;
binary output forms terminal Q16 and classifies every pixel, including protected destinations.
Encoding and independent decode-back comparison consume final expected samples without repeating
transforms or counting R01 again. There is one final quantization boundary.

## Resources and cancellation

Resolve/check actual padded dimensions and checked aligned plane sizes before their allocation.
The [selected processing ledger](../architecture.md#values-ownership-and-budgets) includes source/assets,
original RGB, current Y, mask and codec/profile
owners. R01 owns one aligned binary64 padded real frame and the charged Hermitian spectra.
At the second forward transform it holds signal and kernel spectra together; the kernel spectrum
refunds before inverse allocates its const-preserving spectrum copy. Padded row sums and final band
observations have separate bounded lifetimes. PSF vectors have at most 16,641 coefficients and
remain bounded control metadata outside the pixel payload ledger.

The FFT owner reserves the source-derived opaque provider capacity per actual row/column phase,
accounting for admitted smooth-size twiddles, construction/execution scratch, SIMD lanes and active
scheduler workers. The exact authority is `src/fft/workspace.cpp`; real and spectrum payloads are
additional. Row/column calls process blocks of at most 64, use one provider thread per call and
parallelize only through the supplied scheduler. Each phase selects no more than the requested
workers, reducing concurrency until its unchanged reservation fits; inability to fund one worker
refuses before that phase mutates samples. This candidate capacity must earn independent
production-translation-unit allocation observations; its source formula alone is not measurement
proof. Allocation failure returns E_RESOURCE with charged refunds. No resize, reduced guard,
precision reduction, disk spill or copy-success fallback exists. Charged high-water is not RSS.

First-party padding, compensated reductions, centering, frequency filtering and mapping check
cancellation at bounded intervals. FFT calls observe cancellation before/after each bounded block;
a running provider call is not interrupted. No universal deadline is promised. Provider exceptions
are contained at the FFT boundary; real worker errors retain precedence over concurrent stop.
Workers join before owner release. The final precommit cutoff and truthful publication states
remain governed by [cancellation](../architecture.md#cooperative-cancellation) and [bundles](../record.md).

## Observations and independent evidence

Current typed request and completed WienerFacts share one serialization/schema owner. Facts retain
PSF summaries, eligible/protected/evaluated/changed/context/corrected counts, nullable actual transform
geometry and padded mean, and raw/blended excursion counts. Zero blend or no eligible destinations
have no transform. A later failure preserves completed R01 Facts; failed/unstarted R01 does not
invent a completed value. Responses derive warnings once. Only current records are admitted, with
no compatibility reader or stored coefficient/warning arrays. Read-only verification checks actual
asset/record agreement, not recovery of the source's meaning or numerical correctness by itself.

Independent executable references use direct complex Fourier sums, a separately summed padded mean,
centered-delta/asymmetric phase controls, constants across K, reflected borders, analytic Gaussian
and clockwise motion, precision, alpha/color transport, masks and ordered stage composition.
They retain every original scalar tolerance; source authoring is not a claim those controls passed.
Actual FFT ownership, exact-budget/refund, allocation-failure, worker-equivalence, deterministic
cancellation and instrumented provider behavior require their concrete C++ controls and platform
workflows. Preserve all retained regression inputs, including intentional empty reproducer bytes.
These engineering checks are numerical conformance evidence, not real-document recovery benchmarks.
