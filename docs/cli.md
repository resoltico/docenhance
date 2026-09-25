# Current CLI behavior

## Implemented operations

`docenhance process` defaults to continuous-tone PNG representation with no enhancement filter:

```sh
docenhance process INPUT.png --out-dir RESULT
docenhance process INPUT.png --out-dir GRAY --output-mode gray --bit-depth 16
docenhance process INPUT.png --out-dir BINARY --output-mode bw --binarize sauvola
docenhance process INPUT.png --out-dir FIXED --output-mode bw --binarize fixed --fixed-threshold 0.5
```

`preserve` keeps the decoded color/gray category, not file bytes, original profile, alpha or arbitrary
metadata. Static PNG supports gray/palette/RGB/alpha layouts and 8/16-bit continuous output under
[PNG processing](png-processing.md). Profile assumptions, alpha flattening, requested depth reduction,
orientation and verified output descriptors are reported. JPEG, TIFF, animation, recipes, presets
and enhancement methods other than I01 remain unsupported.

Only explicit `bw` activates a binarizer (Sauvola by default). B02/B03 retain the separate published
1/2/4/8-bit grayscale-without-transparency input contract and 8-bit binary output. They do not run
through color conversion. Parameters, equality and scale remain in [typed binarization](binarization.md).
Wrong-method, wrong-operation and explicitly empty values fail admission before input I/O.

`RESULT` must not exist and its parent must already be a directory. The writer creates exclusively
owned sibling staging and atomically publishes without replacement after encoding and closing
succeed; continuous output is independently verified before commit. Publication uncertainty is not hidden; see [architecture](architecture.md).

`docenhance methods` reports I01, B02 and B03. `methods I01`, `methods B02` or `methods B03` selects one entry.
`version --json` reports the complete executable method list and only `png` as an input format.
Help, version and capability discovery do not invoke the image-processing host.

## Optional illumination and protection

```sh
docenhance process INPUT.png --out-dir RESULT --illumination surface
docenhance process INPUT.png --out-dir RESULT --illumination auto --protect-mask PROTECT.png
```

The default remains `--illumination off`. `surface` attempts I01; `auto` uses its six explicit
applicability predicates. Neither is a preset or paper classifier. Background strength, maximum
gain, source-relative/numeric target, cell size, quantile and smoothing are validated method values.
See [illumination](illumination.md) for defaults, equations, protection and exact failure rules.
Illumination and masks require continuous output; wrong options with `bw` or an explicitly empty
value are errors. A supplied mask is decoded and checked even with off/zero-strength processing.

## Responses and failures

With `--json`, binary success contains the admitted `method` and `method_version`. Continuous
success instead contains `operation: continuous`, a typed `conversion` record and an
`illumination` stage record (including disabled), with no fictional conversion-method ID. Both identify the final output path and `publication: completed`. The delivered machine contract is
[command-response.schema.json](../schemas/command-response.schema.json); edit the authoring
schema and method catalog under `spec/`, then regenerate, rather than editing generated output.

Invalid syntax/parameters use `E_ARGUMENT`, exit 2; invalid or unsupported input uses `E_INPUT`,
exit 3; resource refusal uses `E_RESOURCE`, exit 4; a known output refusal uses `E_OUTPUT`, exit 5. Failed encoded-output verification uses
`E_OUTPUT_VERIFY`, also exit 5, without committing output.
`E_METHOD_INAPPLICABLE` and `E_NUMERICAL` use exit 4 and retain available I01 diagnostics.
An unimplemented method ID uses `E_NOT_IMPLEMENTED`, exit 4. Publication states distinguish
`not_started`, `not_published` and `completed`. An unreported processing effect, ambiguous commit
or unconfirmed staging cleanup uses `E_PUBLICATION_UNKNOWN`, exit 7, with state `unknown`.

JSON goes only to stdout and text errors only to stderr. Rendering and unformatted delivery occur
once, followed by an explicit flush of the selected stream. The response's `exit_code` describes
the command outcome; a subsequent delivery failure can instead produce process exit 5. Neither
absence of a complete response nor exit 5 proves that publication did not happen. Inspect both
response and process status; do not retry blindly. Flushing is not acknowledgement by a consumer.

CLI tokens and admitted paths must be well-formed UTF-8, with no embedded path NUL. Identity bytes
are not normalized or repaired. Invalid diagnostic text alone gets an explanatory fallback.
The [executable method tests](../tests/cli/test_sauvola.py) cover admission, decoding, method
execution, published samples, selected discovery and response conformance together.

## Interrupting processing

SIGINT/SIGTERM on POSIX and CTRL_C/CTRL_BREAK on Windows request cooperative cancellation.
A confirmed cancellation produces `E_CANCELLED` and process exit 130. Valid requests cancelled
before execution do no input/output work. The last precommit checkpoint is a cutoff: a late request
cannot turn successful publication into cancellation. Unknown publication or cleanup retains exit 7;
response delivery can still fail with exit 5. Repeated handled interrupts do not force termination.
See [cancellation](cancellation.md) for precise outcomes, signal safety and blocking-I/O limits.
