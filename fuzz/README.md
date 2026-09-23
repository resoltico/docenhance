# Fuzzing

`targets.json` is the authoritative harness inventory: source files, direct link requirements,
input limits and sanitizer settings. Both engine builds and ordinary corpus replays consume it.
See [campaigns and decoder coverage](../docs/fuzzing.md) for execution, evidence, and failure
contracts, and [quality gates](../docs/quality.md) for commands.

Harness sources describe their byte layouts and independent oracles. `png_decode` consumes raw
encoded bytes; `png_samples` consumes five selector bytes (width, height, depth, interlace,
scanline filter) followed by sample indices, constructing valid inputs without the production
encoder. The CLI harness has no host/filesystem authority.

`corpus/<target>/` holds reviewed seeds; `regressions/<target>/` holds reproducers.
Every input is replayed in normal builds. Campaign preparation copies distinct contents by hash
and records all original paths, including duplicate origins. Prior campaign output is never
removed. Review and minimize findings before deliberately committing them; the runner has no
implicit merge or promotion command. `dict/<target>.dict` supplies optional dictionaries;
`cli.dict` is generated from the reviewed command contract.
