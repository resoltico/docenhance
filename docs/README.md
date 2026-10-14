# Documentation map

Documentation describes the current implementation, contracts, requirements and limits. Release history belongs in the [changelog](../CHANGELOG.md); documentation does not serve as an execution log or certify unobserved platforms.

| Concern | Authority | Supporting documentation |
| --- | --- | --- |
| First-party license | `LICENSE` and SPDX source notices | [licensing](licensing.md) |
| Version | `CMakeLists.txt` | [build](build.md) |
| Dependency identities | `deps/lock.json` | [dependency policy](dependencies.md) |
| Dependency features and tool pins | `deps/features.json`, `deps/tools.json` | [dependency policy](dependencies.md) |
| Layer graph and allowed edges | `spec/architecture.json` | [architecture](architecture.md) |
| Command syntax and method classification | `spec/cli-contract.json`, `spec/method-contract.json` | [CLI contract](cli-contract.md), [methods](methods.md) |
| Response and record shapes | `spec/command-response.schema.json`, `spec/run-record.schema.json` | [bundles](bundles.md), [CLI behavior](cli.md) |
| Public corpus admission and rights | `docs/corpus.md`; `corpus/manifest.json` owns item provenance | [corpus](corpus.md), [licensing](licensing.md) |
| Document-quality protocol and report | `spec/document-quality.schema.json` | [quality evaluation](document-quality.md) |
| Release narrative | `CHANGELOG.md` | [publishing](publishing.md) |

## Reading order

Start with [status](status.md), then [architecture](architecture.md) and [design decisions](decisions.md). [Build](build.md), [quality](quality.md) and [fuzzing](fuzzing.md) describe required verification. Execution evidence must identify the actual commit, executable, platform, inputs and result; a documented command is not evidence that it ran.

[Foundation design](foundation-design.md) describes admitted plans and lifecycle ownership. [Foundation review](foundation-qa.md) lists acceptance conditions. [Document-quality evaluation](document-quality.md) governs independent cleared held-out evidence separately from engineering correctness.

The [public corpus](corpus.md) documents image acquisition, source-specific rights and the boundary
between technical inputs and independent human quality evidence.

## Representation and method guides

- [PNG](png-processing.md), [JPEG](jpeg-processing.md) and [TIFF](tiff-processing.md) own container admission, sample interpretation, resource limits and output verification.
- [Geometry](geometry.md) owns G01 metadata orientation, G02 exact quarter-turns, mask/PSF coordinate frames and resolution handling.
- [Illumination](illumination.md) and [morphological illumination](morphological-illumination.md) own I01/I02 mathematics and applicability.
- [NLM](denoising.md), [NLM validation](denoising-design-qa.md) and [TV-L1](tvl1-denoising.md) own denoising semantics and resource contracts.
- [Restoration](restoration.md), [contrast](contrast.md) and [sharpening](sharpening.md) own the remaining continuous stages.
- [Binarization](binarization.md) owns the separate B01/B02/B03 stored-gray contract.
- [Cancellation](cancellation.md) and [bundles](bundles.md) own execution control and publication safety.
- [CI cache and replay ownership](ci-performance.md) describes source/tool reuse and configuration-private verification work.

## Generated references

[CLI contract](cli-contract.md), [methods](methods.md) and [roadmap](roadmap.md) are generated from reviewed `spec/` sources by `tools/generate_spec.py`; do not edit them by hand. The roadmap is an inventory, not executable admission. Runtime selectable catalogs come from implemented types. Current guides must agree with those authorities.
