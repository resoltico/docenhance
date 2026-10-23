# Current CLI behavior

Published bundles can be read back:

```sh
docenhance verify RESULT
docenhance verify RESULT --json
```

Verification checks that a bundle holds exactly what its record declares, after the directory has
been moved if need be. It is read-only, executes nothing it finds, and establishes agreement rather
than authenticity; see [processing bundles](record.md).

## Implemented operations

`docenhance process` defaults to continuous-tone PNG representation with no enhancement filter:

```sh
docenhance process INPUT.png --out-dir RESULT
docenhance process INPUT.png --out-dir GRAY --output-mode gray --bit-depth 16
docenhance process INPUT.png --out-dir TURNED --rotate 90
docenhance process INPUT.png --out-dir BINARY --output-mode bw --binarize sauvola
docenhance process INPUT.png --out-dir OTSU --output-mode bw --binarize otsu
docenhance process INPUT.png --out-dir FIXED --output-mode bw --binarize fixed --fixed-threshold 0.5
```

`preserve` keeps the decoded color/gray category, not file bytes, original profile, alpha or arbitrary
metadata. Static PNG supports gray/palette/RGB/alpha layouts and 8/16-bit continuous output under
[PNG processing](formats/png.md). Each result is published as a
[processing bundle](record.md): the image, a `run.json` recording what produced it, and the
canonical protection mask when supplied and the exact supplied PSF asset when selected. Profile
assumptions, alpha flattening, requested continuous depth reduction, orientation and verified output
descriptors are reported. Bounded 8-bit baseline/progressive Huffman JPEG and bounded single-page
TIFF/BigTIFF support preserve, gray and binary output through the same fixed pipeline, within their
respective [JPEG](formats/jpeg.md) and [TIFF](formats/tiff.md) source-admission contracts.
Animation, multipage input, recipes, presets and enhancement methods other than I01/I02/D01/D02/R01/C01/C02/C03/S01/G02 remain unsupported.

Only explicit `bw` activates a binarizer (Sauvola by default). All output modes share source
interpretation, G01 metadata orientation, G02 quarter-turns, protection and the selected photometric
stages. Binary classification consumes final luminance after one terminal sRGB Q16 quantization,
then writes logical black0/white1 directly as physical grayscale PNG1. Every pixel is classified,
including protected photometric samples. Parameters, equality and scale belong to
[typed binarization](methods/binarization.md). `--bit-depth` applies only to preserve/gray; alpha and profile
policies apply to every output mode.
Wrong-method, wrong-operation and explicitly empty values fail admission before input I/O.

`RESULT` must not exist and its parent must already be a directory. The writer creates exclusively
owned sibling staging and atomically publishes without replacement after encoding and closing
succeed; binary and continuous output are independently verified before commit. Publication uncertainty is not hidden; see [architecture](architecture.md).

`docenhance methods` lists implemented processing methods with names, families, selectors and
parameter domains. `methods ID` selects one entry. Metadata orientation and exact quarter-turns
are common geometry operations, rather than selectable catalog entries.
`version --json` reports the complete executable method list and `png`/`jpeg`/`tiff` as input formats.
Help, version and capability discovery do not invoke the image-processing host. Human help
shows reviewed defaults and value domains alongside each option. The common pipeline is
interpretation and geometry, illumination, denoising, restoration, contrast, sharpening, then
continuous quantization or terminal binary classification; enhancement stages remain opt-in.

## Processing execution controls

`--threads auto|N` is process-only, defaults to automatic hardware detection and admits decimal
integers 1 through 256. `--memory-limit MIB` is process-only, defaults to 1024 and admits integers
64 through 262144. The ordinary integer grammar permits leading zeroes, but no sign, fraction,
exponent or whitespace. The scheduler uses the requested worker bound and available working memory;
automatic hardware detection has a fallback of one and a maximum of 256. Image work can use fewer
active workers than that bound. Foreign Fourier transforms remain internally serial.

These are per-call controls, outside the persistent method plan. Response `diagnostics` reports the
resolved worker bound, actual selected memory limit and charged high water. Neither setting enters
`run.json`. One processing budget covers snapshots, interpretation, kernels, encoding and publication;
it is not a process-RSS limit. Verification does not accept these options and retains its independent
fixed 1 GiB snapshot budget. A larger budget does not enlarge encoded-file or pixel admission.

## Optional illumination and protection

```sh
docenhance process INPUT.png --out-dir RESULT --illumination surface
docenhance process INPUT.png --out-dir RESULT --illumination auto --protect-mask PROTECT.png
```

The default remains `--illumination off`. `surface` attempts I01; `auto` uses its six explicit
applicability predicates. Neither is a preset or paper classifier. Background strength, maximum
gain, source-relative/numeric target, cell size, quantile and smoothing are validated method values.
See [illumination](methods/illumination.md#i01-surface-illumination) for defaults, equations, protection and exact failure rules.
Illumination and masks are admitted for every output mode. Wrong-method or explicitly empty values
are errors. A supplied mask is decoded and checked even with off/zero-strength processing; it protects
photometric samples and analysis but does not exempt destinations from terminal classification.

## Optional denoising

```sh
docenhance process INPUT.jpg --out-dir RESULT --denoise nlm
```

D01 (`nlm`) and D02 (`tvl1`) run after the selected illumination method and before final
quantization, with exact protection of masked photometric destinations. Both are opt-in for every
output mode; see [denoising](methods/denoising.md#d01-bounded-nlm) and [TV-L1 denoising](methods/denoising.md#d02-floating-point-tv-l1) for parameters,
resource accounting, stopping diagnostics and cancellation.

## Optional restoration

```sh
docenhance process INPUT.png --out-dir RESTORED --deblur wiener --psf gaussian
```

R01 is explicit known-PSF Fourier restoration after denoising and before contrast. The supplied
PSF is an unverified model, even for an identity output; see [restoration](methods/restoration.md).
It defaults to off and can precede any output mode. No automatic blur estimation is implied.
With `--psf kernel --psf-file PATH`, the original bounded raw coefficient PNG is retained unchanged
as a confined bundle asset; its original path and coefficient vector are absent from the record.

## Optional contrast

```sh
docenhance process INPUT.png --out-dir LEVELS --contrast levels
docenhance process INPUT.png --out-dir GAMMA --contrast gamma --gamma 1.2
docenhance process INPUT.png --out-dir CLAHE --contrast clahe --clahe-grid 8x8 --clahe-clip 2
```

Contrast follows illumination, denoising and optional restoration. It defaults to off; all alternatives
can precede every output mode and preserve protected entering photometric values. Preserve-color
output reconstructs color from the final luminance through the shared color transport. Levels fits every eligible sample; gamma applies f^G with exact endpoints. See
[contrast](methods/contrast.md) for percentile units, clipping, identities and resources.

## Optional sharpening

```sh
docenhance process INPUT.png --out-dir SHARPENED --sharpen unsharp
```

S01 follows contrast before final quantization/classification. It defaults to off and can precede
every output mode.
The float64 REFLECT_101 Gaussian context includes protected neighbors while protected destinations
remain exact. Reports include eligible pre-clamp excursions and clipping; every positive requested
amount emits `W_SHARPENING` once S01 is entered or completes; an unstarted selection does not
fabricate a warning observation. See [sharpening](methods/sharpening.md) for parameters, soft threshold,
resources and the limits of increased edge contrast.

## Responses and failures

With `--json`, every processing success contains the full actual `record` value, its separate
`record_sha256`, the final output path, `publication: completed`, execution `diagnostics` and derived
`warnings`. The record separates the admitted request from immutable actual completed Facts; disabled
families are null. Failure contains an explicit publication state, optional `failed_stage`, sparse
`completed` earlier Facts, nullable executor diagnostics and warnings derived from available runtime
context. That private context is never serialized. See [processing bundles](record.md) for the
current version and closed shapes. Actual C++ Fields own the wire contract; native emission produces
[command-response.schema.json](../schemas/command-response.schema.json). Schema authoring and
qualification use those same owners.

Invalid syntax/parameters use `E_ARGUMENT`, exit 2; invalid or unsupported input uses `E_INPUT`,
exit 3; resource refusal uses `E_RESOURCE`, exit 4; a known output refusal uses `E_OUTPUT`, exit 5. Failed encoded-output verification uses
`E_OUTPUT_VERIFY`, also exit 5: before commit it prevents publication; after known commit success
it retains `publication: completed` and the published directory. Inspect the reported state.
`E_METHOD_INAPPLICABLE` and `E_NUMERICAL` use exit 4 and retain available earlier completed Facts
and a failed family when one was observed. `methods ID` outside the implemented catalog uses
`E_NOT_IMPLEMENTED`, exit 4, with `not_started` publication. Unsupported JPEG coding, sampling, color and metadata within
its source-admission contract use `E_INPUT`, exit 3, independently of output mode. Publication states distinguish
`not_started`, `not_published`, `completed` and `unknown`. An unreported processing effect, ambiguous commit
or unconfirmed staging cleanup uses `E_PUBLICATION_UNKNOWN`, exit 7, with state `unknown`.

Every human error states the typed publication state. `not_started` means publication did not start (preparation may already have run);
`not_published` means no result was published. Correct the problem before retrying either case.
For `completed`, inspect the destination and verify its bundle before further action; for `unknown`,
inspect destination and staging entries. Neither state authorizes a blind retry. Human warnings
explain color assumptions/overrides, irreversible alpha/precision changes, TV-L1 stopping and
sharpening excursions while retaining the machine codes and observed values.

Human method summaries use the record field names and show completed stages in pipeline order.
Nested observations retain their values, including zero and false; unavailable optional observations
read `not reported`. An absent stage makes no completion claim. Automated consumers use the
versioned JSON response.

JSON goes only to stdout and text errors only to stderr. Rendering and unformatted delivery occur
once, followed by an explicit flush of the selected stream. An explicit rendering refusal is checked
before either stream is touched and returns exit 5 without a second outcome, response or render attempt.
The retained execution outcome and publication state do not change. The response's `exit_code` describes
the command outcome; a subsequent delivery failure can instead produce process exit 5. Neither
absence of a complete response nor exit 5 proves that publication did not happen. Inspect both
response and process status; do not retry blindly. Flushing is not acknowledgement by a consumer.

CLI tokens and admitted paths must be well-formed UTF-8, with no embedded path NUL. Identity bytes
are not normalized or repaired. Invalid diagnostic text alone gets an explanatory fallback.
String option values retain their token role: `--out-dir --json` and `--out-dir=--json` both
name the literal directory `--json` and produce human text. Add a separately parsed `--json`
flag to select JSON. Flags with `=VALUE`, including an empty value, are syntax errors.
A joined empty string value remains empty: `--out-dir= --json` leaves the JSON flag separate
and fails output-path admission. A separated valued option always consumes its next token,
including `--`. An unconsumed `--` ends option parsing. Single-dash tokens such as `-`, `-h`
and `-input.png` are ordinary operands, with no short-option or stdin meaning.
Only syntax failure uses the exact `--json` token before `--` as a diagnostic fallback, even if
it might have been intended as a value. Syntax failures never start processing. Missing required operands are application refusals after
successful tokenization, so they use parsed JSON roles. Root flags cannot precede a command.

The `tests/cli/test_sauvola.py` in the source distribution cover admission, decoding, method
execution, published samples, selected discovery and response conformance together.

## Interrupting processing

SIGINT/SIGTERM on POSIX and CTRL_C/CTRL_BREAK on Windows request cooperative cancellation.
A confirmed cancellation produces `E_CANCELLED` and process exit 130. Valid requests cancelled
before execution do no input/output work. The last precommit checkpoint is a cutoff: a late request
cannot turn successful publication into cancellation. Unknown publication or cleanup retains exit 7;
response delivery can still fail with exit 5. Repeated handled interrupts do not force termination.
See [cancellation](architecture.md#cooperative-cancellation) for precise outcomes, signal safety and blocking-I/O limits.

Explicit `--rotate 0|90|180|270` is clockwise after metadata orientation for every output mode. Masks are supplied before the user turn in frame B and transformed
identically for processing. See [geometry](pipeline.md) for coordinate frames, physical resolution
and unsupported perspective/deskew/dewarp boundaries.
