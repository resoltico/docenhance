# Numerical composition and independent correctness

## Design pass

Trace decoded integer samples, source transfer/profile interpretation, exact orientation, linear
alpha compositing, oriented protection, frozen I01, prepared D01 and final quantization. Keep
container decoding, scalar interpretation, native ICC precision, numerical methods and publication
at their existing owners. No new operation, dependency, compatibility reader or processing graph
is justified by this audit.

Existing independent PNG/JPEG decode, color and I01 references remain useful. The whole native
D01 comparison checks tiling against the same native algorithm; it cannot establish that its
parameters and integer mathematics match the reviewed operation. The executable I01/D01 comparison
uses a serialized I01 intermediate with a tolerance. Decode-back verification uses the production
row producer, so a common composition defect can agree with itself.

Add a small direct-window reference for the pinned single-channel uint16 NLM-L1 mathematics:
global reflection, direct patch absolute differences, power-of-two distance bins, float32 native
strength/square, fixed integer weights/cutoff and rounded integer weighted means. Write the oracle
independently of the rolling native implementation; do not import OpenCV or copy its code.
Compose it with existing independent orientation/transfer and dense I01 references. Compare
independently decoded final PNG integers and stage observations, including protected identities,
alpha/color counts, native calls and replay neutrality. Investigate mismatches before changing
production behavior; a missing oracle alone does not establish a numerical defect.

## Separate design challenge

A floating ideal NLM oracle would test a different operation. The reference must include the
reviewed native integer binning, actual float32 strength arithmetic and integer rounding; retain
independent direct windows and unbounded Python integer accumulation instead of reproducing native
rolling sums, tiles or buffer arithmetic. Tiny fixtures keep exhaustive windows bounded.

Require wrong-order, early output quantization and altered protected-neighbor controls to differ
from the intended oracle. Use single-cell I01 fixtures for exact final-code comparisons, where
there is no PCG convergence approximation; keep the existing dense multi-cell solver checks.
A source has no native ICC precision guarantee beyond the documented float32 boundary. Do not
hide scalar composition errors by widening a shared tolerance or claim independent JPEG AC
integers without accounting for the documented IDCT difference.

Exercise both output and verification replay through real publication and standalone verification.
Check original source identity, exact protection, one-count observations and actual native call
counts. Register the executable test in the existing CTest discovery owner; do not create a
parallel test list. Keep method identities and wire contracts unless a demonstrated behavior change
requires a contract break.

## QA evidence and remediation

The independent corpus agrees with current production mathematics. It covers linear and sRGB
RGBA16, linear RGBA8 and gray-alpha16, all eight orientations, interlaced/noninterlaced PNG,
known gamma-two gray/RGB ICC ingress, gray/RGB output, PNG8/16 output and exact JPEG DC samples in
baseline and progressive containers. Maximal patch/search geometry and minimum/maximum strength
also have exhaustive direct-window checks at corners and an interior sample. Scalar final codes
are exact; only the known ICC fixtures allow one code at their documented native precision boundary,
and their protected samples must exactly match actual no-filter output.

A temporary production mutation quantized reconstructed entering RGB to 16-bit encoded values
before applying D01 correction. Both processing-time decode-back comparison and standalone bundle
verification accepted the resulting publication. The independent oracle rejected one-code final
sample discrepancies. The mutation was removed and the source restored; no processing behavior,
method identity or wire contract is changed by the remediation. Mathematical wrong-order,
early 8/16-bit quantization and altered protected-neighbor controls must also be detected.

CTest owns the new executable composition check. Stage/alpha observations, native strength,
native calls, protected identities, source bytes and response/record agreement are checked alongside
samples. This adds independent assurance for a shared-producer blind spot; it does not turn bundle
verification into source replay or document-authenticity certification. Platform workflow evidence
remains commit-specific.

Local TSan exposed unnecessary Cartesian repetition in the new executable corpus. Test every
orientation for each scalar source model at preserve16, and every output representation for each
model at orientation one; ICC/JPEG retain their representation checks. Final representation is
pointwise and does not configure upstream linear interpretation, I01 or D01. This decomposition
retains distinct numerical properties and negative controls while honoring the unchanged
90-second case limit. It is not a reduced tolerance or skipped method/format/orientation.
