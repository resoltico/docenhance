# Current CLI behavior

## Implemented operation

`docenhance process` performs one complete, explicit operation:

```sh
docenhance process INPUT.png --out-dir RESULT --binarize fixed [--fixed-threshold T]
```

It accepts only a non-empty grayscale PNG without alpha, at 1, 2, 4, or 8 bits per sample, and
decodes it to 8-bit grayscale. `T` is a finite normalized threshold in `[0,1]` and defaults to
`0.50`; B03 writes black when `sample / 255 <= T`, otherwise white. The result is an 8-bit
grayscale PNG at `RESULT/result.png`.

`RESULT` must not exist and its parent must already be a directory. The writer first creates a
sibling staging directory and publishes it only after a complete PNG has been written. Failed
input, threshold, or output operations leave no result directory behind.

`docenhance methods` reports only B03 and `docenhance version --json` reports only `png` in
`supported_formats`. No other method, image format, alpha behavior, colour conversion, batching,
recipe, or preset is accepted.

## Responses and failures

With `--json`, processing success returns `method: "B03"`, the final output path, and
`publication: "completed"`. Invalid syntax is `E_ARGUMENT` with exit 2; invalid or unsupported
input is `E_INPUT` with exit 3; resource limits are `E_RESOURCE` with exit 4; and publication
errors are `E_OUTPUT` with exit 5. A failed operation reports `publication: "not_started"`.

JSON responses go only to stdout and text errors go only to stderr. The complete machine response
shape is [command-response.schema.json](../schemas/command-response.schema.json), and the real
binary is exercised by [CLI contract tests](../tests/cli/test_cli.py).
