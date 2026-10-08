# Argument contract

> Generated from `spec/cli-contract.json` by `tools/generate_spec.py`. These are the arguments the command line parses and documents today. See [current CLI behavior](cli.md) and [status](status.md) for supported behavior.

P = process.

## Commands

- `root` — `docenhance COMMAND [OPTIONS]`
- `process` — `docenhance process INPUT --out-dir DIRECTORY [--output-mode preserve|gray|bw] [OPTIONS]`
  Admit static PNG or bounded 8-bit baseline/progressive Huffman JPEG by signature. JPEG supports preserve/gray with opt-in I01; bw and protection masks retain their grayscale PNG contracts. All PNG paths reject animation and trailing container bytes; binary processing retains stored grayscale samples.
- `verify` — `docenhance verify DIRECTORY [--json]`
  Validate the complete supported run record, closed artifact inventory and observable PNG properties without executing the recorded request.
- `methods` — `docenhance methods [ID] [--json]`
- `version` — `docenhance version [--json]`

## Input support

- `png`: `preserve`, `gray`, `bw`.
- `jpeg`: `preserve`, `gray`.
- `tiff`: `preserve`, `gray`.

## Value grammar

**Decimal values.** Finite decimal, optionally in scientific notation: an optional leading '-', digits with an optional '.', then an optional exponent 'e' or 'E' with an optional '+' or '-' and one or more digits. No leading '+', whitespace, hexadecimal, infinity, NaN, or trailing characters. The value must be finite and inside the option's documented range; the decimal point is '.' in every locale. Conversion overflow, subnormal results and nonzero magnitudes rounded to zero are rejected; signed zero is admitted.

**Window values.** Decimal digits only; odd integer in [3,4095]. No sign, whitespace, fraction or exponent.

## `--out-dir DIRECTORY`

**Scope:** P. **Domain/default:** Required; a new result directory.

Paths must be well-formed UTF-8 and are never normalized or repaired. Relative effects bind to the operation's initial working directory; reported spelling is retained. Windows drive-relative publication paths are rejected. Parent must exist. Publish result.png into a new directory only after complete processing. Existing destinations are never replaced.

## `--output-mode MODE`

**Scope:** P. **Domain/default:** preserve; preserve|gray|bw.

Preserve keeps the decoded gray/color category, not source bytes, ICC space, metadata or alpha. Continuous output has no enhancement filter and is verified before publication. Gray converts linear luminance; bw explicitly activates stored-sample binarization. JPEG input supports preserve/gray only; bw requires grayscale PNG.

## `--bit-depth DEPTH`

**Scope:** P. **Domain/default:** auto; auto|8|16.

Continuous output only. Auto retains 16-bit source precision, otherwise 8. Explicit 16-to-8 reduction is reported; no precision fallback is used for resource limits.

## `--alpha MODE`

**Scope:** P. **Domain/default:** white; white|black|reject.

Continuous output only. Composite straight PNG alpha over white or black in linear light; reject refuses any non-opaque pixel. Output is opaque.

## `--profile-policy POLICY`

**Scope:** P. **Domain/default:** embedded; embedded|srgb.

Continuous output only. Interpret supported cICP, compatible ICC, sRGB or gAMA/cHRM; otherwise report an sRGB assumption. srgb explicitly overrides color declarations, not integrity or orientation checks. Unsupported HDR/cICP is rejected by embedded policy.

## `--binarize METHOD`

**Scope:** P. **Domain/default:** sauvola for bw; otsu|fixed|sauvola.

Requires explicit --output-mode bw. B01/B02/B03 use stored 1/2/4/8-bit grayscale PNG samples without transparency and output 8-bit black/white; no color or gamma conversion is applied.

**Applicable methods:** B01,B02,B03.

## `--fixed-threshold T`

**Scope:** P. **Domain/default:** 0.50; finite `[0,1]`.

B03 only. Normalized grayscale threshold; black iff sample/255 <= T. Rejected for Sauvola.

**Applicable methods:** B03.

## `--sauvola-window PIXELS`

**Scope:** P. **Domain/default:** 31; odd integer `[3,4095]`.

B02 only. Centered square window with REFLECT_101 borders; a singleton dimension repeats its sample.

**Applicable methods:** B02.

## `--sauvola-k K`

**Scope:** P. **Domain/default:** 0.2; finite `[0,1]`.

B02 only. Weight in T=m*(1+k*(s/(255*R)-1)), using population standard deviation in stored byte units.

**Applicable methods:** B02.

## `--sauvola-r R`

**Scope:** P. **Domain/default:** 0.5; finite `[1/255,1]`.

B02 only. Normalized deviation scale: 0.5 means 127.5 in byte units, not 128. Black iff sample <= T; no threshold rounding or clipping.

**Applicable methods:** B02.

## `--json`

**Scope:** All commands. **Domain/default:** False.

Render one JSON response on stdout and no diagnostic text on stderr. Flush the selected stream before returning. JSON exit_code describes the rendered command outcome; response-delivery failure can instead end the process with exit 5. Check both the response and process status. A missing/incomplete response or exit 5 does not prove that publication did not commit; do not retry blindly.

## `--help`

**Scope:** All commands. **Domain/default:** N/A.

Renders help and exits without opening input or creating output.

## `--version`

**Scope:** Root only. **Domain/default:** N/A.

Human-readable alias of `version`.

## `--illumination MODE`

**Scope:** P. **Domain/default:** off; off|surface|auto|morph.

Select explicit I01 surface or I02 morphological illumination for continuous preserve/gray. Auto remains I01 only.

**Applicable methods:** I01,I02.

## `--background-strength A`

**Scope:** P. **Domain/default:** 1; finite [0,1].

Exponent applied to the illumination gain; 1 corrects the fitted background fully within the gain cap. Zero is an exact photometric no-op after parameter and mask validation.

**Applicable methods:** I01,I02.

## `--background-max-gain G`

**Scope:** P. **Domain/default:** 2; finite [1,4].

Maximum illumination multiplicative gain. One is an exact photometric no-op.

**Applicable methods:** I01,I02.

## `--background-target TARGET`

**Scope:** P. **Domain/default:** source; source or finite [0.1,1].

Linear-light target. Source uses the fitted background nearest-rank 90th percentile, not forced paper white.

**Applicable methods:** I01,I02.

## `--background-cell SIZE`

**Scope:** P. **Domain/default:** auto; auto or integer [8,512].

Cell edge in oriented pixels. Auto is round(min(width,height)/24), clamped to [16,256]. Resource refusal never changes this value.

**Applicable methods:** I01.

## `--background-quantile Q`

**Scope:** P. **Domain/default:** 0.90; finite [0.75,0.99].

Nearest-rank quantile of all eligible cell samples; protected samples are excluded.

**Applicable methods:** I01.

## `--background-smooth BETA`

**Scope:** P. **Domain/default:** 1; finite [0.1,20].

Positive grid-Laplacian weight for fitting the logarithmic background.

**Applicable methods:** I01.

## `--background-radius RADIUS`

**Scope:** P. **Domain/default:** auto; auto or integer [1,256].

Square closing radius in oriented pixels. Auto rounds min(width,height)/50 and clamps to [8,128]; Gaussian sigma=max(0.5,radius/2).

**Applicable methods:** I02.

## `--protect-mask PATH`

**Scope:** P. **Domain/default:** Absent.

1-bit or 8-bit grayscale PNG mask matching oriented source dimensions. Nonzero protects. Any alpha must be fully opaque; mask orientation must be normal. The mask is validated even when illumination and denoising are disabled.

**Applicable methods:** I01,I02,D01,D02,C01,C02,C03,S01.

## `--denoise METHOD`

**Scope:** P. **Domain/default:** off; off|nlm|tvl1.

Explicit NLM-L1 or full-field floating-point TV-L1 for continuous PNG/JPEG/TIFF after illumination. No binary denoising.

**Applicable methods:** D01,D02.

## `--denoise-blend A`

**Scope:** P. **Domain/default:** 0.5; finite `[0,1]`.

Blend once in perceptual luminance; zero preserves entering samples after source/mask validation.

**Applicable methods:** D01,D02.

## `--nlm-h H`

**Scope:** P. **Domain/default:** 3; finite `[0.1,25]`.

NLM only. Equivalent 8-bit perceptual strength; native float strength is 257 times float(H). Higher strengths may remove marks.

**Applicable methods:** D01.

## `--nlm-patch PIXELS`

**Scope:** P. **Domain/default:** 7; odd integer `[3,15]`.

NLM only. Square patch width.

**Applicable methods:** D01.

## `--nlm-search PIXELS`

**Scope:** P. **Domain/default:** 21; odd integer `[7,41]`.

NLM only. Search at least patch; active processing requires fit in smaller oriented dimension.

**Applicable methods:** D01.

## `--tv-lambda LAMBDA`

**Scope:** P. **Domain/default:** 1.5; finite `[0.05,20]`.

TV-L1 only. L1 fidelity weight; larger values mean less smoothing.

**Applicable methods:** D02.

## `--tv-iterations N`

**Scope:** P. **Domain/default:** 150; integer `[10,1000]`.

TV-L1 only. Iteration cap; exhaustion retains a usable iterate and emits W_TV_ITERATION_LIMIT.

**Applicable methods:** D02.

## `--tv-tolerance EPS`

**Scope:** P. **Domain/default:** 0.00001; finite `[1e-8,1e-3]`.

TV-L1 only. Both primal and dual update tolerances at two consecutive ten-iteration checkpoints, starting at iteration 20.

**Applicable methods:** D02.

## `--contrast METHOD`

**Scope:** P. **Domain/default:** off; off|levels|gamma|clahe.

Explicit levels, gamma or CLAHE on continuous PNG/JPEG/TIFF after illumination and denoising.

**Applicable methods:** C01,C02,C03.

## `--contrast-blend A`

**Scope:** P. **Domain/default:** 1; finite `[0,1]`.

Blend once in perceptual luminance. Zero retains entering samples after source/mask validation.

**Applicable methods:** C01,C02,C03.

## `--levels-low P`

**Scope:** P. **Domain/default:** 0.5; finite `[0,10]` percent.

Levels only. Nearest-rank lower percentile among all unprotected perceptual samples.

**Applicable methods:** C01.

## `--levels-high P`

**Scope:** P. **Domain/default:** 99.5; finite `[90,100]` percent.

Levels only. Nearest-rank upper percentile, greater than low. A range below 1e-6 retains exact entering RGB.

**Applicable methods:** C01.

## `--gamma G`

**Scope:** P. **Domain/default:** 1.2; finite `[0.25,4]`.

Gamma only. f_new=f^G; above one darkens midtones. Endpoints and gamma one are exact identities.

**Applicable methods:** C02.

## `--clahe-grid CxR`

**Scope:** P. **Domain/default:** 8x8; each integer [2,32].

CLAHE contextual columns and rows. Every tile must be at least 16 pixels in each direction.

**Applicable methods:** C03.

## `--clahe-clip C`

**Scope:** P. **Domain/default:** 2; finite [1,8].

CLAHE pre-redistribution clip multiplier relative to average eligible occupancy.

**Applicable methods:** C03.

## `--sharpen METHOD`

**Scope:** P. **Domain/default:** off; off|unsharp.

Explicit thresholded unsharp masking after contrast; never enabled by default.

**Applicable methods:** S01.

## `--sharpen-sigma S`

**Scope:** P. **Domain/default:** 0.8; finite [0.3,3].

Gaussian standard deviation in pixels; radius ceil(3*sigma), REFLECT_101 borders.

**Applicable methods:** S01.

## `--sharpen-amount A`

**Scope:** P. **Domain/default:** 0.5; finite [0,2].

Soft-thresholded high-pass amplification; zero is an algebraic identity.

**Applicable methods:** S01.

## `--sharpen-threshold T`

**Scope:** P. **Domain/default:** 1.0; finite [0,20].

Soft threshold in equivalent 8-bit perceptual intensity points; divided by 255 internally.

**Applicable methods:** S01.
