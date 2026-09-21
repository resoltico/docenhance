# Implementation handoff and exit criteria

The work packages below are ordered by dependency. Each one needs its own specification — the mathematics, the boundary conventions, the failure behavior and the fixtures — written and reviewed before implementation begins; accepting an argument or cataloguing a method is not a specification.

| Order | Work package | Exit condition |
|---|---|---|
| 0 | Verify this native foundation on GitHub CI | Native GitHub validation runs on Windows/MSVC, Linux ARM64/x86-64 and macOS ARM64 with retained logs. macOS Intel remains required for release once a pinned LLVM 23 tool distribution is available on a hosted runner; do not substitute a different lint release. |
| 1 | Finish typed request/configuration core | Complete method-option variants; immutable effective configuration; strict RecipeV1 schema and duplicate-key rules; presets and precedence; every cross-option contradiction tested |
| 2 | Resource, cancellation and publication primitives | Checked allocation model, ownership-safe cancellation, exclusive platform publication and outcome reconciliation; adversarial filesystem tests |
| 3 | JPEG/PNG/TIFF and color pipeline | All accepted/rejected encodings and precision/profile/orientation contracts; C longjmp boundaries; malformed-image tests; codec fuzzing; no implicit 8-bit downgrade |
| 4 | Photometric reference operations | Target-plane conversions, masks, blending and validity semantics; full numerical fixtures; no complete-method advertisement before its tests |
| 5 | Illumination methods I01/I02 | Specified solvers, automatic-decision criteria, gains and protected-region behavior; tiled/reference equivalence where specified |
| 6 | Denoising D01/D02 | Exact 16-bit NLM-L1 correction contract and float TV-L1 solver; precision, convergence, cancellation and memory tests |
| 7 | Contrast, sharpening and binary methods | C01–C03, S01, B01–B03; complete masking, tie, histogram and window edge cases |
| 8 | Deblurring R01 | Known PSF construction/normalization, FFT centering, padding, DC preservation and rejection cases; no blind-kernel or inpainting substitution |
| 9 | Geometry G01–G05 | Exact orientations/quarter-turns, perspective, deskew and guarded dewarping; one non-orthogonal full-resolution resampling; uncertainty-aware abstention |
| 10 | App pipeline, batch and reporting | Page discovery/selection, sequential bounded processing, stable per-stage reports and complete bundle publication; failure/cancellation semantics |
| 11 | Cross-platform robustness and performance | Full corpus/codec/numerical/CLI tests; bounded resource measurements; real Windows Unicode paths; required minimum-OS environments |
| 12 | Public release audit | Complete license and binary composition review, SBOM, signing policy, reproducibility qualifications, package smoke tests and documentation consistent with actual behavior |

Each implementation change updates the relevant status entry and retains evidence. A green scaffold test suite is not equivalent to empirical readability/fidelity evaluation on real documents. Testing on already-good documents is required; a legitimate no-op must remain possible.

Do not create a “restore everything” preset by composing every method, add a second GUI, introduce OCR/neural inference, or change CLI meanings to simplify an implementation. Amend the authoritative design explicitly when a genuine contradiction is discovered.
