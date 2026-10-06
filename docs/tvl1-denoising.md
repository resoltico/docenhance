# Floating-point TV-L1 denoising

D02 is an explicit alternative selected by `--denoise tvl1`; denoising defaults to off.
It follows the selected immutable illumination operation and precedes final quantization.
Use the existing oriented linear-RGB converter and sRGB perceptual luminance f=E(Y).
The current double-precision working contract supersedes the blueprint's float32 frame proposal:
input, primal, extrapolated primal and two dual planes use float64, with float64 reductions.
There is no integer analysis plane or eight-bit bottleneck.

## Reviewed design

Minimize isotropic forward-difference TV(u)+lambda*sum(abs(u-f)), subject to 0<=u<=1.
Forward gradients are zero at the last column/row. The algebraic adjoint adds incoming edge
components and subtracts outgoing components; terminal dual components are always zero.
Use tau=sigma=0.25 and theta=1, with the specified unit-ball dual projection and clamped
L1 proximal update. Extrapolated values may lie in [-1,2]; they are not clamped to [0,1].

A full dual pass reads the unchanged extrapolated plane and updates each dual vector independently.
Only after it finishes does a full primal pass read the frozen new dual field. Primal updates use
only that pixel's old primal/input, so the old value can be retained in a scalar register while
updating the primal and extrapolated planes. This has the same simultaneous-iterate semantics as
double buffering, without mixed raster ordering. Five charged aligned float64 planes cost roughly
40 bytes/pixel; a bounded RGB transfer block is also charged. Retain input/result only after solve.
Account actual padding and shared-ledger peak, including decoded source, mask and illumination.
The existing 1 GiB charge budget is not an RSS limit. Refuse insufficient resources; do not tile,
downsample, lower precision, reduce iterations or substitute a denoiser.

Parameters: lambda [0.05,20], default 1.5; iterations [10,1000], default 150; tolerance
[1e-8,1e-3], default 1e-5; shared blend [0,1], default 0.5. NLM and TV private options are
mutually exclusive and require their exact selector. All denoising options are rejected on bw.
Validate source/mask even for zero blend and fully protected no-ops; those do not allocate a solver.

Measure both maximum primal update and maximum per-pixel Euclidean dual update each iteration.
At iterations 20,30,... require both <= tolerance at two consecutive checkpoints. Earliest
`tolerance_met` is iteration 30. Otherwise finish at the cap with `iteration_limit` and
W_TV_ITERATION_LIMIT, including caps 10/20 and nonmultiples of ten. If a second passing checkpoint
coincides with the cap, tolerance_met wins. This is a practical stopping test, not a convergence
certificate. Report initial/final objective; do not promise per-iteration objective monotonicity.
Objectives describe the full solver field before output protection and blending, not the final
encoded image. Evaluated samples exclude protected destinations; corrected samples count u!=f,
and changed samples count actual linear-RGB changes after blending and transport.

Protection is output-only: all entering samples remain solver context. Protected RGB is restored
exactly by bypassing transport. Blend in perceptual space; equal candidate/input or equal blended
value retains the entering RGB exactly. Reuse the immutable result for output verification.
Do not rerun the solver or double-count reconstruction observations.

Cancellation is explicit execution control: observe once per iteration and at bounded intervals
inside transfers, gradient/projection, adjoint/proximal, objective and reconstruction passes.
Discard partial buffers and preserve genuine-error precedence, completed illumination observations
and the existing publication cutoff. Iteration exhaustion is a reported usable iterate, not a
numerical failure or false tolerance claim. Failed stages retain bounded partial observations.

Typed denoising alternatives and parameter variants derive the runtime catalog. D01 native fields
remain specific to NLM; D02 solver fields cannot be mixed with native observations. Closed response
and record contracts move together to version 6 and refuse obsolete versions. No package or layer
edge is added: pure TV math belongs to de_methods and the existing host composes execution.

## Separate challenge and evidence obligations

The separate preimplementation experiment verified adjoint errors below 1e-10, exact agreement
between simultaneous and two-pass iterations, and constant/short-cap boundaries. Its seeded
32x32 noisy objective decreased from 42.33581846380465 to 31.21734715406113 after 400 iterations,
correctly retaining iteration_limit. Runtime tests extend that experiment.

Check the adjoint by independent edge scattering, including 1x1, one-row/one-column and non-square
fields. Compare simultaneous double-buffer reference iterations against the two-pass implementation;
poison terminal dual values in rejection tests. Constant fields must remain exact. Verify bounds,
objective reduction on a defined noisy fixture after 400 iterations, low-contrast 16-bit distinctions,
and short-cap/noncheckpoint/final-checkpoint stopping cases. Never assert per-step monotonicity.

Test protected context versus a spatially constrained solver and exact protected output, zero blend,
all-protected validation, resource peak/one-byte-short refusal and cancellation at every bounded
checkpoint without sleeps. Real executable tests must cover I01/I02 composition, color/alpha,
8/16-bit precision, stage counters, iteration-limit warnings, bundle round trips and malformed
partial reports. Parser/solver fuzzing and complete native/sanitizer/CI runs precede merge.
