# Bounded illumination correction and protected regions

This is the executable contract for **I01 quantile log-surface illumination, method version 1**.
It is an optional continuous-tone operation, not paper recognition, recovery of lost writing,
a preset, or a guarantee that document content is preserved. Preserve the source document.
Representation and B02/B03 remain separately specified in [PNG processing](png-processing.md)
and [binarization](binarization.md).

## Admission

```sh
docenhance process input.png --out-dir result --illumination surface
docenhance process input.png --out-dir result --illumination auto --protect-mask protect.png
```

`--illumination off` is the default. `surface` attempts I01 explicitly. `auto` applies the same
method only after the predicates below pass. Both work with continuous `preserve` or `gray` output.
All illumination/protection options are rejected with `bw`; there is no implicit change to binary
sample meaning. No I02 fallback or automatic preset is introduced.

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

## Working samples and protection

Interpret source color, apply exact metadata orientation and composite transparency under the
existing PNG policy, then expose finite linear-light RGB through `image::LinearSource`. I01 operates
on relative linear luminance `Y = 0.2126 R + 0.7152 G + 0.0722 B`. Final grayscale conversion,
sRGB encoding and integer quantization occur afterward. `image::quantize_linear` is shared by
filtered and no-filter output, avoiding an intermediate integer-output round trip.

The protection mask is in the **already-oriented source coordinate frame**, with identical width
and height. It must be static original-depth 1/8-bit grayscale PNG; 8-bit gray-alpha or grayscale
`tRNS` is accepted only if every decoded alpha value is opaque. Palette, RGB, 2/4/16-bit samples,
nonopaque transparency and non-normal mask orientation are rejected. The original IHDR depth is
checked before expansion. Semantic mask color profiles are ignored, not used to change its values;
container framing, CRCs and bounded metadata still use the production PNG validation path.
Nonzero means protected. Decode to a charged byte mask, retaining neither arbitrary metadata nor
an independently rotated mask. A supplied mask is validated even with `off` or an algebraic no-op.

Protected samples are excluded from fitting and applicability statistics. Application bypasses
them exactly at the linear photometric boundary. This does not promise unchanged source bytes,
original color-space values, alpha, or final samples after an explicitly requested representation
change. In the same representation path, protected output must equal the no-filter baseline.

## Measurement and fitted field

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

## Applicability and target

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

## Application and no-ops

```text
g = clamp(target / max(B,0.02), 1, max_gain)
raw_target = Y * pow(g,strength)
T = clamp(raw_target,0,1)
```

This version never darkens samples. Use the existing neutral-axis luminance transport: when
brightening, `RGB' = RGB + (T-Y)/(1-Y) * (1-RGB)`; an unchanged target bypasses transport entirely.
The endpoint and darkening definitions remain in the scalar numerical primitive. Brightening can
reduce saturation; this is not an exact-chroma-preservation claim. There is no additional clipping,
epsilon, sharpen, threshold, mask dilation or visual heuristic inside I01.

Disabled illumination, zero strength, unit maximum gain, all-protected input and automatic skips
use the existing no-filter row producer. They do not allocate a full fitted field unnecessarily
or introduce an encode/decode approximation to implement a no-op. All-zero/dark data that lacks
measured cells is an explicit method error, or an automatic skip, rather than a fabricated fit.

Fit once, retain an immutable model, and apply to bounded linear blocks. Encoding and independent
output verification use the **same model**; verification cannot refit, choose a different target,
change automatic decisions or double-count stage/color observations. Source and mask remain read-only.

## Memory, cancellation and failure

The existing continuous 1 GiB charged-buffer ceiling remains unchanged. Plane allocations charge
actual aligned row bytes, with normal ownership refunds on all returns. Retained log coefficients
cost at most 512 KiB; seven solver vectors plus those coefficients cost at most 4 MiB. The multigrid
hierarchy adds six vectors over at most twice the cell count, at most 6 MiB. Cell-selection
scratch is at most 2 MiB plus one at-most-512-pixel RGB row. Global lattice storage is at most 16 MiB,
plus a 96 KiB linear block. These phases do not hold their transient workspaces simultaneously.
The optional retained mask costs one byte per sample plus row padding; decoding also accounts for
its immutable encoded snapshot and temporary decoded raster. No full-page floating RGB frame or
per-pixel background frame is allocated. Limits include the original codec/profile/source lifetimes;
pixel count alone does not prove that every aspect ratio fits.

Reserve phase scratch before performing that phase. Resource refusal must not reduce density,
precision, cell resolution or coverage. Fitting happens before staging publication. Partial row
application may exist internally after failure, but the host never publishes it. Core failures use
expected results; numerical methods do not own files, process state, native color types or catches.

Bounded measurement, solving and application checkpoints share the existing execution cancellation
capability. Dense scalar reductions are bounded by the admitted grid; a checked selection call is
bounded by its admitted scratch size. These are work bounds, not a universal millisecond deadline.
The publisher still encodes, verifies, closes and commits exclusively. The final precommit cutoff,
unknown publication and cleanup semantics are unchanged; no automatic retry or rollback is added.

## Typed diagnostics and outcome integrity

Continuous success requires an `illumination` record, including `disabled` for the default path.
A requested I01 stage carries method identity/version, requested parameters, resolved cell size,
eligible/protected counts, measured coverage, dark cells and the background reference (null when no
cell was measured), solver iterations/true residual/tolerance, global
measurements, target, six optional predicate decisions and application counters. Gain extrema refer
to g before the strength exponent; capped samples include equality at max_gain; saturation counts
raw target above one. Fractions with no evaluated samples are null.

`complete` means the illumination stage finished, **not** that output was committed. Encoding's
last generated row completes the stage; a subsequent verification/publication failure retains the
completed stage record alongside the actual command error. An interrupted or failed incomplete
stage is `failed`, without erasing prior diagnostics. A completed stage is `applied` only when some
working samples changed, otherwise `no_change`; an automatic decision not to apply is `skipped`.
The application rejects a successful adapter result with a missing/incomplete or mismatched stage
record as unknown publication, since effects may already have happened. It never invents success.

`ProcessFailure` carries optional method diagnostics at the application boundary; generic core
errors do not depend on I01. `E_METHOD_INAPPLICABLE` and `E_NUMERICAL` use processing exit 4. Delivery
and publication failure rules remain separate. CLI text summarizes the stage; JSON retains all
measurements under the generated closed schema. Schema method families distinguish illumination
from completed binary methods, so I01 cannot masquerade as a binarizer.

## Design QA and verification obligations

Design and a separate QA pass preceded implementation. The review rejected these shortcuts:

| Risk | Required resolution |
|---|---|
| Quantized row API makes enhancement operate on encoded integers. | Expose interpreted linear blocks and share one downstream quantizer. |
| ICC or transparency handling differs between measurement and output. | Reuse the same converter; only output traversals count color diagnostics. |
| Expanded mask bytes conceal an unsupported original depth. | Validate original-depth admission before shared decoding and normalization. |
| Missing cells create a singular or incorrectly connected operator. | Explicit positive-weight coverage and an independently assembled tiny-grid matrix. |
| A recurrence residual falsely declares convergence. | Check the actual residual; finite iteration refusal remains a numerical error. |
| Verification silently changes the fitted model or counters. | Immutable fitted coefficients and independent observation copies during replay. |
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
acceptance tests, not evidence of performance on arbitrary documents or handwriting. The aggregate
native-suite limit is 600 seconds to accommodate the expanded compiler/header checks; individual
90-second CLI contract limits and all numerical/failure gates remain intact. Log exact command results
and platforms separately; CI or benchmark success is not implied by these obligations.

## References

The supplied DocEnhance implementation blueprint (September 17, 2026), I01 and protection/photometric
contracts, informs the method. Its planned presets and unrelated features are not reinstated.
The matrix solver is independently written; algorithmic background is the SIAM/Netlib
*Templates for the Solution of Linear Systems*, https://www.netlib.org/templates/templates.html,
U. Trottenberg, C. W. Oosterlee and A. Schüller, *Multigrid* (Academic Press, 2001), and
D. Braess, "Towards algebraic multigrid for elliptic problems of second order", *Computing* 55
(1995), for over-scaled piecewise-constant coarse corrections.
Color interpretation and scalar luminance transport retain the reviewed project definitions.
