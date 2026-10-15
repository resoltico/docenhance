# Fixed-pipeline plans and lifecycle ownership

## Admitted plans

`ProcessRequest` privately owns `BinaryPlan | ContinuousPlan`. Binary plans contain validated binarization and exact rotation. Continuous plans contain representation, the fixed photometric sequence, B-frame protection and exact rotation. Paths are shared request data; cancellation is separate execution control. Borrowing requires a live lvalue owner. Moves consume the source path; valueless or moved-from requests fail before I/O.

The fixed order is geometry, illumination, denoising, restoration, contrast, sharpening and final quantization. Models, preparation, source consumption and native boundaries differ by family. There is no generic recipe/DAG engine or plugin registry. Runtime catalogs come from actual method types, with parameter domains and selectors declared in `spec/method-contract.json` and projected into [method metadata](methods.md). G01 metadata orientation and explicit G02 quarter-turn rotation belong to the common image/request geometry contract, outside that catalog. G03–G05 are not executable capabilities.

## Progress and numerical disposition

One `methods::StageObservations` aggregate owns the five family reports. Records borrow it; terminal continuous results move it, retaining the resolved PSF without a diagnostic vector copy after publication. The [bundle lifecycle contract](bundles.md#stage-progress-and-numerical-disposition) owns progress vocabulary, status semantics and record policy.

Selection initializes requested facts without inventing work. Each owner changes only the stage it enters. Failure retains completed earlier facts, actual preparation and partial counters, and untouched later selections. Direct failures belong to their owner; consumers propagating an upstream read error remain unresolved. Publication and stream delivery remain separate facts.

Illumination uses one row-major observing cursor. Other host assessments own their stage completion. Prefix-observed flags mean that the entering canvas was actually consumed, not merely that a model exists. Successful active denoising/restoration/sharpening preparation and measured contrast consume their prefix; gamma preparation and identity decisions can leave illumination prepared. Later failure must preserve that distinction. Replay uses retained models and cannot mutate recorded observations.

## Geometry, resources and effects

The [A–F geometry contract](geometry.md) provides integer inverse gathers, B-frame masks, F-frame PSFs and density transpose parity. Numeric kernels own mathematics without I/O, process state, allocation expressions or exceptions. Adapters own allocation and foreign-library containment; the shared ledger owns charged payloads and finite native reservations. Charged high-water bytes are not process RSS. Fresh-process measurements must identify machine, inputs and build and distinguish actual RSS/CPU/wall observations from sampled disk/thread counts.

[Cancellation](cancellation.md) requires bounded checkpoints, joined workers, genuine-error precedence and one final precommit cutoff. [Publication](bundles.md) promises exclusive atomic visibility, not power-loss durability. Confirmed commits survive later inspection failure; ambiguous effects remain unknown. The application prepares its unknown-publication fallback before effects, and the CLI renders/delivers once without retry.

## Authorities and qualification

[Architecture](architecture.md) owns layer responsibilities; reviewed manifests and schemas own executable permissions and wire shapes. [Dependency policy](dependencies.md) owns immutable sources, private adaptations, attribution and advisory limitations. Current response and record formats have independent schema authorities; obsolete forms have no compatibility reader or migration.

[Build](build.md) owns toolchain and platform requirements, including actual minimum-OS execution. A deployment flag or newer-OS result cannot certify that minimum. [Document-quality evaluation](document-quality.md) requires independent cleared natural-document evidence and blinded review; synthetic numerical agreement cannot establish readability or authenticity. Source publication and signed/notarized native distribution have separate qualification requirements.
