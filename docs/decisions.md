# Design decisions

These decisions index the current implementation. The linked authority owns each guarantee and
non-guarantee; release history belongs in [CHANGELOG](../CHANGELOG.md).

| Decision | Current choice and authority |
| --- | --- |
| D1 — Execution | [Eager fixed pipeline](pipeline.md#eager-interpretation-and-fixed-method-execution), with immutable completed facts. |
| D2 — Output modes | [One interpretation and photometric path](pipeline.md); continuous representation or terminal [binary classification](methods/binarization.md). |
| D3 — Method shape | [Typed methods on one working plane](pipeline.md#eager-interpretation-and-fixed-method-execution) produce immutable completed Facts; [kernels](architecture.md#numerical-work-and-scheduling) remain separate from effects. |
| D4 — Native packages | [Locked dependency isolation](dependencies.md); first-party NLM and bounded pocketfft restoration. |
| D5 — Admission | [Actual typed parameters](methods.md) and owned plans, with generated projections and no recipe registry. |
| D6 — CLI | [Strict tokenization and one delivery](cli.md); generated syntax is in [the CLI reference](cli-reference.md). |
| D7 — Records | [Actual Fields and native schema generation](record.md#source-observations-and-format-compatibility). |
| D8 — Wire | [Current record/response forms](record.md); completed facts and runtime diagnostics have separate homes. |
| D9 — Layers | [Responsibility-owned layers](architecture.md#layers) follow the reviewed manifest rather than third-party package or task boundaries. |
| D10 — Effects | [Host snapshots and publication](architecture.md#exclusive-publication) surround pure pipeline execution. |
| D11 — Errors | [Code/message and explicit publication](architecture.md#authority-and-composition) retain uncertainty and actual completed work. |
| D12 — Execution control | [Checked per-call workers and one processing ledger](architecture.md#numerical-work-and-scheduling), with worker-private color readers and bounded native reservations. |
| D13 — Catalog | [Runtime catalogs](methods.md) contain actual implemented method alternatives; [common geometry](pipeline.md) remains outside that catalog. |
| D14 — Documentation | [The documentation map](README.md) links one current home for each responsibility; generated references are not hand-authored contracts. |
| D15 — Ownership census | [Explicit build/source ownership](architecture.md#enforced-boundaries-not-just-a-diagram) and [verification](quality.md) retain concrete protected properties. |
| D16 — Versions | `CMakeLists.txt`, [current wire declarations](record.md#source-observations-and-format-compatibility) and [method metadata](methods.md) own their respective versions; net breaking outcomes belong in [CHANGELOG](../CHANGELOG.md). |
