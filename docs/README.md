# Documentation map

Documentation describes current implementation and limits; history belongs in [CHANGELOG](../CHANGELOG.md).
Execution evidence identifies the actual source, executable, platform, inputs and outcome.

| Concern | Authority and guide |
| --- | --- |
| Layers, resources, cancellation and effects | `spec/architecture.json`; [architecture](architecture.md) |
| Fixed execution and common geometry | [pipeline](pipeline.md) |
| I01/I02, D01/D02, R01, C01–C03/S01 and B01–B03 | [illumination](methods/illumination.md), [denoising](methods/denoising.md), [restoration](methods/restoration.md), [contrast](methods/contrast.md), [sharpening](methods/sharpening.md), [binarization](methods/binarization.md) |
| Representation and metadata admission | [PNG](formats/png.md), [JPEG](formats/jpeg.md), [TIFF](formats/tiff.md) |
| Bundle layout, wire shapes and read-only agreement | Actual C++ Fields and native-emitted schemas; [record](record.md) |
| Syntax and implemented parameters | `spec/cli-contract.json`, `spec/method-contract.json`; generated [CLI reference](cli-reference.md) and [method reference](methods.md) |
| Invocation, status and delivery | [CLI](cli.md) |
| Native identity, features, tools and licensing | `deps/lock.json`, `deps/features.json`, `deps/tools.json`; [dependencies](dependencies.md), [licensing](licensing.md) |
| Build, verification and publication | [build](build.md), [quality](quality.md), [fuzzing](fuzzing.md), [publishing](publishing.md) |
| Corpus provenance and independent human evidence | [corpus](corpus.md), `corpus/manifest.json`, [document quality](document-quality.md), `spec/document-quality.schema.json` |
| Version and release narrative | `CMakeLists.txt`, [CHANGELOG](../CHANGELOG.md) |

Start with [status](status.md), [architecture](architecture.md) and [the decision index](decisions.md).
The [roadmap](roadmap.md) is planning, not executable admission. First-party licensing remains in
[LICENSE](../LICENSE) and SPDX notices; [third-party notices](../THIRD_PARTY_NOTICES.md) retain upstream attribution.

## Generated references

`tools/generate_spec.py` projects reviewed CLI/method contracts into [the CLI reference](cli-reference.md)
and [method reference](methods.md). Actual implemented types own runtime catalogs; common geometry
belongs to [pipeline](pipeline.md). Actual C++ Fields and the compiled `de_schema` emitter generate
both schemas and the record dictionary. Do not edit those generated projections by hand.
