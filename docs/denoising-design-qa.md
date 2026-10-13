# D01 validation requirements

[Denoising](denoising.md) owns the current mathematical and resource contract. The controls below must exercise actual production/native boundaries; this checklist is not an execution result.

| Boundary | Required control |
| --- | --- |
| Global reflection | Compare tiles with whole-image native output at seams/corners. Halos fold the global source, not independent stitched tiles. |
| Native concurrency | Check the pinned 256-pixel tiling/stripe behavior without process-global thread setters; native reservations and concurrent isolation remain required. |
| Arithmetic | Exercise maximal patch/search windows, checked extents and CV_16U/L1 accumulation bounds against an independent direct-window integer oracle. |
| Quantization | Preserve linear floating input until the defined quantized-luminance correction; distinguish real working-space changes from equal final encoded bytes. |
| Protection | Exclude protected samples only where the contract requires; preserve entering protected RGB exactly while retaining neighbor context. |
| Lifecycle | Check successful prefix consumption, identities that leave illumination prepared, direct versus delegated errors, unstarted later stages and immutable replay. |
| Allocation | Inject native C++/Mat failures, require real resource errors and full refunds, and compare observed allocation to finite reservations. |
| Diagnostics | Capture command streams during native failure. Tracing/log policies suppress foreign streams while native exceptions reach their owning boundary. |
| Color output | Round-trip serialized grayscale profiles and verify standard parametric transfers separately from scalar pixel precision. |
| Source/platform identity | Require canonical acquired bytes, compiler/feature binding and sanitizer-compatible observers, including TSan's allocation hooks. |
| Attribution | Preserve applicable upstream notices and inspect actual native package composition; source hashes do not establish legal clearance. |

Required native, reference, sanitizer, corpus and CLI execution must bind the tested commit, executable, platform and inputs. Runtime/RSS observations describe those cases and do not establish a universal bound or natural-document quality. [Quality evaluation](document-quality.md) governs independent human claims.
