# Foundational ownership and validation design

The reviewed handoff is anchored to `09e7a979e9378ca81475c31f0c9eed36007a73ec`.
The source/evidence baseline is collected before editing; later main changes must be reconciled.
This design retains the existing fourteen ownership layers and all numerical algorithms. Layer
count, source-file count and a green build do not establish optimality or natural-document quality.

## Admitted plans and observations

`ProcessRequest` privately owns one `BinaryPlan | ContinuousPlan`. The binary alternative contains
only validated binarization and exact rotation. The continuous alternative contains representation,
one fixed `PhotometricPlan`, B-frame protection policy and exact rotation. Request paths remain
shared and cancellation remains separate execution control. There are no fallback stage members,
mutable plan accessors or forwarding APIs. Borrowing requires a live lvalue; moves consume the
source path. A valueless or moved-from request is refused before I/O.

One `methods::StageObservations` aggregate retains the five family-specific reports. Application
selection initializes requested facts once. A selected stage begins as `not_started`, with no work
observations. Execution changes only the stage it attempts; failures preserve earlier completed
facts and leave later selections unstarted. The same aggregate is borrowed by record serialization
and moved into terminal continuous success/failure. The restoration report includes its immutable
resolved PSF. Moving the result avoids allocating a copied PSF merely to retain a known commit.

The alternative of virtual stages, JSON parameter maps or generic recipe/DAG execution was rejected:
the current order is fixed, and numerical/native ownership, model liveness and preparation differ
by family. These differences are required correctness boundaries. The existing bundle reader still
checks complete typed claims and canonical serialization, independent of JSON Schema.

Response and record version 13 are current. Obsolete records are refused without migration.
The source method contract has one classification: implemented-selectable, implemented-automatic
or planned. G01 is automatic continuous metadata orientation, never a selector. G02 remains selectable;
G03–G05 remain plans. Runtime descriptors continue coming from actual executable variants.
The generated roadmap removes a second manually maintained capability inventory.

## Retained geometry, effects and resources

The [A–F geometry contract](geometry.md) already supplies exact inverse gathers, B-frame masks,
F-frame PSFs and density transpose parity. A second geometry object or general warp backend adds
no proven property; retain the existing exact value types and independently tested permutations.

`core::Error::valid_publication` remains the small semantic authority for error/publication pairing.
Native no-replace ownership, final precommit cutoff, real-error precedence, complete decode-back
comparison, monotone known commits and conservative cleanup remain in I/O/host. No generic state
machine, new fsync promise or automatic retry is added. The app preallocates its unknown-publication
fallback before effects; the CLI renders/delivers once. Read-only verification cannot publish.

The existing shared ledger owns buffers and native reservations. Its charged high-water observation
includes reservations and refused system allocations, preserves peaks after refunds and is not
process RSS. Fresh-process resource measurements report actual RSS/CPU/wall data and sampled
thread/disk data on named inputs/machines. Individual native-call timings remain unavailable unless
separately observed; whole command elapsed time is not mislabeled as native latency.

Locked upstream sources, private feature/patch recipes, compiled closure checks, license inventory,
OSV evidence, both strict fuzz engines and sanitizer detection remain authoritative. A cache hash
binds inputs, not source trust. No dependency is substituted or upgraded as part of this refactor.

## Platform and quality decisions

`deps/tools.json` owns macOS minimum 15.0. Seven required native lanes include actual macOS 15
ARM64/Intel and retained macOS 26 ARM64/Intel, Linux x86-64/ARM64 and Windows x86-64. A wrong physical
OS/CPU fails before compilation; relocated package execution and native deployment/import checks
supply separate product-boundary observations. Missing/skipped lanes cannot satisfy release admission.

The [quality protocol](document-quality.md) closes the evidence gap through a governed external
corpus, explicit comparator, actual deterministic replay, independent integer/protection comparison,
blinded human review and per-class harm refusal. No favorable empirical result is inferred from
synthetic tests. Source publication remains distinct from signed/notarized native distribution.
