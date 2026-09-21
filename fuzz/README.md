# Fuzzing

Strict, engine-agnostic fuzz harnesses. See [design decisions](../docs/decisions.md) for the design and [quality gates](../docs/quality.md) for commands.

| Target | Checks | Input layout |
| --- | --- | --- |
| `pages` | `contract::parse_pages` against an independent reference parser; invariants; round trip | 2-byte big-endian expansion limit, then the selection text |
| `decimal` | `contract::parse_finite` against a regular-expression grammar; exact conversion; range and bound errors | 1 selector byte (0–7: option bounds; otherwise 16 bytes of arbitrary bounds), then the text |
| `numeric` | Raster budgets, reflect-101, sRGB, luminance transport and nearest-rank against exact references | 1 operation byte, then the operands |
| `cli` | The complete command line: exit codes, one valid JSON object, JSON errors for `--json`, determinism | NUL-separated arguments |

- `corpus/<target>/` holds reviewed seed inputs. Merge new coverage with `tools/run_fuzzers.py --merge`; never commit a raw campaign corpus.
- `regressions/<target>/` holds every input that once found a defect. Add the reproducer when you fix the defect.
- `dict/<target>.dict` holds the dictionaries. `cli.dict` is generated from `spec/cli-contract.json`.
- `targets.json` holds per-target limits and the strict sanitizer runtime options.

`tools/run_fuzzers.py` adds each engine's own requirements, such as the `symbolize=0` that AFL++ insists on, so the same strict options work with libFuzzer and AFL++.

Every normal build replays both directories through each harness as the `fuzz-replay-*` tests, on every compiler.
