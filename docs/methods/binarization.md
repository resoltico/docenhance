# Typed binarization

B01 global Otsu, B02 Sauvola and B03 fixed threshold are each method version 2. They classify the
terminal Q16 perceptual luminance of the common fixed pipeline. Preserve the original document;
binarization discards information and does not establish readability or original mark meaning.

## Admission and execution

The CLI retains option presence separately from spelling. An absent option permits a default;
an explicitly empty option is invalid. Application admission constructs the typed common Plan,
including an Otsu, Sauvola or FixedThreshold selection for binary output. Raw parameter records are
factory inputs rather than admitted execution values. Continuous output has no binarization selection.

Runtime discovery comes from actual implemented types and reviewed method declarations; changing
metadata cannot create a capability. `methods B01`, `methods B02` and `methods B03` return the selected
capability. Persistent requests use method/version/selector/parameters. A successful command embeds
the complete current record in `response.record`, with actual completed binarization Facts under
`record.execution.binarization`. Typed Fields own serialization and native-emitted schemas. Obsolete
record/response forms have no compatibility reader; unknown or inconsistent fields are refused.

| Method | Selector | Options and defaults |
|---|---|---|
| B01 | `--output-mode bw --binarize otsu` | No method-specific options |
| B03 | `--output-mode bw --binarize fixed` | `--fixed-threshold 0.5`, finite in `[0,1]` |
| B02 | `--output-mode bw --binarize sauvola` | `--sauvola-window 31`, odd integer in `[3,4095]`; `--sauvola-k 0.2`, finite in `[0,1]`; `--sauvola-r 0.5`, finite in `[1/255,1]` |

Window syntax is ASCII digits only. Leading zeros are accepted; signs, whitespace, fractions,
exponents, overflow and even windows are rejected. Decimal options use the strict finite-decimal
contract. Fixed rejects explicitly present Sauvola options, Sauvola rejects explicit fixed threshold,
and Otsu rejects both, including empty and explicit-default spellings. Private method options cannot
silently disappear. An absent selector in explicit `bw` mode selects Sauvola. There are no recipes,
compatibility aliases or implicit binary-output selection from private parameters.

R remains in normalized units with its reviewed floor **1/255**, even though samples are Q16.
The default 0.5 corresponds to 32767.5 in Q16 code units. It does not mean 32768, nor does the larger
sample precision change the parameter domain. No magnitude-dependent unit conversion occurs.

## Source interpretation, geometry and protection

The common PNG/JPEG/TIFF source admission, color/profile interpretation, straight-alpha policy and
precision rules apply before all photometric stages. See [PNG](../formats/png.md),
[JPEG](../formats/jpeg.md) and [TIFF](../formats/tiff.md). Metadata orientation G01 precedes exact
quarter-turn G02. Source gamma, ICC, alpha and color can therefore change the luminance entering
classification; there is no special stored-byte or metadata-ignoring binary decoder.

The fixed order is interpretation/geometry, illumination, denoising, restoration, contrast,
sharpening, then terminal binarization. Stages retain binary64 current Y. Form exactly one terminal
`Q = floor(65535*sRGB_encode(finalY)+0.5)` uint16 plane. Do not reuse a denoiser's earlier analysis Q,
serialize a stage intermediate or route decisions through a 0/255 byte plane.

A supplied protection mask retains the common original-depth/frame/opacity admission. It protects
photometric destinations while retaining their neighbors as context where specified by the method.
Every terminal pixel is still classified, including protected photometric destinations. The mask
is not an eligible-sample exclusion for Otsu, Sauvola or Fixed. Current source and mask identities,
canonical mask retention and exact geometry belong to [geometry](../pipeline.md) and [bundles](../record.md).

Binary kernels consume immutable Q16 views and write logical uint8 values **0 black / 1 white**.
The codec packs those decisions directly as physical grayscale PNG depth 1, most significant bit
first, with zero original unused low row bits and no ICC profile. Canonical output validation checks
original unfiltered packed rows before combining/expanding them. General source PNG1 admission is
separate from this canonical artifact requirement. Decode-back compares actual encoded bits to the
intended logical raster without re-running the binarizer. Read-only bundle verification establishes
artifact/record agreement, not numerical correctness, authenticity or document meaning.

## Mathematical definitions

For B01 map each terminal code p to `q = floor(4095*p/65535+0.5)` and build a 4,096-bin uint64
histogram over every pixel. Positive half ties round upward; the odd denominator has no exact half
tie for integer p. The implementation's exact integer mapping and independent floating quantizer
must agree over all 65,536 input codes. A 256-bin replacement does not implement this contract.

For each integer candidate t in 0..4094 with nonempty populations on both sides, compute binary64
`w0*w1*(mu0-mu1)^2`, with population fractions w0/w1 and mean bin coordinates mu0/mu1. Find the true
global maximum best first; then choose the smallest candidate whose score differs by at most
`1e-12*max(1,best)`. Empty-class candidates are excluded. Updating an approximate best during a
single tie-selecting pass can create order-dependent chains and is not permitted.

If exactly one bin is occupied, use threshold 2047 and set `single_bin_fallback=true`; otherwise
false. Black is exactly `q <= threshold_bin`, white otherwise. A true fallback requires 2047.
For an sRGB byte source embedded as Q=257*byte, flat 0..127 is black and 128..255 white; this is a
fixture embedding property rather than a production byte bridge. Otsu is global, not spatially adaptive.
Facts retain the selected threshold, fallback and complete black/white counts.

For B03 black is exactly `double(p)/65535 <= threshold`. Equality is black. Threshold 0 admits
only code 0 as black; threshold 1 makes every pixel black. Q16 codes 32767 and 32768 fall on opposite
sides of the default 0.5. The normalized division is not replaced by rounding an integer threshold.

For B02 form a centered w-by-w square with REFLECT_101 independently on both axes. For extent e>1,
reduce a coordinate modulo `2*(e-1)` to nonnegative r; the reflected coordinate is r if r<e,
otherwise `2*(e-1)-r`. Extent 1 always maps to 0. Reflection does not duplicate seam samples, and a
window may exceed either image extent. Let N=w*w, S=sum(p), P=sum(p*p) over that population:

```text
V = N*P - S*S                         # exact unsigned wide-integer subtraction
n = double(N)
m = double(S) / n
s = sqrt(round_binary64(V)) / n
T = m * (1 + k * (s / (65535*R) - 1))
logical_output = 0 if double(p) <= T else 1
```

The order shown is literal binary64 evaluation after exact variance formation. S and P fit uint64
because `65535²*4095² < 2^64`; products N*P and S*S can need 80 bits. Two uint64 limbs preserve the
full products and subtraction before one nearest-even conversion of V to binary64. Converting each
product first and subtracting rounded doubles can destroy a small nonzero variance. The wide owner
retains carry/borrow and guard/sticky behavior; impossible negative subtraction is an invariant
refusal, not a variance repair. No epsilon or floating variance clamp conceals an error.

This is population variance, with denominator N rather than N-1. Fast-math and fused reassociation
are disabled. The final threshold is not rounded to a byte, clipped or adjusted by a visual heuristic.
Equality is black: k=0 is a local-mean test and a constant zero input remains black. A positive
constant is white when the literal binary64 threshold is lower; tiny k=1e-20 rounds the multiplier
to 1 and leaves it black, whereas ordinary k=0.2 makes it white.

The local formula follows Sauvola and Pietikäinen, *Adaptive document image binarization*,
Pattern Recognition 33(2), 225–236 (2000),
[DOI 10.1016/S0031-3203(99)00055-2](https://doi.org/10.1016/S0031-3203(99)00055-2).
The implementation is independently written from that mathematical definition. It implements the
local text threshold, not every classification or post-processing procedure in the paper. Normalized
R, reflection, wide integer statistics and literal equality are the explicit DocEnhance contract.

## Memory, scheduling and completion

All output modes share the [selected processing ledger](../architecture.md#values-ownership-and-budgets),
40-million-pixel ceiling and codec/source/asset bounds. Original interpreted RGB, current Y, protection, encoded snapshots and output lifetimes
are accounted at the actual pipeline owners. Terminal Q is allocated while final Y is live; Y then
refunds before the logical output is allocated. This is finite in-memory work without disk spill,
not arbitrary-page streaming or an RSS limit. Padding can make a valid aspect ratio exceed budget.

Classification owns an aligned two-row uint64 count plane, allocated before any output store.
Fixed and Otsu have one column per 64-row band; Sauvola has one per 4096-column strip. Every completed
partition must contain exactly its expected logical pixels before counts combine in increasing
partition order. Facts require black+white=W*H, disposition applied and reason none. A partially
written or interrupted operation does not return completed Facts. Actual earlier photometric Facts
survive a later classification failure under the [bundle contract](../record.md).

Otsu allocates its 32,768-byte histogram and count plane simultaneously before histogram work or
output stores. It fits serially in row-major order and releases the histogram before parallel
classification; scores need no separately allocated scan workspace. Fixed allocates only the count
plane. Histogram moments stay within the admitted source population; a smaller histogram changes
the specified 4096-bin operation.

Sauvola uses fixed 4096-column output strips, with clipped source halos of radius w/2. Two uint64
column rows per active slot hold vertical sums and squares. Sliding horizontal windows then form
exact square-window moments. Complete reflected periods are represented by multiplicities, so tiny
or singleton images do not re-enumerate a huge window for every output sample. Slot-owned scratch
is indexed by work item rather than OS thread identity; slots reuse rows across assigned strips.
Source views stay immutable and output strips are disjoint. Numerical order is worker-independent;
platform math libraries are not promised universally identical square roots at every possible tie.

For A(x)=align_up(x,64), image W/H, window w and scheduler workers t, exact additional payload is:

```text
row_parts = ceil(H/64)
row_counts = 2*A(8*row_parts)
Fixed scratch = row_counts
Otsu scratch = 32768 + row_counts
strips = ceil(W/4096)
slots = min(t,strips)
columns = min(W,4096+w-1)
Sauvola moments = 2*slots*A(8*columns)
Sauvola counts = 2*A(8*strips)
Sauvola scratch = moments + counts
```

The two-row count plane is part of the simultaneous reservation, not an observation allocated after
output writes. One moment slot needs at most 128 KiB; simultaneous slots follow the worker/strip
formula above, in addition to the count plane. The host resolves the selected execution worker bound;
see [indexed execution](../architecture.md#numerical-work-and-scheduling). The common host does not change the algorithm to fit a short budget: actual allocation
refuses E_RESOURCE. Complete scratch allocation precedes scheduling/output, so a one-byte-short
refusal leaves the destination untouched. Invalid geometry and full-backing overlap refuse before
access. All charged scratch refunds on success, cancellation or worker failure. An internal partial
destination after execution failure is discarded and never published. Worker and memory controls
belong to execution and do not alter method parameters.

## Independent reference definitions

Independent direct reflected windows cover Q16 full range, singleton axes, nontrivial strides,
strip seams, ordinary/tiny-k equality and parameter extrema. The maximum-window CLI constant tests
exercise large-product cancellation to zero and literal equality; they do **not** establish nonzero
wide-variance conversion. Distinct concrete controls preserve the stronger questions:

- `tests/support/binary_wide_cases.hpp` checks carry/borrow and independently authored nonzero-high-
  limb even/odd/guard/sticky conversion literals, including cross-limb and significand-carry cases.
- `tests/unit/binary_maximum.cpp` exercises the actual 4095² population with one 65534 sample among
  65535 values, exact V=N-1 and a chosen threshold that distinguishes rounded-product wrong-white.
- `tests/unit/binary_numerical.cpp` and `binary_large_window_reference.hpp` compare actual maximum-
  window seams/borders with separately enumerated two-level multiplicities and both decisions.
  Their variance is nonzero but fits the low limb; it is separate from high-limb conversion tests.
- `tests/unit/binary_resources.cpp` checks independent simultaneous scratch formulas, exact capacity,
  one-byte-short destination preservation and refunds. Worker/cancellation controls attain actual
  1/2/7 participation, partial stores, untouched suffixes and complete-source/padding preservation.

These are authored or retained control owners, not a claim that current source passes execution.
Independent CLI references also cover sub-byte Q16 distinctions, Otsu all-code/bin/tie/fallback,
Fixed endpoints/equality, original source embeddings, current record refusals, strict option
presence, source preservation and actual bundle round trips. Physical-bit tests inspect packed
rows, CRC/zlib framing and original tails independently of the producer. Fuzz targets retain
bounded direct references and all original regression bytes; engine qualification is separate.

## Cancellation and quality limits

The scheduler carries cancellation separately from method parameters. Otsu observes allocation,
histogram initialization/gathering, fixed score scans and classification; Sauvola also observes
reflected initialization and moment work. Bounded checkpoints and joined workers preserve real-error
precedence, actual completed prefixes and the final publication cutoff. Cancellation may leave an
internal partial raster, which the host discards. See [cancellation](../architecture.md#cooperative-cancellation).

A known synthetic text mask on an uneven background distinguishes local thresholding from global
Fixed. This is mathematical/synthetic conformance evidence, not a claim of universal superiority or
preservation of arbitrary handwriting, faint marks, halftones or color. Held-out empirical readability,
fidelity, harms, uncertainty and abstention remain separate under [document quality](../document-quality.md).
