# Argument contract

> Generated from `spec/cli-contract.json` by `tools/generate_spec.py`. These are the arguments the command line parses and documents today. See [current CLI behavior](cli.md) and [status](status.md) for supported behavior.

P = process.

## Commands

- `root` — `docenhance COMMAND [OPTIONS]`
- `process` — `docenhance process INPUT --out-dir DIRECTORY --binarize fixed [--fixed-threshold T] [--json]`
- `methods` — `docenhance methods [ID] [--json]`
- `version` — `docenhance version [--json]`

## Value grammar

**Decimal values.** Finite decimal, optionally in scientific notation: an optional leading '-', digits with an optional '.', then an optional exponent 'e' or 'E' with an optional '+' or '-' and one or more digits. No leading '+', whitespace, hexadecimal, infinity, NaN, or trailing characters. The value must be finite and inside the option's documented range; the decimal point is '.' in every locale.

## `--out-dir DIRECTORY`

**Scope:** P. **Domain/default:** Required; a new result directory.

Parent must exist. The final directory must not exist; processing writes a staged 8-bit grayscale PNG and publishes the directory only after success.

**Applicable methods:** B03.

## `--binarize METHOD`

**Scope:** P. **Domain/default:** Required; `fixed`.

Selects B03 fixed-threshold binarization. No other method is accepted.

**Applicable methods:** B03.

## `--fixed-threshold T`

**Scope:** P. **Domain/default:** 0.50; finite `[0,1]`.

Normalized grayscale threshold. A sample is black iff its value is less than or equal to T.

**Applicable methods:** B03.

## `--json`

**Scope:** All commands. **Domain/default:** False.

Writes exactly one final JSON response to stdout.

## `--help`

**Scope:** All commands. **Domain/default:** N/A.

Renders help and exits without opening input or creating output.

## `--version`

**Scope:** Root only. **Domain/default:** N/A.

Human-readable alias of `version`.
