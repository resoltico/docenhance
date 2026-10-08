# Methods

> Generated from `spec/method-contract.json` by `tools/generate_spec.py`. Only methods marked **implemented** appear in `docenhance methods`; every other entry remains a reviewed design target until its contract, tests, and fixtures exist.

## I01 — Quantile log-surface illumination

Status: **implemented**.

Method-specific argument references: `--illumination`, `--background-strength`, `--background-max-gain`, `--background-target`, `--background-cell`, `--background-quantile`, `--background-smooth`, `--protect-mask`.

## I02 — Morphological illumination

Status: **implemented**.

Method-specific argument references: `--illumination`, `--background-strength`, `--background-max-gain`, `--background-target`, `--background-radius`, `--protect-mask`.

## D01 — 16-bit NLM-L1

Status: **implemented**.

Method-specific argument references: `--denoise`, `--denoise-blend`, `--nlm-h`, `--nlm-patch`, `--nlm-search`, `--protect-mask`.

## D02 — Floating-point TV-L1

Status: **implemented**.

Method-specific argument references: `--denoise`, `--denoise-blend`, `--tv-lambda`, `--tv-iterations`, `--tv-tolerance`, `--protect-mask`.

## R01 — Known-PSF regularized Fourier restoration

Status: **not-implemented**.

Method-specific argument references: `--deblur`, `--psf`, `--wiener-k`, `--deblur-blend`.

## C01 — Percentile levels

Status: **implemented**.

Method-specific argument references: `--contrast`, `--contrast-blend`, `--levels-low`, `--levels-high`, `--protect-mask`.

## C02 — Gamma

Status: **implemented**.

Method-specific argument references: `--contrast`, `--contrast-blend`, `--gamma`, `--protect-mask`.

## C03 — Masked floating-point CLAHE

Status: **implemented**.

Method-specific argument references: `--contrast`, `--contrast-blend`, `--clahe-grid`, `--clahe-clip`, `--protect-mask`.

## S01 — Thresholded unsharp masking

Status: **implemented**.

Method-specific argument references: `--sharpen`, `--sharpen-sigma`, `--sharpen-amount`, `--sharpen-threshold`, `--protect-mask`.

## B02 — Sauvola

Status: **implemented**.

Method-specific argument references: `--binarize`, `--sauvola-window`, `--sauvola-k`, `--sauvola-r`.

## B03 — Fixed threshold

Status: **implemented**.

Method-specific argument references: `--binarize`, `--fixed-threshold`.

## B01 — Otsu

Status: **implemented**.

Method-specific argument references: `--binarize`.

## G01 — Metadata orientation

Status: **not-implemented**.

Method-specific argument references: See geometry/common options in the target contract.

## G02 — Exact quarter-turn

Status: **not-implemented**.

Method-specific argument references: `--rotate`.

## G03 — Manual perspective

Status: **not-implemented**.

Method-specific argument references: `--quad`, `--page-size`.

## G04 — Deskew

Status: **not-implemented**.

Method-specific argument references: `--deskew`, `--deskew-limit`.

## G05 — Vertical text-line dewarp

Status: **not-implemented**.

Method-specific argument references: `--dewarp`, `--dewarp-min-lines`, `--dewarp-max-displacement`.
