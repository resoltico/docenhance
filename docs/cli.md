# Current CLI behavior

## Implemented operations

`docenhance process` performs one explicit binarization operation:

```sh
docenhance process INPUT.png --out-dir RESULT --binarize fixed --fixed-threshold 0.5
docenhance process INPUT.png --out-dir RESULT --binarize sauvola --sauvola-window 31 --sauvola-k 0.2 --sauvola-r 0.5
```

Method options shown above are their defaults and may be omitted. The selector is required.
B03 uses a fixed normalized threshold; B02 uses a reflected local population window. Parameters,
equality, scale and resource rules are specified in [typed binarization](binarization.md).
Wrong-method options, explicitly empty numeric values and invalid combinations are rejected before
reading input. Window values are odd ASCII-digit integers in `[3,4095]`; `k` is finite in `[0,1]`
and `R` is finite in `[1/255,1]`, measured in normalized sample units.

Both accept a non-empty grayscale PNG without transparency, at 1/2/4/8 bits per sample, expanded
to 8-bit stored samples without a gamma conversion. Output is an 8-bit grayscale PNG containing
only black and white at `RESULT/result.png`. No color, alpha, 16-bit, multipage, JPEG/TIFF, recipe,
preset or other method support is implied.

`RESULT` must not exist and its parent must already be a directory. The writer creates exclusively
owned sibling staging and atomically publishes without replacement after encoding and closing
succeed. Publication uncertainty is not hidden; see [architecture](architecture.md).

`docenhance methods` reports B02 and B03. `methods B02` or `methods B03` selects one entry.
`version --json` reports the complete executable method list and only `png` as an input format.
Help, version and capability discovery do not invoke the image-processing host.

## Responses and failures

With `--json`, processing success contains the admitted `method`, its `method_version`, the final
output path, and `publication: completed`. The delivered machine contract is
[command-response.schema.json](../schemas/command-response.schema.json); edit the authoring
schema and method catalog under `spec/`, then regenerate, rather than editing generated output.

Invalid syntax/parameters use `E_ARGUMENT`, exit 2; invalid or unsupported input uses `E_INPUT`,
exit 3; resource refusal uses `E_RESOURCE`, exit 4; a known output refusal uses `E_OUTPUT`, exit 5.
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
