# Methods

> Generated from `spec/method-contract.json` by `tools/generate_spec.py`. Only **implemented-selectable** methods appear in `docenhance methods`. **Implemented-automatic** capabilities run as built-in behavior without a selector. **Planned** methods require complete contracts, tests and fixtures.

## I01 — Quantile log-surface illumination

Classification: **implemented-selectable**.

Method-specific argument references: `--illumination`, `--background-strength`, `--background-max-gain`, `--background-target`, `--background-cell`, `--background-quantile`, `--background-smooth`, `--protect-mask`.

## I02 — Morphological illumination

Classification: **implemented-selectable**.

Method-specific argument references: `--illumination`, `--background-strength`, `--background-max-gain`, `--background-target`, `--background-radius`, `--protect-mask`.

## D01 — 16-bit NLM-L1

Classification: **implemented-selectable**.

Method-specific argument references: `--denoise`, `--denoise-blend`, `--nlm-h`, `--nlm-patch`, `--nlm-search`, `--protect-mask`.

## D02 — Floating-point TV-L1

Classification: **implemented-selectable**.

Method-specific argument references: `--denoise`, `--denoise-blend`, `--tv-lambda`, `--tv-iterations`, `--tv-tolerance`, `--protect-mask`.

## C01 — Percentile levels

Classification: **implemented-selectable**.

Method-specific argument references: `--contrast`, `--contrast-blend`, `--levels-low`, `--levels-high`, `--protect-mask`.

## C02 — Gamma

Classification: **implemented-selectable**.

Method-specific argument references: `--contrast`, `--contrast-blend`, `--gamma`, `--protect-mask`.

## C03 — Masked floating-point CLAHE

Classification: **implemented-selectable**.

Method-specific argument references: `--contrast`, `--contrast-blend`, `--clahe-grid`, `--clahe-clip`, `--protect-mask`.

## S01 — Thresholded unsharp masking

Classification: **implemented-selectable**.

Method-specific argument references: `--sharpen`, `--sharpen-sigma`, `--sharpen-amount`, `--sharpen-threshold`, `--protect-mask`.

## R01 — Known-PSF regularized Fourier restoration

Classification: **implemented-selectable**.

Method-specific argument references: `--deblur`, `--psf`, `--psf-sigma`, `--psf-length`, `--psf-angle`, `--psf-file`, `--wiener-k`, `--deblur-blend`, `--protect-mask`.

## B02 — Sauvola

Classification: **implemented-selectable**.

Method-specific argument references: `--binarize`, `--sauvola-window`, `--sauvola-k`, `--sauvola-r`.

## B03 — Fixed threshold

Classification: **implemented-selectable**.

Method-specific argument references: `--binarize`, `--fixed-threshold`.

## B01 — Otsu

Classification: **implemented-selectable**.

Method-specific argument references: `--binarize`.

## G01 — Metadata orientation

Classification: **implemented-automatic**.

Automatic continuous metadata orientation; binary output ignores orientation.
No standalone selector or method-specific arguments.

## G02 — Exact quarter-turn

Classification: **implemented-selectable**.

Method-specific argument references: `--rotate`.

## G03 — Manual perspective

Classification: **planned**.

Method-specific argument references: `--quad`, `--page-size`.

## G04 — Deskew

Classification: **planned**.

Method-specific argument references: `--deskew`, `--deskew-limit`.

## G05 — Vertical text-line dewarp

Classification: **planned**.

Method-specific argument references: `--dewarp`, `--dewarp-min-lines`, `--dewarp-max-displacement`.
