# Argument contract

> Generated from `spec/cli-contract.json` by `tools/generate_spec.py`. These are the arguments the command line parses and documents today. Accepting an argument is not a claim that the processing behind it exists; see [current CLI behavior](cli.md) and [status](status.md).

P = process; L = plan; I = inspect.

## Commands

- `root` — `docenhance COMMAND [OPTIONS]`
- `process` — `docenhance process INPUT --out-dir DIRECTORY [OPTIONS]`
- `plan` — `docenhance plan INPUT [OPTIONS]`
- `inspect` — `docenhance inspect INPUT [OPTIONS]`
- `presets` — `docenhance presets [NAME] [--json]`
- `methods` — `docenhance methods [ID] [--json]`
- `version` — `docenhance version [--json]`

## Value grammar

**Decimal values.** Finite decimal, optionally in scientific notation: an optional leading '-', digits with an optional '.', then an optional exponent 'e' or 'E' with an optional '+' or '-' and one or more digits. 1e2, 1e+2 and 1E+2 denote the same value. No leading '+', no surrounding or embedded whitespace, no hexadecimal, infinity or NaN spelling, and no trailing characters. The value must be finite and inside the option's documented range; the decimal point is '.' in every locale.

**Integer values.** Unsigned or signed decimal digits only, as each option documents. Integer-valued options do not accept scientific notation, and page selections accept digits, ',' and '-' only.

## `--out-dir PATH`

**Scope:** P,L. **Domain/default:** Required for P; optional for L.

New result directory. Parent must exist. Final path must not exist, including as a symlink. No implicit parent creation.

## `--recursive`

**Scope:** P,L,I. **Domain/default:** False.

Traverse input directory descendants under the discovery rules. Error for file input.

## `--pages SELECTION`

**Scope:** P,L,I. **Domain/default:** `all`.

File input only. `all` or comma-separated positive integers/inclusive ranges; no spaces, duplicates, zero, reversed or open-ended ranges. Selection must exist. Processing order is ascending page number.

## `--preset NAME`

**Scope:** P,L. **Domain/default:** `balanced` when no recipe.

`none`, `balanced`, `shadow`, `faint`, `noisy`. Explicitly incompatible with `--recipe`.

## `--recipe PATH`

**Scope:** P,L. **Domain/default:** Absent.

Strict complete JSON RecipeV1, maximum 2 MiB. No automatic user/system config files.

## `--protect-mask PATH`

**Scope:** P,L. **Domain/default:** Absent.

Single selected page only; protection contract in §5.9. Not a recipe path field.

## `--on-error MODE`

**Scope:** P,L. **Domain/default:** `stop`.

`stop` aborts before publication on any file/page failure. `continue` permits a partial bundle if at least one page succeeds. Fatal run/publication errors always abort.

## `--threads VALUE`

**Scope:** P,L. **Domain/default:** `auto`.

`auto` = min(4,max(1,reported hardware concurrency)); or integer 1–64. Internal numerical parallelism, not multiple simultaneous pages.

## `--memory-mib N`

**Scope:** P,L. **Domain/default:** 1024; integer 256–65536.

Working-allocation planning budget; not a promised hard RSS ceiling. 1 MiB = 1,048,576 bytes.

## `--max-megapixels N`

**Scope:** P,L. **Domain/default:** 40; finite 1–250.

Maximum pixels per decoded source page and final geometry canvas; 1 MP = 1,000,000 pixels. No automatic resize to satisfy it.

## `--max-input-mib N`

**Scope:** P,L. **Domain/default:** 1024; integer 1–8192.

Maximum encoded bytes in one source file, before snapshot/decode.

## `--progress MODE`

**Scope:** P,L. **Domain/default:** `auto`.

`auto` emits stderr progress only when stderr is a terminal; `plain` always emits lines; `none` disables it. Plan reports header progress only.

## `--quiet`

**Scope:** P,L. **Domain/default:** False.

Suppress informational stderr and progress, never the final result or errors. Overrides progress display only.

## `--json`

**Scope:** All commands. **Domain/default:** False.

Exactly one final stdout JSON object. `presets NAME --json` emits RecipeV1.

## `--help`

**Scope:** All commands. **Domain/default:** N/A.

Help and exit zero; no processing.

## `--version`

**Scope:** Root only. **Domain/default:** N/A.

Human-readable alias of `version`.

## `--output-mode MODE`

**Scope:** P,L. **Domain/default:** `preserve`; `preserve|gray|bw`.

Final representation. `bw` activates a binarizer, default Sauvola unless explicitly chosen.

**Applicable methods:** Final representation. `bw` activates a binarizer, default Sauvola unless explicitly chosen..

## `--format FORMAT`

**Scope:** P,L. **Domain/default:** `png`; `png|tiff`.

Lossless per-page output container; not inferred from `--out-dir`.

**Applicable methods:** Lossless per-page output container; not inferred from `--out-dir`..

## `--bit-depth DEPTH`

**Scope:** P,L. **Domain/default:** `auto`; `auto|1|8|16`.

Auto chooses 1 for bw, otherwise 16 if the decoded source carries 16-bit precision, else 8. Depth 1 requires bw. Explicit 8/16 with bw encodes only the two endpoint values.

**Applicable methods:** Auto chooses 1 for bw, otherwise 16 if the decoded source carries 16-bit precision, else 8. Depth 1 requires bw. Explicit 8/16 with bw encodes only the two endpoint values..

## `--alpha MODE`

**Scope:** P,L. **Domain/default:** `white`; `white|black|reject`.

§5.6 compositing policy.

**Applicable methods:** §5.6 compositing policy..

## `--profile-policy MODE`

**Scope:** P,L. **Domain/default:** `embedded`; `embedded|srgb`.

§5.3 color interpretation; `srgb` is an explicit profile override.

**Applicable methods:** §5.3 color interpretation; `srgb` is an explicit profile override..

## `--rotate DEGREES`

**Scope:** P,L. **Domain/default:** 0; `0|90|180|270`.

Exact clockwise quarter-turn after metadata orientation.

**Applicable methods:** G02.

## `--quad POINTS`

**Scope:** P,L. **Domain/default:** Absent.

Four normalized points `TL;TR;BR;BL`, each `x,y` in `[0,1]`, in the frame after `--rotate`. Requires one selected page and `--page-size`.

**Applicable methods:** G03.

## `--page-size WIDTHxHEIGHT`

**Scope:** P,L. **Domain/default:** Absent; each integer 16–50000.

Explicit rectified output pixel dimensions. Requires `--quad`; resource limits still apply.

**Applicable methods:** G03.

## `--deskew VALUE`

**Scope:** P,L. **Domain/default:** `off`; `off|auto|ANGLE`.

`ANGLE` is a finite clockwise correction in degrees, `[-15,15]`. Numeric zero means an intentional no-op.

**Applicable methods:** G04.

## `--deskew-limit DEGREES`

**Scope:** P,L. **Domain/default:** 5; finite `(0,15]`.

Half-width of the automatic search interval. Not a clipping operation on a larger estimated angle.

**Applicable methods:** G04 auto only.

## `--dewarp METHOD`

**Scope:** P,L. **Domain/default:** `off`; `off|text-lines`.

Explicit vertical text-line dewarping. No horizontal justification or borrowed page model.

**Applicable methods:** G05.

## `--dewarp-min-lines N`

**Scope:** P,L. **Domain/default:** 8; integer 8–40.

Minimum accepted long text lines for model construction.

**Applicable methods:** G05.

## `--dewarp-max-displacement F`

**Scope:** P,L. **Domain/default:** 0.08; finite `[0.01,0.15]`.

Maximum absolute vertical displacement divided by canvas height, after boundary taper.

**Applicable methods:** G05.

## `--illumination METHOD`

**Scope:** P,L. **Domain/default:** Preset; `off|auto|surface|morph`.

`auto` uses I01's estimator and exact automatic eligibility checks. It never substitutes I02.

**Applicable methods:** I01/I02.

## `--background-strength A`

**Scope:** P,L. **Domain/default:** 0.35; finite `[0,1]`.

Exponent/blend strength of the bounded gain field, not a sharpening amount. Zero is a no-op.

**Applicable methods:** I01/I02.

## `--background-max-gain G`

**Scope:** P,L. **Domain/default:** 1.5; finite `[1,4]`.

Hard upper bound on the proposed luminance multiplier before gamut-safe transport.

**Applicable methods:** I01/I02.

## `--background-target VALUE`

**Scope:** P,L. **Domain/default:** `source`; `source` or finite `[0.1,1]`.

`source` = 90th percentile of valid background estimates. Numeric value is a linear-light target. No automatic pure-white paper target.

**Applicable methods:** I01/I02.

## `--background-cell VALUE`

**Scope:** P,L. **Domain/default:** `auto`; integer 8–512.

Full-canvas cell edge in pixels. Auto = clamp(round(min(W,H)/24),16,256).

**Applicable methods:** I01/auto.

## `--background-quantile Q`

**Scope:** P,L. **Domain/default:** 0.90; finite `[0.75,0.99]`.

Bright-sample quantile within each eligible cell.

**Applicable methods:** I01/auto.

## `--background-smooth BETA`

**Scope:** P,L. **Domain/default:** 2.0; finite `[0.1,20]`.

Graph smoothness coefficient of the log-surface solve.

**Applicable methods:** I01/auto.

## `--background-radius VALUE`

**Scope:** P,L. **Domain/default:** `auto`; integer 1–256.

Morphological radius; square side `2r+1`. Auto = clamp(round(min(W,H)/50),8,128).

**Applicable methods:** I02 only.

## `--denoise METHOD`

**Scope:** P,L. **Domain/default:** Preset; `off|nlm|tvl1`.

Select exactly one denoiser.

**Applicable methods:** D01/D02.

## `--denoise-blend A`

**Scope:** P,L. **Domain/default:** 0.5; finite `[0,1]`.

Convex blend of stage input and denoised candidate. Zero is a no-op.

**Applicable methods:** D01/D02.

## `--nlm-h H`

**Scope:** P,L. **Domain/default:** 3.0; finite `[0.1,25]`.

Strength in equivalent 8-bit perceptual intensity points. Native 16-bit strength = `257*H`.

**Applicable methods:** D01.

## `--nlm-patch N`

**Scope:** P,L. **Domain/default:** 7; odd integer 3–15.

Patch width and height, pixels.

**Applicable methods:** D01.

## `--nlm-search N`

**Scope:** P,L. **Domain/default:** 21; odd integer 7–41.

Search width and height, pixels; must be ≥ patch size and fit the image's smaller dimension.

**Applicable methods:** D01.

## `--tv-lambda LAMBDA`

**Scope:** P,L. **Domain/default:** 1.5; finite `[0.05,20]`.

Weight of the L1 fidelity term in `TV(u)+lambda*|u-f|_1`; **larger means less smoothing**.

**Applicable methods:** D02.

## `--tv-iterations N`

**Scope:** P,L. **Domain/default:** 150; integer 10–1000.

Maximum primal-dual iterations.

**Applicable methods:** D02.

## `--tv-tolerance EPS`

**Scope:** P,L. **Domain/default:** 0.00001; finite `[1e-8,1e-3]`.

Both primal and dual update tolerances; tested every 10 iterations after iteration 20.

**Applicable methods:** D02.

## `--deblur METHOD`

**Scope:** P,L. **Domain/default:** `off`; `off|wiener`.

Known-PSF regularized restoration; never enabled by a preset.

**Applicable methods:** R01.

## `--psf KIND`

**Scope:** P,L. **Domain/default:** Required when enabling R01 without recipe PSF; `gaussian|motion|kernel`.

Blur model in the post-geometry processing coordinate system.

**Applicable methods:** R01.

## `--psf-sigma SIGMA`

**Scope:** P,L. **Domain/default:** 1.0; finite `[0.3,5]`.

Standard deviation, pixels. Kernel radius = ceil(3*sigma).

**Applicable methods:** Gaussian PSF.

## `--psf-length LENGTH`

**Scope:** P,L. **Domain/default:** 5.0; finite `[1,31]`.

Length of the exposure segment, pixels.

**Applicable methods:** Motion PSF.

## `--psf-angle ANGLE`

**Scope:** P,L. **Domain/default:** 0; finite `[-180,180]`.

Clockwise direction from positive x; negative allowed.

**Applicable methods:** Motion PSF.

## `--psf-file PATH`

**Scope:** P,L. **Domain/default:** Required for CLI `kernel` unless recipe contains coefficients.

Single grayscale PNG, unsigned 8/16-bit, odd dimensions 3–129, no alpha. Raw samples are coefficients; ignore profile/gamma. Nonnegative, positive sum required.

**Applicable methods:** Kernel PSF.

## `--wiener-k K`

**Scope:** P,L. **Domain/default:** 0.01; finite `[1e-5,1]`.

Regularization term in `|H|^2+K`; not a claimed measured noise-to-signal ratio.

**Applicable methods:** R01.

## `--deblur-blend A`

**Scope:** P,L. **Domain/default:** 0.5; finite `[0,1]`.

Blend of original linear luminance and the restored candidate.

**Applicable methods:** R01.

## `--contrast METHOD`

**Scope:** P,L. **Domain/default:** Preset; `off|levels|gamma|clahe`.

Exactly one contrast method.

**Applicable methods:** C01/C02/C03.

## `--contrast-blend A`

**Scope:** P,L. **Domain/default:** 1.0; finite `[0,1]`.

Convex blend with the original perceptual plane.

**Applicable methods:** C01/C02/C03.

## `--levels-low P`

**Scope:** P,L. **Domain/default:** 0.5; finite `[0,10]` percent.

Lower percentile among valid unprotected samples.

**Applicable methods:** C01.

## `--levels-high P`

**Scope:** P,L. **Domain/default:** 99.5; finite `[90,100]` percent.

Upper percentile; must exceed lower percentile.

**Applicable methods:** C01.

## `--gamma G`

**Scope:** P,L. **Domain/default:** 1.2; finite `[0.25,4]`.

`f_new=f^G`; values above 1 darken midtones while retaining endpoints.

**Applicable methods:** C02.

## `--clahe-grid CxR`

**Scope:** P,L. **Domain/default:** `8x8`; each integer 2–32.

Number of contextual columns/rows, not tile pixel dimensions. Each tile must have at least 16 pixels in each direction.

**Applicable methods:** C03.

## `--clahe-clip C`

**Scope:** P,L. **Domain/default:** 2.0; finite `[1,8]`.

Pre-redistribution histogram clip multiplier relative to average occupancy.

**Applicable methods:** C03.

## `--sharpen METHOD`

**Scope:** P,L. **Domain/default:** `off`; `off|unsharp`.

Thresholded unsharp masking; never enabled by a preset.

**Applicable methods:** S01.

## `--sharpen-sigma S`

**Scope:** P,L. **Domain/default:** 0.8; finite `[0.3,3]`.

Gaussian blur standard deviation, pixels.

**Applicable methods:** S01.

## `--sharpen-amount A`

**Scope:** P,L. **Domain/default:** 0.5; finite `[0,2]`.

High-pass amplification.

**Applicable methods:** S01.

## `--sharpen-threshold T`

**Scope:** P,L. **Domain/default:** 1.0; finite `[0,20]`.

Soft-threshold in equivalent 8-bit perceptual intensity points; divide by 255 internally.

**Applicable methods:** S01.

## `--binarize METHOD`

**Scope:** P,L. **Domain/default:** `sauvola` when bw is activated; `otsu|sauvola|fixed`.

Requires `--output-mode bw`; does not implicitly change output mode.

**Applicable methods:** B01/B02/B03.

## `--sauvola-window VALUE`

**Scope:** P,L. **Domain/default:** `auto`; odd integer 3–511.

Window side. Auto = `2*floor((v-1)/2+0.5)+1` for `v=0.015*min(W,H)` (nearest odd, ties upward), clamped 15–101, then reduced to largest odd ≤ min(W,H) when necessary.

**Applicable methods:** B02.

## `--sauvola-k K`

**Scope:** P,L. **Domain/default:** 0.20; finite `[0,1]`.

Local threshold coefficient.

**Applicable methods:** B02.

## `--sauvola-r R`

**Scope:** P,L. **Domain/default:** 0.50; finite `[0.05,1]`.

Standard-deviation scale in normalized perceptual intensity units.

**Applicable methods:** B02.

## `--fixed-threshold T`

**Scope:** P,L. **Domain/default:** 0.50; finite `[0,1]`.

Black iff `f≤T`.

**Applicable methods:** B03.
