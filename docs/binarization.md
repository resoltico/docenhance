# Typed binarization

This is the executable contract for **B02 Sauvola, method version 1**, and **B03 fixed threshold,
method version 1**. It specifies the stored-sample grayscale operation, not a color-managed or
complete restoration pipeline. Preserve the original document; binarization discards information.

## Admission and execution

The CLI retains option presence separately from its spelling. An absent option permits a default;
an explicitly empty option is invalid. `de_app` constructs `ProcessRequest` only after validating
paths and creating one closed `methods::Binarization` alternative: `Sauvola` or `FixedThreshold`.
Both parameter types have private constructors and validated factories. The raw named
`SauvolaParameters` record is factory input, never an admitted execution value.

The execution visitor has an overload for each alternative, with no generic fallback. The runtime
catalog is constructed from those same alternatives and must equal the generated reviewed catalog
at compile time. Editing a JSON status cannot create an executable capability. The generator also
rejects duplicate identities/selectors, invalid versions and unknown implemented-option references.

The processing port returns `PublishedImage`, not a method identifier supplied by an adapter.
The application attaches the identity of the admitted method to `Processed`; only the report layer
serializes it. A successful process response contains `method`, `method_version`, `output` and
`publication: completed`. `methods B02` and `methods B03` return just the selected capability.
The schema template is `spec/command-response.schema.json`; method identities come from
`spec/method-contract.json`. The delivered schema and C++ descriptors are generated together.

## Parameters

| Method | Selector | Options and defaults |
|---|---|---|
| B03 | `--output-mode bw --binarize fixed` | `--fixed-threshold 0.5`, finite in `[0,1]` |
| B02 | `--output-mode bw --binarize sauvola` | `--sauvola-window 31`, odd integer in `[3,4095]`; `--sauvola-k 0.2`, finite in `[0,1]`; `--sauvola-r 0.5`, finite in `[1/255,1]` |

Window syntax is ASCII digits only. Leading zeros are accepted; signs, whitespace, fractions,
exponents, overflow and even windows are rejected. Decimal options use the existing finite-decimal
contract. A B03 invocation rejects every explicitly present Sauvola option, and B02 rejects an
explicit fixed threshold, even when the supplied value is empty or equal to a default. There are
no ignored method-specific options, recipes, compatibility aliases or inference of binary output from private method flags. In explicit `bw` mode, an absent selector uses Sauvola.

`R` uses **normalized grayscale units**. Its default `0.5` is `127.5` in byte units, not the value
`128` used by some byte-scale implementations. The normalized positive floor prevents meaningless
near-zero denominators and keeps all intermediate thresholds finite. No implicit unit conversion
based on the magnitude of an argument is performed.

## Samples and mathematical definition

Input is the same grayscale PNG subset as B03: 1/2/4/8-bit samples without transparency, expanded
to unsigned 8-bit stored samples. PNG gamma metadata does not change the threshold samples. Color,
alpha and 16-bit input remain rejected. Output is 8-bit grayscale containing only 0 and 255.

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

The scheduler carries the operation's cancellation capability separately from B02/B03 parameters.
Both binarizers observe bounded checkpoints; B02 also observes reflected initialization and scratch
admission. Cancellation can leave a partially written destination, which the host discards without
publishing. No-stop arithmetic and method versions remain unchanged. See [cancellation](cancellation.md).
