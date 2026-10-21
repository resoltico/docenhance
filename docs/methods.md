# Methods

> Generated from `spec/cli-contract.json` and `spec/method-contract.json` by `tools/generate_spec.py`. Edit those authorities instead.

Discovery lists implemented processing methods. Metadata orientation and exact quarter-turn geometry are common operations described in [geometry](pipeline.md). Future work is tracked in the [roadmap](roadmap.md).

## I01 — Quantile log-surface illumination

Family: **illumination**. Method version: **1**.

- `--illumination surface`: explicit.
- `--illumination auto`: automatic.

### `--background-strength A`

**Domain/default:** 1; finite [0,1].

Exponent applied to the illumination gain; 0 is an exact no-op.

### `--background-max-gain G`

**Domain/default:** 2; finite [1,4].

Largest multiplicative gain applied to any sample; 1 is an exact no-op.

### `--background-target TARGET`

**Domain/default:** source; source | finite [0.1,1].

Linear-light target; 'source' uses the fitted background's nearest-rank 90th percentile.

### `--background-cell SIZE`

**Domain/default:** auto; auto | integer [8,512].

Cell edge in processing pixels.

**Automatic resolution:** round(min(W,H)/24) clamped to [16,256].

### `--background-quantile Q`

**Domain/default:** 0.90; finite [0.75,0.99].

Nearest-rank quantile of eligible cell samples.

### `--background-smooth BETA`

**Domain/default:** 1; finite [0.1,20].

Grid-Laplacian weight of the logarithmic background fit.

## I02 — Morphological illumination

Family: **illumination**. Method version: **1**.

- `--illumination morph`: explicit.

### `--background-strength A`

**Domain/default:** 1; finite [0,1].

Exponent applied to the illumination gain; 0 is an exact no-op.

### `--background-max-gain G`

**Domain/default:** 2; finite [1,4].

Largest multiplicative gain applied to any sample; 1 is an exact no-op.

### `--background-target TARGET`

**Domain/default:** source; source | finite [0.1,1].

Linear-light target; 'source' uses the fitted background's nearest-rank 90th percentile.

### `--background-radius RADIUS`

**Domain/default:** auto; auto | integer [1,256].

Square closing radius in processing pixels; Gaussian sigma = max(0.5, radius/2).

**Automatic resolution:** round(min(W,H)/50) clamped to [8,128].

## D01 — NLM-L1 on 16-bit perceptual codes

Family: **denoising**. Method version: **2**.

- `--denoise nlm`: explicit.

### `--denoise-blend A`

**Domain/default:** 0.5; finite [0,1].

Blend in perceptual luminance; 0 keeps entering samples.

### `--nlm-h H`

**Domain/default:** 3; finite [0.1,25].

Filter strength in equivalent 8-bit perceptual points; higher values may remove marks.

### `--nlm-patch PIXELS`

**Domain/default:** 7; odd integer [3,15].

Square patch width.

### `--nlm-search PIXELS`

**Domain/default:** 21; odd integer [7,41].

Square search width; at least the patch width.

Cross-field constraints: search >= patch.

## D02 — Floating-point TV-L1

Family: **denoising**. Method version: **1**.

- `--denoise tvl1`: explicit.

### `--denoise-blend A`

**Domain/default:** 0.5; finite [0,1].

Blend in perceptual luminance; 0 keeps entering samples.

### `--tv-lambda LAMBDA`

**Domain/default:** 1.5; finite [0.05,20].

L1 fidelity weight; larger values smooth less.

### `--tv-iterations N`

**Domain/default:** 150; integer [10,1000].

Iteration cap; exhaustion keeps the iterate and warns.

### `--tv-tolerance EPS`

**Domain/default:** 1e-5; finite [1e-8,1e-3].

Primal and dual update tolerance at two consecutive checkpoints.

## R01 — Known-PSF regularized Fourier restoration

Family: **restoration**. Method version: **2**.

- `--deblur wiener`: explicit.

### `--psf KIND`

**Domain/default:** required; gaussian | motion | kernel.

Point-spread function model in the processing frame.

Explicit presence is required when this option applies.

### `--psf-sigma SIGMA`

**Domain/default:** 1; finite [0.3,5].

Gaussian standard deviation in pixels.

**Requires:** `--psf=gaussian`.

### `--psf-length LENGTH`

**Domain/default:** 5; finite [1,31].

Motion segment length in pixels.

**Requires:** `--psf=motion`.

### `--psf-angle ANGLE`

**Domain/default:** 0; finite [-180,180].

Motion direction, degrees clockwise from +x in y-down coordinates.

**Requires:** `--psf=motion`.

### `--psf-file PATH`

**Domain/default:** required; well-formed UTF-8 path.

Grayscale PNG of raw non-negative coefficients, odd sides 3..129.

**Requires:** `--psf=kernel`.

Explicit presence is required when this option applies.

### `--wiener-k K`

**Domain/default:** 0.01; finite [1e-5,1].

Positive regularizer in |H|^2+K; not a measured noise ratio.

### `--deblur-blend A`

**Domain/default:** 0.5; finite [0,1].

Blend of entering and restored linear luminance before clamping.

## C01 — Percentile levels

Family: **contrast**. Method version: **1**.

- `--contrast levels`: explicit.

### `--contrast-blend A`

**Domain/default:** 1; finite [0,1].

Blend in perceptual luminance; 0 keeps entering samples.

### `--levels-low P`

**Domain/default:** 0.5; finite [0,10].

Nearest-rank lower percentile of unprotected perceptual samples.

### `--levels-high P`

**Domain/default:** 99.5; finite [90,100].

Nearest-rank upper percentile; greater than the lower one.

Cross-field constraints: high > low.

## C02 — Gamma

Family: **contrast**. Method version: **1**.

- `--contrast gamma`: explicit.

### `--contrast-blend A`

**Domain/default:** 1; finite [0,1].

Blend in perceptual luminance; 0 keeps entering samples.

### `--gamma G`

**Domain/default:** 1.2; finite [0.25,4].

f' = f^G; above 1 darkens midtones; 1 is an exact identity.

## C03 — Masked floating-point CLAHE

Family: **contrast**. Method version: **1**.

- `--contrast clahe`: explicit.

### `--contrast-blend A`

**Domain/default:** 1; finite [0,1].

Blend in perceptual luminance; 0 keeps entering samples.

### `--clahe-grid CxR`

**Domain/default:** 8x8; CxR, each integer [2,32].

Contextual columns x rows; every tile at least 16 pixels per side.

### `--clahe-clip C`

**Domain/default:** 2; finite [1,8].

Pre-redistribution clip multiplier relative to mean occupancy.

## S01 — Thresholded unsharp masking

Family: **sharpening**. Method version: **1**.

- `--sharpen unsharp`: explicit.

### `--sharpen-sigma S`

**Domain/default:** 0.8; finite [0.3,3].

Gaussian standard deviation in pixels.

### `--sharpen-amount A`

**Domain/default:** 0.5; finite [0,2].

Soft-thresholded high-pass gain; 0 is an exact identity.

### `--sharpen-threshold T`

**Domain/default:** 1; finite [0,20].

Soft threshold in equivalent 8-bit perceptual points.

## B01 — Otsu

Family: **binarization**. Method version: **2**.

- `--binarize otsu`: explicit.

## B02 — Sauvola

Family: **binarization**. Method version: **2**.

- `--binarize sauvola`: explicit.

### `--sauvola-window PIXELS`

**Domain/default:** 31; odd integer [3,4095].

Centred square window with REFLECT_101 borders.

### `--sauvola-k K`

**Domain/default:** 0.2; finite [0,1].

Weight in T = m*(1 + k*(s/R - 1)).

### `--sauvola-r R`

**Domain/default:** 0.5; finite [1/255,1].

Normalized deviation scale.

## B03 — Fixed threshold

Family: **binarization**. Method version: **2**.

- `--binarize fixed`: explicit.

### `--fixed-threshold T`

**Domain/default:** 0.5; finite [0,1].

Black iff the normalized terminal Q16 code is at most T.
