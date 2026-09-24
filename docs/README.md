# Documentation map

This directory explains the project; it does not duplicate machine-owned configuration. Use the
following sources when changing behavior.

| Concern | Authority | Supporting documentation |
| --- | --- | --- |
| Version | `CMakeLists.txt` | [build](build.md) |
| Dependency identities | `deps/lock.json` | [dependency policy](dependencies.md) |
| Dependency features and tool pins | `deps/features.json`, `deps/tools.json` | [dependency policy](dependencies.md) |
| Layer graph and allowed edges | `spec/architecture.json` | [architecture](architecture.md) |
| Command syntax and method catalog | `spec/cli-contract.json`, `spec/method-contract.json` | [CLI contract](cli-contract.md), [methods](methods.md) |
| Machine response shape | `spec/command-response.schema.json`, `spec/method-contract.json` | [current CLI behavior](cli.md) |
| Release prose | `CHANGELOG.md` | [publishing](publishing.md) |

## Reading order

Start with [status](status.md) for verified capability boundaries, then read
[architecture](architecture.md) and [design decisions](decisions.md) before changing production
code. Use [build](build.md) and [quality](quality.md) to reproduce validation. The
[roadmap](roadmap.md) orders work that is not yet implemented.

## Generated documents

[CLI contract](cli-contract.md) and [methods](methods.md) are generated from the reviewed `spec/`
sources by `tools/generate_spec.py`; never edit them by hand. The structural check rejects stale
generated output. Other documents describe current policy and evidence and must be updated in the
same change as the behavior they describe.

[Typed binarization](binarization.md) specifies B02/B03 parameters, mathematics, memory, execution
contracts and the separate design QA.
