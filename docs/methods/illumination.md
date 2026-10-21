# Illumination

## I01 surface illumination

This is the executable contract for **I01 quantile log-surface illumination, method version 1**.
It is an optional photometric operation, not paper recognition, recovery of lost writing,
a preset, or a guarantee that document content is preserved. Preserve the source document.
Representation and B02/B03 remain separately specified in [PNG processing](../formats/png.md)
and [binarization](binarization.md).

### I01: Admission

```sh
docenhance process input.png --out-dir result --illumination surface
docenhance process input.png --out-dir result --illumination auto --protect-mask protect.png
```

`--illumination off` is the default. `surface` attempts I01 explicitly. `auto` applies the same
method only after the predicates below pass. Both work before continuous `preserve`/`gray` output
or terminal `bw` classification in the common fixed plan. Protection bypasses photometric changes;
the selected binarizer still classifies every pixel. Explicit I02 is a separate alternative; see [morphological illumination](#i02-morphological-illumination). Auto remains I01 only.

| Option | Default | Domain |
|---|---|---|
| `--background-strength` | 1 | finite [0,1] |
| `--background-max-gain` | 2 | finite [1,4] |
| `--background-target` | source | source or finite [0.1,1] |
| `--background-cell` | auto | auto or integer [8,512] |
| `--background-quantile` | 0.90 | finite [0.75,0.99] |
| `--background-smooth` | 1 | finite [0.1,20] |
| `--protect-mask` | absent | nonempty well-formed UTF-8 path, no embedded NUL |

The defaults correct the fitted background completely (`strength` 1) up to a doubling of any
sample (`max-gain` 2), with moderate smoothing. On synthetic shaded, book-gutter and photograph
pages they reduced paper-luminance variation by 68–86% while retaining at least 99.7% of local mark
contrast; a conservative 0.35/1.5/2 setting reduced it by only 19–30%. A solid black block is
brightened only by the local illumination correction. A photograph brighter than a quarter of the
paper reference is treated as shaded paper and can be brightened up to the gain cap; protect it
with a mask. These synthetic measurements are not a real-document benchmark.

Numeric spelling uses the existing finite-decimal grammar; cell integers use ASCII digits only.
Missing values may default; explicitly empty or wrong-operation values fail before input I/O.
Background parameters with `off` are errors. All parameters are validated even when strength is zero
or maximum gain is one. Application admission creates a privately constructed `Surface` value;
raw option strings do not control pixel execution. `IlluminationOff` is a separate alternative.

### I01: Working samples and protection

Interpret source color, apply exact metadata orientation and composite transparency under the
existing codec policy, apply exact G02 rotation, and retain the interpreted original RGB plus a
finite binary64 linear-Y plane. For color, `Y = 0.2126 R + 0.7152 G + 0.0722 B`; interpreted gray
retains its scalar directly. I01 modifies only Y. Continuous output transports original RGB to the
final Y once, then encodes/quantizes. Binary output forms terminal Q16 once and classifies it.
No stage passes through an intermediate integer-output image.

The protection mask is in the **metadata-oriented source coordinate frame B**, with identical width
and height. It must be static original-depth 1/8-bit grayscale PNG; 8-bit gray-alpha or grayscale
`tRNS` is accepted only if every decoded alpha value is opaque. Palette, RGB, 2/4/16-bit samples,
nonopaque transparency and non-normal mask orientation are rejected. The original IHDR depth is
checked before expansion. Semantic mask color profiles are ignored, not used to change its values;
container framing, CRCs and bounded metadata still use the production PNG validation path.
Nonzero means protected. Decode to a charged byte mask, retaining neither arbitrary metadata nor
an independently oriented mask. G02 gathers the mask identically into C for processing;
the canonical bundle asset stays in B. See [geometry](../pipeline.md) for all frames. A supplied mask is validated even with `off` or an algebraic no-op.

Protected samples are excluded from fitting and applicability statistics. Application bypasses
them exactly at the linear photometric boundary. This does not promise unchanged source bytes,
original color-space values, alpha, or final samples after an explicitly requested representation
change. In the same continuous representation path, protected output must equal the no-filter
baseline; binary output classifies the retained protected Y under the selected binarizer.

### I01: Measurement and fitted field

For dimensions W,H, auto cell size is `clamp(floor(min(W,H)/24 + 0.5),16,256)`.
Make clipped square cells anchored at (0,0). Refuse a grid exceeding 65,536 cells; do not silently
change cell size. Global eligibility requires at least `min(16,W*H)` unprotected samples.
No eligible samples is a defined no-change operation.

A cell of actual area A is measured only when its unprotected count n is at least
`max(min(16,A),ceil(A/4))`. Use all its eligible luminances, not a subsampling shortcut. The
nearest-rank quantile is the sorted element at `max(ceil(q*n)-1,0)`. A quantile below 0.02 is
unmeasured. The **background reference** R is the nearest-rank 90th percentile of the remaining
cell quantiles. A cell with `log(q_i) < log(R) - log(4)` is also unmeasured and counted as a
**dark cell**: it is darker than any admissible gain (at most 4) could correct, so it is dark
content such as a solid fill or dark photograph rather than evidence of illumination. The fitted
field spans it from surrounding paper, so dark content receives the local illumination correction
instead of the gain cap. Otherwise `b_i = log(q_i)` and `w_i = n_i/A_i`; missing cells use
`w_i=b_i=0`.
Require measured-cell coverage of at least 25% in explicit mode and 60% in automatic mode.
Explicit insufficiency returns `E_METHOD_INAPPLICABLE`; automatic insufficiency records a skip.

Fit the four-neighbor rectangular-grid system

```text
(diag(w) + beta*L) z = diag(w) b
L z at cell i = sum(z_i - z_j) over existing grid neighbors j
```

There are no wrapped neighbors, ghost edges, or penalties outside the actual grid. This connected
Laplacian plus at least one positive data weight is positive definite. Use matrix-free, binary64
conjugate gradients, starting with the weighted mean of measured logarithms, preconditioned by one
symmetric multigrid V-cycle per iteration:

- Levels aggregate 2 x 2 cells (clipped at odd edges) until one cell remains; a 1 x 65,536 grid
  has the most, 17. Coarse operators are the exact Galerkin products `P^T A P` for piecewise-constant
  `P`, which keep the weighted four-neighbor form: coarse data weights and edge weights are sums of
  the fine ones, and edges inside an aggregate vanish.
- Each level applies two damped-Jacobi sweeps (damping 0.8) before restriction and two after
  prolongation; the coarse correction is scaled by 1.6. The single-cell level is solved exactly.

Jacobi preconditioning alone needs iterations proportional to the width of an unmeasured region:
a 200-cell protected square took 471 iterations and a 1 x 65,536 grid did not converge in 20,000.
With the V-cycle these took 18–30 iterations in two dimensions and under 100 in one dimension,
even at the largest smoothing. Symmetry and positive definiteness are checked by factorizing the
cycle's dense matrix on small grids. A scale in (0,2) keeps an exact two-level correction
non-expansive in the energy norm; dense checks of the whole cycle cover scales from 1 to 2, and
1.6 conditioned best.

Reductions have fixed row-major order. Recompute the actual `rhs-A*z` residual after every step;
do not certify convergence from the recurrence residual alone. The stopping rule is
`norm(rhs-A*z) <= 1e-8 * max(norm(rhs),1e-12)`, with at most 500 iterations. Zero initial residual
needs no iteration. Nonfinite arithmetic, invalid curvature, or failure to meet the bound is
`E_NUMERICAL`, not automatic inapplicability and not permission to change methods.

Interpolate **the fitted log field** bilinearly between actual clipped-cell centers
`first + (actual_extent-1)/2`; extend the boundary-center value outside the center interval.
The background is `B=exp(interpolated_z)`, not interpolation of the original cell quantiles.
A singleton grid axis is constant. Positive finite background is required.

### I01: Applicability and target

Measure a deterministic row-major lattice at x/y multiples of
`stride=max(1,ceil(max(W,H)/1024))`, excluding protected pixels, up to 1,048,576 values.
If this lattice contains no eligible sample, use the first eligible row-major values at stride one,
up to the same cap. Report both the nominal stride and whether fallback was used. The fit still
uses every eligible cell sample; this bounded lattice is only for global statistics and target.

Use nearest-rank B10/B50/B90 and Y90 over this set. A source-relative target is B90; an explicitly
requested numeric target overrides it. Automatic application requires all six predicates:

| Predicate | Requirement |
|---|---|
| Measured-cell coverage | at least 0.60 |
| Bright-sample quantile | Y90 at least 0.35 |
| Median background | B50 at least 0.20 |
| Background variation | `(B90-B10)/max(B90,0.02)` at least 0.08 |
| Paper-like fraction | fraction `Y >= 0.8*B` at least 0.55 |
| Dark fraction | fraction `Y < 0.75*B` in [0.001,0.40] |

These are deterministic eligibility heuristics, not confidence scores. Large illustrations,
dark paper, solid fills and sparse backgrounds can violate the model assumption. Record failed
predicates; an unevaluated predicate is null, never a successful test. Explicit mode reports the
same measurements but does not require the automatic predicates. It still requires valid data,
coverage and a converged numerical solution.

### I01: Application and no-ops

```text
g = clamp(target / max(B,0.02), 1, max_gain)
raw_target = Y * pow(g,strength)
T = clamp(raw_target,0,1)
```

This version never darkens samples. The final continuous output uses neutral-axis luminance transport: when
brightening, `RGB' = RGB + (T-Y)/(1-Y) * (1-RGB)`; an unchanged target bypasses transport entirely.
The endpoint and darkening definitions remain in the scalar numerical primitive. Brightening can
reduce saturation; this is not an exact-chroma-preservation claim. There is no additional clipping,
epsilon, sharpen, threshold, mask dilation or visual heuristic inside I01.

Disabled illumination has no selected method or completed Facts. Zero strength, unit maximum gain,
all-protected input and automatic skips retain entering Y exactly without an intermediate encode/decode
approximation. Algebraic bypasses do not allocate a fitted field. Automatic skips may have completed
fitting and applicability measurements. All-zero/dark data that lacks measured cells is an explicit
method error or automatic skip, rather than a fabricated fit.

Fit once and retain immutable log coefficients through measurement and the joined final map. I01
completes eagerly before D01 or any later stage starts; the fitted coefficients are released after
application. Encoding and decode-back comparison consume the final expected output samples without
refitting or reapplying illumination. Verification cannot change the target, automatic decision or
stage/color observations. Source and mask remain read-only.

### I01: Memory, cancellation and failure

The common pipeline uses the [selected processing ledger](../architecture.md#values-ownership-and-budgets).
Plane allocations charge actual
aligned row bytes and refund ownership on all returns. At most 65,536 retained log coefficients
cost 512 KiB; seven solver rows plus those coefficients cost at most 4 MiB. The multigrid hierarchy
owns six aligned rows over G, the sum of actual ceil-halved grid level sizes. For legal grids G is
at most 131,071 and this hierarchy costs at most 6 MiB; odd level chains can exceed twice their own
finest-cell count. Two global lattice sample rows cost at most 16 MiB. These phase workspaces do not
all remain live simultaneously. The common source snapshots, original interpreted RGB, current Y,
protection and codec/profile owners share the same ledger; pixel count alone does not prove that
every aspect ratio fits. I01 adds no full-page background plane.

The direct linear-Y Working core owns a fixed cell-task sorter bank: each of at most 64
extent-defined tasks has an exclusive aligned row of up to 512² doubles (at most 128 MiB
for the bank). It releases that bank before the serial PCG/V-cycle task, and releases solver
work/hierarchy before the two global sample rows. Only immutable log coefficients survive
into the joined final map; no RGB transfer or full-page background plane is added. Its
additional charged peak is max(W7+L+Q, W7+L+Hmg, L+2S), using actual aligned sizes for
seven work rows W7, retained log row L, cell bank Q, hierarchy Hmg and one sample row S.
These are conservative owned payload bounds, not minimum-memory or RSS limits.
Finite fitted coefficients do not certify Unit background: the shared raw interpolation/exp
primitive is total, while actual sample/map producers admit new numerical background outputs
once at their consuming row boundary. Gain consumption retains the finite-positive then Unit
background/target refusal order, without a repair clamp or per-point Result primitive.


Reserve phase scratch before performing that phase. Resource refusal must not reduce density,
precision, cell resolution or coverage. Fitting happens before staging publication. Partial row
application may exist internally after failure, but the host never publishes it. Core failures use
expected results; numerical methods do not own files, process state, native color types or catches.

Bounded measurement, solving and application checkpoints share the existing execution cancellation
capability. Dense scalar reductions are bounded by the admitted grid; nearest-rank selection sorts
caller-owned scratch in place with O(n log n) worst-case comparisons, avoiding adversarial quadratic
partitioning. These are work bounds, not a universal millisecond deadline.
The pure pipeline encodes and independently compares before the filesystem publisher stages owned
bytes, flushes/closes and commits exclusively. The final precommit cutoff,
unknown publication and cleanup semantics are unchanged; no automatic retry or rollback is added.

### I01: Typed diagnostics and outcome integrity

An actual selected I01 stage returns completed SurfaceFacts. Disabled illumination remains null.
The typed request retains method identity/version and requested parameters. Facts retain resolved
cell size, eligible/protected/evaluated/changed counts, coverage, dark cells and background reference
(null when no cell was measured), solver iterations/true residual/tolerance, global measurements,
six optional predicate decisions and gain/saturation counters. Gain extrema refer to g before the
strength exponent; capped samples include equality at max_gain; saturation counts raw target above
one. No completed field is invented for an unstarted or failed operation.

The [bundle contract](../record.md#completed-facts-and-failures) separates completed numerical Facts,
the optional failed family and publication. A completed active stage is `applied` when working
samples changed, otherwise `no_change`; an automatic decision not to apply is `skipped`. Later stage,
verification or publication failure preserves completed I01 Facts. An I01 failure has no completed
I01 value. Asset acquisition happens before numerical rendering, so a missing source/mask/PSF can
fail without any completed numerical stage. Failure responses use a sparse map of actual completed
Facts; successful response.record contains the full persistent record with nullable selections.
Stage producers, host record construction and wire validators own consistency checks. Dispatch
forwards the processor's reported outcome; its preallocated unknown-publication fallback is used
only when an exception escapes the processor call.

Generic core errors do not depend on I01. `E_METHOD_INAPPLICABLE` and `E_NUMERICAL` use processing
exit 4; delivery and publication rules remain separate. CLI text summarizes actual Facts, and JSON
uses the generated closed schema. Warnings derive at the response boundary rather than being stored
inside persistent stage Facts. Typed method families prevent I01 from masquerading as a binarizer.

### I01: Mathematical and ownership constraints

The implementation retains these mathematical and ownership constraints:

| Boundary | Contract property |
|---|---|
| Quantized row API makes enhancement operate on encoded integers. | Retain interpreted original RGB and mutable binary64 Y with one downstream quantizer. |
| ICC or transparency handling differs between measurement and output. | Interpret once before stage execution; retain the actual interpretation Facts. |
| Expanded mask bytes conceal an unsupported original depth. | Validate original-depth admission before shared decoding and normalization. |
| Missing cells create a singular or incorrectly connected operator. | Explicit positive-weight coverage and an independently assembled tiny-grid matrix. |
| A recurrence residual falsely declares convergence. | Check the actual residual; finite iteration refusal remains a numerical error. |
| Verification silently changes the fitted model or counters. | Immutable fitted coefficients through one joined map; compare final samples without rerunning I01. |
| Low memory changes the algorithm. | Charged phase scratch, refusal/refund and exact-budget boundary tests. |
| A good-looking background erases marks. | Protected identities, already-good no-op and independent synthetic contrast measurements. |
| Fault after a completed stage falsely labels its numerical work as failed. | Stage completion is distinct from command publication and delivery. |
| Solid fills or dark photographs measured as background receive the full gain cap. | Cells below the background reference divided by the largest admissible gain are dark content, left unmeasured, spanned from surrounding paper and reported. |
| Solver iterations grow with the width of unmeasured regions, refusing admissible grids. | Multigrid-preconditioned CG; symmetric positive definite checks and bounded iteration counts on the widest admitted gaps. |
| Conservative defaults leave most shading in place. | Full-strength defaults chosen from measured fixtures; the gain cap bounds the change to any sample. |

Unit and raw-mask/model fuzz tests exercise independent dense solves and interpolation, no-ops,
partial cells, missing measurements, bounds, failure, resources, protection and cancellation.
Real-executable tests independently decode PNG samples and compare against a Python dense solve,
all orientation cases and linear color-transport references. Clearly synthetic P01/P02 fixtures
require an already-good automatic skip, at least 40% reduction of background coefficient of
variation, and at least 75% retention of the defined local normalized mark contrast. These are
acceptance tests, not evidence of performance on arbitrary documents or handwriting. The
aggregate watchdog and native process bound follow [native verification](../quality.md); individual
90-second CLI contract limits and all numerical/failure gates remain intact. Log exact command results
and platforms separately; CI or benchmark success is not implied by these obligations.

### I01: References

The matrix solver is independently written; algorithmic background is the SIAM/Netlib
*Templates for the Solution of Linear Systems*, https://www.netlib.org/templates/templates.html,
U. Trottenberg, C. W. Oosterlee and A. Schüller, *Multigrid* (Academic Press, 2001), and
D. Braess, "Towards algebraic multigrid for elliptic problems of second order", *Computing* 55
(1995), for over-scaled piecewise-constant coarse corrections.
Color interpretation and scalar luminance transport retain the reviewed project definitions.

## I02 morphological illumination

This records the reviewed I02 design. Capability admission requires the implementation and
verification below. I02 is an explicit alternative to I01, never an automatic fallback.

### I02: Contract and integration

`--illumination morph` selects I02 before continuous preserve/gray output or terminal binary
classification, using the common PNG/JPEG/single-page TIFF admission. `auto` retains I01 exclusively.
Shared strength, gain and target defaults retain the current repository values (1, 2 and source). Morph alone accepts
`--background-radius auto|1..256`; auto resolves half-up rounding of min(W,H)/50 clamped to [8,128].
Surface/auto reject radius presence; morph rejects cell, quantile and smooth presence. Off rejects
background options. Validate all parameters and the existing oriented protection mask even for zero strength or unit gain.

Interpretation/orientation/alpha complete at the common codec/color boundary before stages. Analysis
uses the finite binary64 current Y plane; interpreted original RGB remains available for final
transport. Protected samples are excluded from statistics and temporarily filled with eligible
luminance Y90, never inpainted in output. Analysis does not count application evaluations. Invalid
working samples are refused. No eligible pixels is a no-change operation;
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
Prepare once and retain an immutable scalar field through the joined map onto current Y. I02
completes eagerly before D01; its field is released after application. Continuous output transports
retained original RGB once to final Y. Binary output quantizes/classifies every pixel, including
protected photometric destinations. Encoding and decode-back comparison consume final samples
without rerunning I02. Real-error precedence and the final publication cutoff remain unchanged.

Typed illumination and requested-parameter alternatives distinguish Surface from Morphology.
The request retains method identity/version and parameters. I02 completed Facts contain disposition,
reason, eligible/protected/evaluated/changed counts, resolved radius, sigma/support, nullable analysis
fill/sample count/stride/fallback/background quantiles/range/target and gain/saturation counters.
Algebraic bypasses have no analysis or evaluations. Allocation totals remain execution diagnostics,
not persistent method parameters. Native method-owned validation rejects foreign I01 fields,
impossible geometry and invalid scalar/count relationships.

The [bundle contract](../record.md#completed-facts-and-failures) separates actual completed Facts from
the failed family and publication. A later failure preserves fully completed I02 Facts. An I02
failure does not invent a partial completed value. Asset byte acquisition happens before rendering;
a missing PSF therefore has no completed numerical prefix, whereas a malformed acquired PSF decoded
at restoration preserves the eager I02 prefix. Current response and record forms reject obsolete
fields without compatibility readers. Method versions come from the reviewed method contract.

### I02: Chosen implementation and resource contract

First-party binary64 separable kernels implement the specified equations. Illumination and D01
use the existing charged first-party workspaces. I02 adds no package, layer
edge or exception boundary.

Whole-page fields avoid composed-halo ambiguity. Two charged float64 planes ping-pong through
horizontal/vertical dilation, erosion and Gaussian smoothing. A monotone ring deque uses at most
2r+1 indices; Gaussian support uses at most 769 coefficients. Global sample scratch is at most
8 MiB. Release sample scratch before field preparation and the second field before background
statistics. Retain one field through the joined application, then release it before D01. Charge
actual aligned plane/storage bytes; report peak shared-ledger charge, not RSS. The
[selected processing ledger](../architecture.md#values-ownership-and-budgets) includes encoded
sources/assets, original interpreted RGB, current Y, mask and codec/profile
owners together with each active stage workspace. Refuse
insufficient memory without reducing radius, precision, density or image dimensions. Large valid
pages can exceed the working budget. Check cancellation inside transfers, extrema/deque work,
Gaussian output pixels (each dot product has at most 769 coefficients), measurement blocks and application,
with fixed bounded observation intervals.

### I02: Reference models and limits

Independent square-neighborhood references must cover degenerate axes, small images and radii exceeding image dimensions, with exact constant preservation. Gaussian references must cover maximal radius/support and exact constant-one results. Closing is not opening; negative/broad marks are not promised to disappear. A tiled alternative would require dependency
radius 2r+ceil(3*sigma) for the complete composition, so a single-operation halo is insufficient.

The challenge requires source/mask immutability, protected analysis fill rather than output fill,
sparse-mask lattice fallback, no NaN masking, method-specific option presence and report identity,
no automatic I02 selection, ownership refunds on every failure and final-sample comparison without
method replay.
Independent numerical tests must compare slow two-dimensional extrema/Gaussian references against
production kernels, including degenerate axes, maximal radius, borders, constant/ramp/impulse and
wide-mark cases. Real executable references cover grayscale/color, precision, masks/orientation,
I01/I02/D01 composition, no-ops, diagnostics, records and refusals. Raw method/CLI fuzzing and real
compiler, sanitizer, native package and exact-commit CI workflows are required before merge.
