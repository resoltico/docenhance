# Methods

> Generated from `spec/method-contract.json` by `tools/generate_spec.py`. Only methods marked **implemented** appear in `docenhance methods`; every other entry remains a reviewed design target until its contract, tests, and fixtures exist.

## I01 — Quantile log-surface illumination

Status: **not-implemented**.

Method-specific argument references: `--illumination`, `--background-strength`, `--background-max-gain`, `--background-target`, `--background-cell`, `--background-quantile`, `--background-smooth`.

## I02 — Morphological illumination

Status: **not-implemented**.

Method-specific argument references: `--illumination`, `--background-strength`, `--background-max-gain`, `--background-target`, `--background-radius`.

## D01 — 16-bit NLM-L1

Status: **not-implemented**.

Method-specific argument references: `--denoise`, `--denoise-blend`, `--nlm-h`, `--nlm-patch`, `--nlm-search`.

## D02 — Floating-point TV-L1

Status: **not-implemented**.

Method-specific argument references: `--denoise`, `--denoise-blend`, `--tv-lambda`, `--tv-iterations`, `--tv-tolerance`.

## R01 — Known-PSF regularized Fourier restoration

Status: **not-implemented**.

Method-specific argument references: `--deblur`, `--psf`, `--wiener-k`, `--deblur-blend`.

## C01 — Percentile levels

Status: **not-implemented**.

Method-specific argument references: `--contrast`, `--contrast-blend`, `--levels-low`, `--levels-high`.

## C02 — Gamma

Status: **not-implemented**.

Method-specific argument references: `--contrast`, `--contrast-blend`, `--gamma`.

## C03 — Masked floating-point CLAHE

Status: **not-implemented**.

Method-specific argument references: `--contrast`, `--contrast-blend`, `--clahe-grid`, `--clahe-clip`.

## S01 — Thresholded unsharp masking

Status: **not-implemented**.

Method-specific argument references: `--sharpen`, `--sharpen-sigma`, `--sharpen-amount`, `--sharpen-threshold`.

## B01 — Otsu

Status: **not-implemented**.

Method-specific argument references: `--binarize`.

## B02 — Sauvola

Status: **implemented**.

Method-specific argument references: `--binarize`, `--sauvola-window`, `--sauvola-k`, `--sauvola-r`.

## B03 — Fixed threshold

Status: **implemented**.

Method-specific argument references: `--binarize`, `--fixed-threshold`.

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
