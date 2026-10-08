# Known-PSF Fourier restoration

R01 is explicitly selected with `--deblur wiener --psf gaussian|motion|kernel`.
It runs on linear luminance after illumination and denoising, before contrast and sharpening.
It uses the existing continuous PNG/JPEG/TIFF domain; binary input/output admission is unchanged.
The PSF describes blur in the already-oriented processing frame. No recipe, automatic selection,
blind PSF estimation, geometry operation or broader binary pipeline is introduced.

The model is `Y ≈ h*u + n` for a user-specified spatially invariant PSF. The application does
not estimate or verify the camera PSF. `W_RESTORATION_INFERENCE` accompanies every reported enabled R01
stage, including identities. Failures before stage observations exist can omit the restoration report. `W_PSF_AFTER_TRANSFORM` reports an applied non-normal orientation or a preceding illumination/
denoising stage that actually changed samples. Enhanced strokes are inferred candidates, not
proof of original detail, document meaning or authenticity. The positive `K` in the denominator
is a chosen regularizer, **not a measured noise-to-signal ratio**.

## Admission and kernels

All restoration options are continuous-only and retain explicit presence. With restoration off,
PSF and numerical options are errors. `--psf` is required with `wiener`; Gaussian parameters cannot
be supplied for motion/kernel, and motion parameters cannot be supplied for Gaussian/kernel.
`--wiener-k` defaults to 0.01 in `[1e-5,1]`; `--deblur-blend` defaults to 0.5 in `[0,1]`.
Every supplied number must follow the strict finite decimal grammar. Zero blend validates all
options and loads/validates a supplied kernel before bypassing numerical restoration.

Gaussian `--psf-sigma` defaults to 1 in `[0.3,5]`. Radius is `ceil(3*sigma)`; the square kernel
samples `exp(-(x*x+y*y)/(2*sigma*sigma))` and normalizes its positive sum in float64.
Motion `--psf-length` defaults to 5 in `[1,31]`, and `--psf-angle` defaults to 0 in `[-180,180]`.
Radius is `ceil(length/2)+1`. Deposit `N=max(64,ceil(32*length))` equally weighted midpoint
samples along the centered exposure segment into four neighboring pixels with bilinear weights,
then normalize in float64. Positive angle is clockwise from positive x in y-down coordinates.

Kernel `--psf-file` requires one regular, static grayscale PNG without transparency, at original
8/16-bit depth, at most 1 MiB encoded bytes, and odd width/height in 3..129. Stored unsigned samples are raw nonnegative
coefficients; gamma/profile and orientation do not reinterpret or rearrange them. Structural
PNG integrity remains mandatory. A zero sum is an input error. Normalize in float64, retain
coefficients and supplied-byte identity in the record, and use the central pixel as origin.
Report the centroid displacement from that center; displacement greater than 0.25 pixels emits `W_PSF_OFF_CENTER`. Do not
recenter an asymmetric kernel. The kernel and source files are opened read-only. Relative PSF paths resolve when the kernel is
opened during loading, before FFT preparation; loading retains one immutable encoded snapshot.
The request and supplied identity retain the admitted PSF path spelling, which may contain personal
information. Read-only input loading does not snapshot the process working directory for the
whole processing pipeline. Publication retains its existing transaction-scoped directory binding.

## Fourier contract

For `r=max(kernel_halfwidth,kernel_halfheight)`, guard `g=max(32,4*r)` on every side. The FFT width
and height are the smallest sizes at least `W+2*g` and `H+2*g` whose prime factors are 2, 3, 5.
Extra right/bottom samples also use global `REFLECT_101`, including singleton-axis handling.
There is no zero padding of image content. Kernel sample `(i,j)` occupies
`((j-halfheight) mod fft_height, (i-halfwidth) mod fft_width)` in a zero kernel frame.
This centered displacement defines convolution phase; no generic image `fftshift` is used.

Compute padded luminance mean `mu` in float64. Use full complex float32 spectra:
`F=DFT(Y_padded-mu)`, `H=DFT(h_padded)`, `U=conj(H)*F/(|H|^2+K)`.
Inverse with explicit scale and real output; restore `mu`, then crop the exact original canvas.
Subtracting/restoring `mu` prevents systematic DC attenuation by `1/(1+K)`.
Constant inputs stay constant within the explicit float32 tolerance of 2e-6 for every admitted K.
This mean belongs to the reflected padded frame; it does not promise an unchanged cropped-image
mean for every nonconstant image.

Blend original linear luminance and the raw restored candidate **before** clamping:
`T=clamp((1-a)*Y+a*u,0,1)`. Apply the common neutral-axis color transport. Protected destinations
retain their entering linear RGB exactly; protected neighbors remain restoration context.
Prepare one immutable restored-candidate field and retain it through output verification. Verification reuses
that field and does not repeat transforms or count observations again. Final integer quantization
happens once; float32 numerical precision does not introduce an 8-bit intermediate.

## Resources and cancellation

Resolve and check actual padded dimensions and native-transform storage before execution.
The charged-buffer budget covers retained numerical fields, padded work arrays and the
conservatively reserved native FFT workspace. For padded pixel count Q, the native reservation
is `40*Q + 8 MiB`; explicit float buffers add 24 bytes per padded pixel, alongside the original-canvas
float64 candidate, aligned transfer rows and live source/profile/codec payloads. Refusal is `E_RESOURCE_LIMIT`; there is no automatic
resize, lower-precision alternative, reduced guard or copy-success fallback. This is a charged
payload/native-workspace bound, **not a process-RSS limit**. Bounded standard-library PSF
coefficient vectors and their report/record copies (at most 16,641 doubles per vector), strings,
small metadata and native control bookkeeping are outside this charged payload accounting. Existing source, codec, profile and
publication limits still apply.

Bounded first-party loops check explicit execution cancellation. OpenCV native DFT calls can be
observed before and after each call, but a running native transform is not interrupted. No deadline
or maximum cancellation latency is promised. Native errors retain precedence over a concurrent
stop request. The existing final precommit cutoff and truthful publication states remain in force.

Responses and run records use format 11. Closed requests and observations include the PSF,
regularizer/blend, normalized coefficients, centroid, padding/FFT geometry, eligible/context
counts, raw/blended excursions, charged preparation peak and explicit warnings. Only complete
current records are accepted; older records have no reader or migration.

Independent executable references use direct complex Fourier sums rather than the production FFT,
with centered-delta and asymmetric phase cases, constants across K, reflected borders, Gaussian
and clockwise motion coefficients, 16-bit precision, color transport and protection. These are
numerical conformance evidence, not real-document recovery benchmarks.

The development resource probe warms the CPU backend and the observing thread's C++ exception
runtime before measurement, then observes native allocations on square,
rectangular mixed-radix and long singleton-axis canvases. Ordinary replaceable C++ allocation hooks
cover FFT contexts/AutoBuffer storage and a shared Mat allocator covers native Mat payloads;
actual charged external plane storage is added conservatively. TSan malloc/free hooks observe
aligned external and native heap blocks directly, without adding those same external bytes twice.
The probe compares this observation to the declared reservation and injects each observed C++
allocation failure, requiring resource errors, preserved source samples and full refunds. This
bounded probe is evidence for those shapes and the pinned CPU path, not a universal peak-RSS limit.
The caught allocation-exception warmup initializes runtime state outside the measured operation;
on Darwin, LLDB identified first-throw exception TLS as a persistent 16-byte `calloc` allocation
through `__cxa_get_globals`. No observed allocations are filtered, and every injected failure
still requires zero retained observed heap bytes and a full charged-buffer refund.

The pinned CPU FFT factories require checked private ownership and dispatch corrections: each of the four 1D/2D
contexts gains an owning `Ptr` before initialization. Stock raw-pointer factories leak a partially
initialized context when allocation throws. The dependency recipe verifies the exact original
source SHA and each replacement's single occurrence, retains upstream notices and compiles a
private copy without changing the locked cache. Its dispatch table uses correctly typed forwarding
functions for all six float32/float64 real, packed-inverse and complex kernels. Casting their typed
function pointers to a generic `void*` data signature produces undefined indirect calls; erasing
only the data pointers through matching-signature forwarding functions leaves their mathematics
unchanged. The dependency audit checks that actual compilation
uses precisely that reviewed copy. Content hashes bind identity; the allocation-failure probe
separately proves cleanup behavior at the exercised failure positions.

The zero-byte `fuzz/regressions/restoration/native-dispatch-abi` reproducer is intentional: an empty
harness seed still prepares and executes restoration. Instrumented upstream dependencies exposed
the function-type mismatch on that first seed. Keep the input empty; appending explanatory bytes
would alter the retained finding. Native dispatch checks also exercise all six float32/float64
kernel alternatives against analytic impulse/DC expectations under dependency instrumentation.
