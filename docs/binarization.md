# Typed binarization

This is the executable contract for **B01 global Otsu**, **B02 Sauvola** and **B03 fixed threshold**,
each at **method version 1**. It specifies the stored-sample grayscale operation, not a color-managed or
complete restoration pipeline. Preserve the original document; binarization discards information.

## Admission and execution

The CLI retains option presence separately from its spelling. An absent option permits a default;
an explicitly empty option is invalid. `de_app` constructs `ProcessRequest` only after validating
paths and creating one closed `methods::Binarization` alternative: `Otsu`, `Sauvola` or `FixedThreshold`.
Otsu has no parameters. The other parameter types have private constructors and validated factories.
The raw named
`SauvolaParameters` record is factory input, never an admitted execution value.

The execution visitor has an overload for each alternative, with no generic fallback. The runtime
catalog is constructed from those same alternatives and must equal the generated reviewed catalog
at compile time. Editing a JSON status cannot create an executable capability. The generator also
rejects duplicate identities/selectors, invalid versions and unknown implemented-option references.

The processing port returns the binary `PublishedBinary` alternative, not a method identifier supplied by an adapter.
The application attaches the identity of the admitted method to `Processed`; only the report layer
serializes it. A successful process response contains `method`, `method_version`, `output` and
`publication: completed`. `methods B01`, `methods B02` and `methods B03` return just the selected capability.
The schema template is `spec/command-response.schema.json`; method identities come from
`spec/method-contract.json`. The delivered schema and C++ descriptors are generated together.

## Parameters

| Method | Selector | Options and defaults |
|---|---|---|
| B01 | `--output-mode bw --binarize otsu` | No method-specific options |
| B03 | `--output-mode bw --binarize fixed` | `--fixed-threshold 0.5`, finite in `[0,1]` |
| B02 | `--output-mode bw --binarize sauvola` | `--sauvola-window 31`, odd integer in `[3,4095]`; `--sauvola-k 0.2`, finite in `[0,1]`; `--sauvola-r 0.5`, finite in `[1/255,1]` |

Window syntax is ASCII digits only. Leading zeros are accepted; signs, whitespace, fractions,
exponents, overflow and even windows are rejected. Decimal options use the existing finite-decimal
contract. A B03 invocation rejects every explicitly present Sauvola option, and B02 rejects an
explicit fixed threshold, even when the supplied value is empty or equal to a default. Otsu rejects
all explicitly present fixed/Sauvola options under the same presence rule. There are
no ignored method-specific options, recipes, compatibility aliases or inference of binary output from private method flags. In explicit `bw` mode, an absent selector uses Sauvola.

`R` uses **normalized grayscale units**. Its default `0.5` is `127.5` in byte units, not the value
`128` used by some byte-scale implementations. The normalized positive floor prevents meaningless
near-zero denominators and keeps all intermediate thresholds finite. No implicit unit conversion
based on the magnitude of an argument is performed.

## Samples and mathematical definition

Input is the same grayscale PNG subset as B03: 1/2/4/8-bit samples without transparency, expanded
to unsigned 8-bit stored samples. PNG gamma/orientation metadata does not change the stored threshold samples. Explicit G02
`--rotate` permutes those samples before thresholding, without applying metadata orientation; see
[geometry](geometry.md). The shared bounded
CRC/framing scan refuses animation, nonconsecutive IDAT, duplicate known declarations and bytes
after IEND before native grayscale decoding. Color,
alpha and 16-bit input remain rejected. Output is 8-bit grayscale containing only 0 and 255.

For B01, quantize every decoded stored sample `p` to `q = round(4095*p/255)` and
build a 4,096-bin uint64 histogram. Positive half ties round upward (the byte input mapping has
no half ties). Every admitted sample participates; there is no eligible mask, geometry padding,
perceptual conversion or floating input plane. This deliberately retains the current binary domain.
The blueprint's broader perceptual/mask/geometry pipeline requires a separate domain contract.

For each candidate integer `t` from 0 through 4094 with nonempty populations on both sides,
compute float64 `w0*w1*(mu0-mu1)^2`, with population fractions `w0`, `w1` and mean bin
coordinates `mu0`, `mu1`. Find the global maximum `best`, then select the smallest candidate
whose score differs from it by at most `1e-12*max(1,best)`. Do not update an approximate
best while walking candidates: that can make a chain of near ties order-dependent. Empty-class
candidates are excluded. If exactly one bin is occupied, use threshold 2047 and record
`single_bin_fallback: true`; otherwise record false. Black is exactly `q <= t`, with output
sample 0; white is exactly 255. Thus flat bytes 0..127 become black and 128..255 become white.
This is global Otsu; it does not adapt thresholds spatially.

The fitted threshold remains immutable through output verification; no refitting
or second observation count occurs. Successful B01 responses and records carry `threshold_bin`
and `single_bin_fallback` in `binarization`. Other binary responses carry null; other execution
records carry null. A true fallback requires threshold 2047. Response/record format 12 rejects
obsolete forms and unknown fields.

For B03, an input sample `p` is black exactly when `double(p)/255 <= threshold`. Its existing
rounding and equality behavior is unchanged.

For B02, form a centered `w` by `w` square at each sample, using `REFLECT_101` independently on
both axes. For extent `e > 1`, reduce the coordinate modulo `2*(e-1)` to a nonnegative remainder
`q`; the reflected coordinate is `q` if `q < e`, otherwise `2*(e-1)-q`. For extent one, it is zero.
Border samples are not duplicated at the reflection seam. Windows may exceed either image extent.

Let `N = w*w`, `S = sum(p)` and `Q = sum(p*p)` over that reflected window. Population variance is
used, not the sample estimator with denominator `N-1`. The specified evaluation is:

```text
V = N*Q - S*S                         # exact uint64 arithmetic
n = double(N)
m = double(S) / n
s = sqrt(double(V)) / n
T = m * (1 + k * (s / (255*R) - 1))
output = 0 if p <= T else 255
```

`S`, `Q`, `N*Q` and `S*S` cannot overflow for the admitted domain:
`255² * 4095⁴ < 2⁶⁴`. The kernel checks this bound at compile time. Exact integer cancellation
avoids a negative variance caused by subtracting rounded mean squares; no epsilon or variance
clamp conceals an error. Conversion to binary64 and square root occur only after that subtraction.
Fast-math and fused reassociation remain disabled. The final threshold is not rounded to a byte,
clipped, or modified by a visual heuristic. Equality is black: `k=0` reduces to a local-mean test,
and a constant zero image remains black. A positive constant image is white for `k>0`.

The local threshold follows Sauvola and Pietikäinen, *Adaptive document image binarization*,
Pattern Recognition 33(2), 225–236 (2000),
[DOI 10.1016/S0031-3203(99)00055-2](https://doi.org/10.1016/S0031-3203(99)00055-2).
This implementation is independently written from the mathematical definition. It implements the
local text-threshold operation, not every classification or post-processing procedure in that paper.
The normalized parameter domain, reflection rule, integer statistics and equality convention above
are the explicit DocEnhance contract.

## Memory and scheduling

B01 fits sequentially in fixed row-major order and uses one charged 32,768-byte histogram,
independent of image dimensions and worker count. It reserves that storage before filling or
writing output and refunds it after fitting; only the fitted observations remain through verification.
Histogram counts and moments are
bounded by the decoder's 40-million-pixel ceiling. Score scans are fixed-size float64 work;
The histogram is charged to the existing binary 128 MiB budget; score scans do not allocate scratch.
That charged-buffer budget is not a process-RSS limit. Invalid/overlapping views, insufficient
budget and cancellation refuse without publishing a partial result.

The following strip scheduling describes B02 only.

Do not build full-page float moment planes or integral-image tables for this operation. The kernel
partitions output into fixed **4096-column strips**, with clipped source halos of radius `w/2`.
The strip width exceeds the maximum radius, so reflected border coordinates remain in its source
interval. Each active slot owns two uint64 column arrays: vertical sums and squared sums. Sliding
horizontal windows combine them into exact square-window statistics.

The initial reflected window is decomposed into complete periods and a remainder. A singleton or
small extent therefore repeats multiplicities, not radius-proportional work for each output sample.
Each slot reuses its arrays across its assigned strips. Source rows are immutable, output strips
do not overlap, and scratch slots are indexed work items, not identities of operating-system
threads. Numerical accumulation is independent of scheduling and worker count. Different platform
math libraries are not claimed to give universally bitwise-identical square roots at every tie.

For width `W`, window `w` and requested workers `t`:

```text
strips = ceil(W / 4096)
slots = min(t, strips)
columns = min(W, 4096 + w - 1)
stride = align_up(columns * sizeof(uint64), 64)
scratch_bytes = stride * (2 * slots)
```

The workspace shape is checked before allocation and charged through `core::Budget`. Every scratch
byte is reserved before scheduling or writing destination samples. Allocation refusal leaves the
destination unchanged. Invalid or overlapping views are rejected before access. All scratch is
refunded on return, including scheduler failure. A thread-launch/task failure may leave an internal
destination partially filled, but the host does not publish a failed operation.

One slot needs at most 128 KiB, independent of image height. The internal 64-worker API needs at
most **8 MiB**. The host's existing automatic policy caps requested workers at four and reduces
that count to fit the available charged-buffer budget; the kernel also caps slots by strip count.
The host retains its **128 MiB** image/codec budget and the decoder's input/pixel ceilings. Source,
destination, row padding and scratch all count; small metadata, C buffers and OS thread stacks do
not. This is neither a process-RSS ceiling nor arbitrary-page streaming. `--threads` and
`--memory-mib` are not exposed by this PR.

## Design review and separate QA

The design was reviewed before implementation. These challenges determined the final structure:

| Challenge | Resolution |
|---|---|
| A flat bag of optional execution fields permits invalid combinations. | Optional strings stop at admission; the effect boundary receives a validated discriminated method value. |
| Reusing float box means twice exceeds the page budget and rounds moment cancellation. | Use bounded uint64 column statistics and exact `N*Q-S*S`; keep the existing box-mean primitive unchanged. |
| Oversized windows make skinny pages expensive. | Initialize complete reflection periods with multiplicities; handle singleton extents explicitly. |
| Worker-dependent partitioning changes results or shares scratch. | Fixed output strips and slot-owned scratch; run identical reference comparisons with multiple worker counts. |
| A low-memory refusal occurs after modifying output. | Validate and reserve the complete planned scratch allocation before launching work. |
| Metadata can falsely advertise an unimplemented method. | Compare generated reviewed identities with descriptors from actual variant alternatives at compile time; dispatch is exhaustive. |
| A result adapter can mislabel a completed operation. | The port supplies only the published image; application admission supplies method identity. |
| Default-looking wrong-method options disappear. | Preserve presence and test absent, empty and explicit-default spellings separately. |
| Passing an algorithm oracle is mistaken for general restoration quality. | Test independent reference samples and one clearly synthetic illumination case; do not claim a real-document benchmark. |

Factory input uses a named parameter record rather than a positional trio of convertible numeric
types; the review rejects accidentally swapped `k`/`R` values as an avoidable API hazard.

## Verification obligations

Unit tests compare direct reflected-window sums with the rolling kernel across singleton images,
strip seams, nontrivial strides, maximum windows and different worker counts. They check parameter
extrema, equality, zero variance, exact-budget success, one-byte-short refusal and refunds. The
engine-independent `sauvola` harness compares against the direct oracle and is registered in the
same manifest as all other campaigns and ordinary corpus replays.

Real-executable tests validate both methods, selected capability discovery, all method-option
conflicts, source preservation, publication and closed JSON responses. A known synthetic text mask
on a changing background distinguishes Sauvola from global fixed thresholding; it is not evidence
of universal superiority or preservation of arbitrary handwriting, faded marks, halftones or color.
Full empirical readability/fidelity evaluation remains distinct from mathematical conformance.

## Execution cancellation

The scheduler carries the operation's cancellation capability separately from B01/B02/B03 parameters.
All binarizers observe bounded checkpoints; B02 also observes reflected initialization and scratch
admission. Cancellation can leave a partially written destination, which the host discards without
publishing. No-stop arithmetic and method versions remain unchanged. See [cancellation](cancellation.md).

B01 design and independent challenge cover empty-bin plateaus, exact byte-to-bin expansion,
global tolerance selection and one-bin endpoint polarity. The simpler 256-bin alternative was
rejected because the requested contract explicitly fixes 4,096 bins. Retained observations replace
refitting during encode/verify; serial fitting avoids worker-dependent reductions. Independent
kernel and real-executable references cover all byte samples, skewed populations, low-depth PNG
expansion, exact observations, wrong-method presence and obsolete/inconsistent records. B02/B03
references remain unchanged. Mathematical agreement is not document quality certification.
