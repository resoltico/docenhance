# Current CLI behavior

This document describes the executable as it behaves today. The [argument contract](cli-contract.md) lists every argument it parses, and the [roadmap](roadmap.md) covers the processing that does not exist yet.

## Working commands

`docenhance --help` and `docenhance COMMAND --help` render core-owned help. Adding `--json` returns machine-readable help using the command-response schema. Command-specific help includes the future argument descriptions and explicitly states that processing is unavailable.

`docenhance version [--json]` reports the version generated from the top-level `project(... VERSION ...)` command, build compiler/platform and source-lock hash. `docenhance --version` is a text-only alias; combining it with `--json` is an invocation error. Use `version --json` instead.

`docenhance methods [--json]` returns an empty implemented-method list. `methods ID` fails because no complete algorithm is available. The planned 17-method catalog is documentation, not a runtime promise.

## Commands that intentionally fail

`process`, `plan`, `inspect` and `presets` have syntax scaffolding but no implementation. They return `E_NOT_IMPLEMENTED`, exit status **4**, and `publication: not_started` in JSON. They do not read source documents, validate codec content, write recipes or create output bundles. Required input syntax and the required `process --out-dir` are checked first; their absence returns exit status 2.

Example intended for observing the refusal, not enhancing an image:

```sh
docenhance process input.jpg --out-dir result --json
```

The result is an explicit error, not a successful no-op copy. Existing files are not touched.

## Parsing and streams

Arguments are case-sensitive and unknown arguments are rejected. Repeated options are rejected. No abbreviation, response-file, environment-variable or automatic user/system configuration loader is installed. Root flags are not mixed with subcommands; place `--json` and `--help` after the selected command. Literal `--json` is recognized for presentation of parser failures.

JSON responses go only to stdout, and ordinary text errors go to stderr. Exit codes keep their defined meanings: in particular **7 means publication state unknown**, never “feature not implemented.” The scaffold-only `E_NOT_IMPLEMENTED` uses processing failure class 4. Stream failures return 5. Unexpected internal exceptions return 8.

The command-response JSON schema is [command-response.schema.json](../schemas/command-response.schema.json). Every JSON response of the real executable is validated against it by the CLI contract test, so an undeclared or malformed field fails the build. Numeric method options are captured as syntax for help/parsing only; full semantic compatibility and range validation is not implemented or claimed. Future processing may not bypass a typed core validator.

## Contract regression tests

`tests/cli/test_cli.py` exercises a **real built binary**, JSON streams, help, unknown/repeated arguments, required parameters, false capability prevention and the no-write behavior of unavailable processing commands. These native CLI tests are configured in CTest/CI but were not executed in the authoring environment; see [verification](status.md).
