# Implementation handoff and exit criteria

The work packages below are ordered by dependency. Each one needs its own specification — the mathematics, the boundary conventions, the failure behavior and the fixtures — written and reviewed before implementation begins; accepting an argument or cataloguing a method is not a specification.

| Order | Work package | Exit condition |
|---|---|---|
| 0 | Verify the native baseline on GitHub CI | Complete: native GitHub validation runs on Windows/MSVC, Linux ARM64/x86-64 and macOS ARM64/Intel with retained logs and the pinned LLVM 23 toolchain. |
| 1 | Extend typed method execution | B02/B03 are validated alternatives with bounded execution and cross-option tests. Add further alternatives alongside complete methods; introduce recipes/presets only for a demonstrated composition requirement. |
| 2 | Resource, cancellation and publication primitives | Owned budgets, cooperative cancellation and exclusive publication have tested contracts. Extend alongside real requirements; forced-shutdown recovery and crash durability are not implied. |
| 3 | Extend image containers and interpretation | Static PNG continuous-tone precision/profile/alpha/orientation and verified output are implemented. Add JPEG/TIFF alongside complete codec contracts, malformed-input coverage and the same resource/cancellation boundaries. |
| 4 | Extend photometric operations | Linear-block interpretation, shared final quantization and oriented protection support I01. Add further transport/blending/validity contracts with concrete methods. |
| 5 | Extend illumination | I01 has bounded quantile/log-grid fitting, true-residual PCG, opt-in applicability, protected regions and independent references. I02 remains unimplemented; do not use it as a silent fallback. |
| 6 | Denoising D01/D02 | Exact 16-bit NLM-L1 correction contract and float TV-L1 solver; precision, convergence, cancellation and memory tests |
| 7 | Contrast, sharpening and binary methods | C01–C03, S01 and B01; retain the implemented B02/B03 contracts and extend masking/tie/histogram/window fixtures with each new operation |
| 8 | Deblurring R01 | Known PSF construction/normalization, FFT centering, padding, DC preservation and rejection cases; no blind-kernel or inpainting substitution |
| 9 | Geometry G01–G05 | Exact orientations/quarter-turns, perspective, deskew and guarded dewarping; one non-orthogonal full-resolution resampling; uncertainty-aware abstention |
| 10 | App pipeline, batch and reporting | Page discovery/selection, sequential bounded processing, stable per-stage reports and complete bundle publication; failure/cancellation semantics |
| 11 | Cross-platform robustness and performance | Full corpus/codec/numerical/CLI tests; bounded resource measurements; real Windows Unicode paths; required minimum-OS environments |
| 12 | Public release audit | Complete license and binary composition review, SBOM, signing policy, reproducibility qualifications, package smoke tests and documentation consistent with actual behavior |

Each implementation change updates the relevant status entry and retains evidence. A green test suite is not equivalent to empirical readability/fidelity evaluation on real documents. Testing on already-good documents is required; a legitimate no-op must remain possible.

Do not create a “restore everything” preset by composing every method, add a second GUI, introduce OCR/neural inference, or change CLI meanings to simplify an implementation. Amend the authoritative design explicitly when a genuine contradiction is discovered.
