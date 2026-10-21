# Argument contract

> Generated from `spec/cli-contract.json` and `spec/method-contract.json` by `tools/generate_spec.py`. Edit those authorities instead.

## Commands

- `root` — `docenhance [--help] [--json] | docenhance --version`
- `process` — `docenhance process INPUT --out-dir DIRECTORY [OPTIONS]`
  Process one PNG, JPEG or single-page TIFF/BigTIFF page and publish a new bundle directory.
- `verify` — `docenhance verify DIRECTORY [--json]`
  Check that a bundle's artifacts agree with its run record without executing anything.
- `methods` — `docenhance methods [ID] [--json]`
  List the implemented methods and their parameter domains.
- `version` — `docenhance version [--json]`
  Report the build, platform, compiler and dependency-lock identity.

## Input support

- `png`: `preserve`, `gray`, `bw`.
- `jpeg`: `preserve`, `gray`, `bw`.
- `tiff`: `preserve`, `gray`, `bw`.

## Grammar

**Tokens.** docenhance [--help] [--json] | docenhance --version (no arguments renders help, exit 0) | docenhance COMMAND [OPERAND|OPTION]... . Only tokens beginning with '--' name options; '-', '-h' and '-input.png' are operands. OPTION is --NAME VALUE, --NAME=VALUE or a flag --NAME. A separated valued option always consumes the next token, including '--'; joined empty values stay empty and consume no later token. Flags given '=VALUE', unknown/repeated options, missing option values and extra operands are syntax errors. An unconsumed '--' ends options; later tokens are operands. Missing required operands and invalid option values belong to application admission, not tokenization. Only on syntax failure, an exact raw '--json' token before the first raw '--' selects JSON error rendering. Successful syntax uses parsed flag roles, including application refusals. Arguments must be well-formed UTF-8.

**Decimal.** Finite decimal: optional leading '-', digits with an optional decimal point or a point followed by digits, and an optional exponent [eE][+-]?digits. No leading '+', whitespace, hexadecimal, infinity or NaN. Overflow, subnormal results and non-zero magnitudes rounded to zero are rejected; '.' in every locale.

**Integer.** ASCII decimal digits only (leading zeros allowed); no sign, whitespace, fraction or exponent.

**Grid.** Two integers joined by a lower-case 'x', for example 8x8.

**Path.** Non-empty well-formed UTF-8 without NUL; never normalized or repaired.

## `--out-dir DIRECTORY`

**Scope:** `process`.

**Domain/default:** required; well-formed UTF-8 path.

New result directory; its parent must exist and it is never replaced.

Explicit presence is required when this option applies.

## `--output-mode MODE`

**Scope:** `process`.

**Domain/default:** preserve; preserve | gray | bw.

Preserve keeps the decoded gray/color category, not source bytes, ICC space, metadata or alpha. All modes share interpretation, geometry and the fixed photometric stages. Gray quantizes final linear luminance; bw classifies terminal sRGB Q16 samples and writes physical 1-bit PNG.

## `--bit-depth DEPTH`

**Scope:** `process`.

**Domain/default:** auto; auto | 8 | 16.

Continuous output depth; auto keeps 16-bit source precision, otherwise 8.

**Output modes:** preserve, gray.

## `--alpha MODE`

**Scope:** `process`.

**Domain/default:** white; white | black | reject.

Composite alpha over white or black in linear light, or reject any non-opaque pixel.

**Output modes:** preserve, gray, bw.

## `--profile-policy POLICY`

**Scope:** `process`.

**Domain/default:** embedded; embedded | srgb.

Interpret supported colour declarations, or override them with sRGB.

**Output modes:** preserve, gray, bw.

## `--rotate DEGREES`

**Scope:** `process`.

**Domain/default:** 0; 0 | 90 | 180 | 270.

Exact clockwise quarter-turn after metadata orientation, in every output mode.

## `--protect-mask PATH`

**Scope:** `process`.

**Domain/default:** optional; well-formed UTF-8 path.

1/8-bit gray PNG in metadata-oriented source dimensions; non-zero samples protect photometric stages. Binarization still classifies every pixel.

**Output modes:** preserve, gray, bw.

## `--threads N`

**Scope:** `process`.

**Domain/default:** auto; auto | integer [1,256].

Maximum scheduler workers; auto uses detected concurrency bounded to 1..256. Execution settings are response diagnostics, not persistent method configuration.

**Automatic resolution:** detected concurrency clamped to the admitted worker domain; hardware zero uses its minimum.

## `--memory-limit MIB`

**Scope:** `process`.

**Domain/default:** 1024; integer [64,262144].

One charged processing budget in MiB for snapshots, interpretation, methods, encoding and publication. Charged bytes are not a process-RSS limit; verification retains its separate fixed budget.

## `--json`

**Scope:** `root`, `process`, `verify`, `methods`, `version`.

**Domain/default:** optional; flag.

Render one JSON response on stdout and nothing on stderr.

## `--help`

**Scope:** `root`, `process`, `verify`, `methods`, `version`.

**Domain/default:** optional; flag.

Render help without opening input or creating output.

## `--version`

**Scope:** `root`.

**Domain/default:** optional; flag.

Human-readable alias of the version command.

## `--illumination MODE`

**Scope:** `process`.

**Domain/default:** off; off | surface | auto | morph.

Select illumination

**Applicable methods:** I01, I02.

**Output modes:** preserve, gray, bw.

## `--background-strength A`

**Scope:** `process`.

**Domain/default:** 1; finite [0,1].

Exponent applied to the illumination gain; 0 is an exact no-op.

**Applicable methods:** I01, I02.

**Output modes:** preserve, gray, bw.

## `--background-max-gain G`

**Scope:** `process`.

**Domain/default:** 2; finite [1,4].

Largest multiplicative gain applied to any sample; 1 is an exact no-op.

**Applicable methods:** I01, I02.

**Output modes:** preserve, gray, bw.

## `--background-target TARGET`

**Scope:** `process`.

**Domain/default:** source; source | finite [0.1,1].

Linear-light target; 'source' uses the fitted background's nearest-rank 90th percentile.

**Applicable methods:** I01, I02.

**Output modes:** preserve, gray, bw.

## `--background-cell SIZE`

**Scope:** `process`.

**Domain/default:** auto; auto | integer [8,512].

Cell edge in processing pixels.

**Automatic resolution:** round(min(W,H)/24) clamped to [16,256].

**Applicable methods:** I01.

**Output modes:** preserve, gray, bw.

## `--background-quantile Q`

**Scope:** `process`.

**Domain/default:** 0.90; finite [0.75,0.99].

Nearest-rank quantile of eligible cell samples.

**Applicable methods:** I01.

**Output modes:** preserve, gray, bw.

## `--background-smooth BETA`

**Scope:** `process`.

**Domain/default:** 1; finite [0.1,20].

Grid-Laplacian weight of the logarithmic background fit.

**Applicable methods:** I01.

**Output modes:** preserve, gray, bw.

## `--background-radius RADIUS`

**Scope:** `process`.

**Domain/default:** auto; auto | integer [1,256].

Square closing radius in processing pixels; Gaussian sigma = max(0.5, radius/2).

**Automatic resolution:** round(min(W,H)/50) clamped to [8,128].

**Applicable methods:** I02.

**Output modes:** preserve, gray, bw.

## `--denoise METHOD`

**Scope:** `process`.

**Domain/default:** off; off | nlm | tvl1.

Select denoising

**Applicable methods:** D01, D02.

**Output modes:** preserve, gray, bw.

## `--denoise-blend A`

**Scope:** `process`.

**Domain/default:** 0.5; finite [0,1].

Blend in perceptual luminance; 0 keeps entering samples.

**Applicable methods:** D01, D02.

**Output modes:** preserve, gray, bw.

## `--nlm-h H`

**Scope:** `process`.

**Domain/default:** 3; finite [0.1,25].

Filter strength in equivalent 8-bit perceptual points; higher values may remove marks.

**Applicable methods:** D01.

**Output modes:** preserve, gray, bw.

## `--nlm-patch PIXELS`

**Scope:** `process`.

**Domain/default:** 7; odd integer [3,15].

Square patch width.

**Applicable methods:** D01.

**Output modes:** preserve, gray, bw.

## `--nlm-search PIXELS`

**Scope:** `process`.

**Domain/default:** 21; odd integer [7,41].

Square search width; at least the patch width.

**Applicable methods:** D01.

**Output modes:** preserve, gray, bw.

## `--tv-lambda LAMBDA`

**Scope:** `process`.

**Domain/default:** 1.5; finite [0.05,20].

L1 fidelity weight; larger values smooth less.

**Applicable methods:** D02.

**Output modes:** preserve, gray, bw.

## `--tv-iterations N`

**Scope:** `process`.

**Domain/default:** 150; integer [10,1000].

Iteration cap; exhaustion keeps the iterate and warns.

**Applicable methods:** D02.

**Output modes:** preserve, gray, bw.

## `--tv-tolerance EPS`

**Scope:** `process`.

**Domain/default:** 1e-5; finite [1e-8,1e-3].

Primal and dual update tolerance at two consecutive checkpoints.

**Applicable methods:** D02.

**Output modes:** preserve, gray, bw.

## `--deblur METHOD`

**Scope:** `process`.

**Domain/default:** off; off | wiener.

Select restoration

**Applicable methods:** R01.

**Output modes:** preserve, gray, bw.

## `--psf KIND`

**Scope:** `process`.

**Domain/default:** required; gaussian | motion | kernel.

Point-spread function model in the processing frame.

Explicit presence is required when this option applies.

**Applicable methods:** R01.

**Output modes:** preserve, gray, bw.

## `--psf-sigma SIGMA`

**Scope:** `process`.

**Domain/default:** 1; finite [0.3,5].

Gaussian standard deviation in pixels.

**Requires:** `--psf=gaussian`.

**Applicable methods:** R01.

**Output modes:** preserve, gray, bw.

## `--psf-length LENGTH`

**Scope:** `process`.

**Domain/default:** 5; finite [1,31].

Motion segment length in pixels.

**Requires:** `--psf=motion`.

**Applicable methods:** R01.

**Output modes:** preserve, gray, bw.

## `--psf-angle ANGLE`

**Scope:** `process`.

**Domain/default:** 0; finite [-180,180].

Motion direction, degrees clockwise from +x in y-down coordinates.

**Requires:** `--psf=motion`.

**Applicable methods:** R01.

**Output modes:** preserve, gray, bw.

## `--psf-file PATH`

**Scope:** `process`.

**Domain/default:** required; well-formed UTF-8 path.

Grayscale PNG of raw non-negative coefficients, odd sides 3..129.

**Requires:** `--psf=kernel`.

Explicit presence is required when this option applies.

**Applicable methods:** R01.

**Output modes:** preserve, gray, bw.

## `--wiener-k K`

**Scope:** `process`.

**Domain/default:** 0.01; finite [1e-5,1].

Positive regularizer in |H|^2+K; not a measured noise ratio.

**Applicable methods:** R01.

**Output modes:** preserve, gray, bw.

## `--deblur-blend A`

**Scope:** `process`.

**Domain/default:** 0.5; finite [0,1].

Blend of entering and restored linear luminance before clamping.

**Applicable methods:** R01.

**Output modes:** preserve, gray, bw.

## `--contrast METHOD`

**Scope:** `process`.

**Domain/default:** off; off | levels | gamma | clahe.

Select contrast

**Applicable methods:** C01, C02, C03.

**Output modes:** preserve, gray, bw.

## `--contrast-blend A`

**Scope:** `process`.

**Domain/default:** 1; finite [0,1].

Blend in perceptual luminance; 0 keeps entering samples.

**Applicable methods:** C01, C02, C03.

**Output modes:** preserve, gray, bw.

## `--levels-low P`

**Scope:** `process`.

**Domain/default:** 0.5; finite [0,10].

Nearest-rank lower percentile of unprotected perceptual samples.

**Applicable methods:** C01.

**Output modes:** preserve, gray, bw.

## `--levels-high P`

**Scope:** `process`.

**Domain/default:** 99.5; finite [90,100].

Nearest-rank upper percentile; greater than the lower one.

**Applicable methods:** C01.

**Output modes:** preserve, gray, bw.

## `--gamma G`

**Scope:** `process`.

**Domain/default:** 1.2; finite [0.25,4].

f' = f^G; above 1 darkens midtones; 1 is an exact identity.

**Applicable methods:** C02.

**Output modes:** preserve, gray, bw.

## `--clahe-grid CxR`

**Scope:** `process`.

**Domain/default:** 8x8; CxR, each integer [2,32].

Contextual columns x rows; every tile at least 16 pixels per side.

**Applicable methods:** C03.

**Output modes:** preserve, gray, bw.

## `--clahe-clip C`

**Scope:** `process`.

**Domain/default:** 2; finite [1,8].

Pre-redistribution clip multiplier relative to mean occupancy.

**Applicable methods:** C03.

**Output modes:** preserve, gray, bw.

## `--sharpen METHOD`

**Scope:** `process`.

**Domain/default:** off; off | unsharp.

Select sharpening

**Applicable methods:** S01.

**Output modes:** preserve, gray, bw.

## `--sharpen-sigma S`

**Scope:** `process`.

**Domain/default:** 0.8; finite [0.3,3].

Gaussian standard deviation in pixels.

**Applicable methods:** S01.

**Output modes:** preserve, gray, bw.

## `--sharpen-amount A`

**Scope:** `process`.

**Domain/default:** 0.5; finite [0,2].

Soft-thresholded high-pass gain; 0 is an exact identity.

**Applicable methods:** S01.

**Output modes:** preserve, gray, bw.

## `--sharpen-threshold T`

**Scope:** `process`.

**Domain/default:** 1; finite [0,20].

Soft threshold in equivalent 8-bit perceptual points.

**Applicable methods:** S01.

**Output modes:** preserve, gray, bw.

## `--binarize METHOD`

**Scope:** `process`.

**Domain/default:** sauvola; otsu | sauvola | fixed.

Select binarization

**Applicable methods:** B01, B02, B03.

**Output modes:** bw.

## `--sauvola-window PIXELS`

**Scope:** `process`.

**Domain/default:** 31; odd integer [3,4095].

Centred square window with REFLECT_101 borders.

**Applicable methods:** B02.

**Output modes:** bw.

## `--sauvola-k K`

**Scope:** `process`.

**Domain/default:** 0.2; finite [0,1].

Weight in T = m*(1 + k*(s/R - 1)).

**Applicable methods:** B02.

**Output modes:** bw.

## `--sauvola-r R`

**Scope:** `process`.

**Domain/default:** 0.5; finite [1/255,1].

Normalized deviation scale.

**Applicable methods:** B02.

**Output modes:** bw.

## `--fixed-threshold T`

**Scope:** `process`.

**Domain/default:** 0.5; finite [0,1].

Black iff the normalized terminal Q16 code is at most T.

**Applicable methods:** B03.

**Output modes:** bw.
