# Dependency policy

`deps/lock.json` is the sole authoritative catalog of selected upstream sources: names, release
identities, transport, cryptographic digests, declared licenses and distribution scope. Do not copy
or update an individual dependency version in this document.

`deps/features.json` is the sole authoritative feature policy. `deps/tools.json` owns the pinned
developer-tool versions and the macOS deployment target. The build reads those files directly;
ordinary configure and build commands never substitute a system dependency or follow a moving
branch.

## Acquisition and verification

Acquisition is explicit:

```sh
cmake -P cmake/AcquireDependencies.cmake
```

Each source is checked against its declared release object or archive digest, then inventoried into
a receipt in `.cache/deps/receipts`. Every build re-verifies that receipt before using the cache. Archive source bytes must also
match the original digest-checked archive; changing both a source tree and its receipt cannot
override the lock. Selected source archives admit bounded regular files/directories in one root,
without links or duplicate names.
The cache is local working state, not source control and not a second authority. See the [build
guide](build.md#acquisition-is-a-separate-phase) for concurrency and recovery rules.

The JSON build uses a checked private copy of the locked single header. Its recursive destruction
avoids an allocating traversal stack inside noexcept cleanup; production DOM inputs are admitted
at depth <=16, while record/response builders have fixed typed shapes. The source cache and upstream
attribution are untouched. Feature policy pins the reviewed source-header digest; the build audit
compares the installed header with the private build input and an independent reviewed output
digest. Allocation-failure tests establish the
cleanup behavior separately from that byte-identity check. See [the resource audit](resource-limits-audit.md).
Native C++ integrations must consistently use the corrected installed header from the project's
private prefix; mixing it with stock JSON headers would violate the inline definition contract.

## Runtime boundary

The lock distinguishes `runtime-candidate` dependencies from test-only dependencies. A selected
source is not automatically a runtime feature: a layer may use it only when
`spec/architecture.json` grants that package to the layer and the code actually includes it. The
current permitted OpenCV module closure is `core`, `flann`, `geometry`, `imgproc`, and `photo`.
The selected codecs, OpenCV, Leptonica and Little CMS do not authorize image decoding or processing
until their complete contracts and tests exist.

## Attribution and release review

The top-level [third-party notices](../THIRD_PARTY_NOTICES.md) describe the source distribution.
Native package generation copies the original upstream license texts and writes an SPDX inventory.
Before distributing a binary, review the locked source identity, upstream release notes, licenses,
feature policy, configured module closure, final binary composition, and relocated-package smoke
test. A source digest establishes identity; it is not a security endorsement or a reproducible
machine-image claim.

## Updating a dependency

Review the official upstream release and license changes first. Change the lock and feature policy
together when necessary, acquire into a fresh named cache entry, build in a fresh private prefix,
and run the native probe, feature audit, numerical/CLI tests, fuzzing where applicable, and the
relocated-package smoke test. Never accept an update merely because its version is higher.

Acquisition claims one writer per cache. Git commands ignore inherited Git environment/global
configuration and grant only the exact owned working directory as safe. Release objects and
source inventories remain mandatory. The Intel LLVM tool cache similarly requires both tools,
source/recipe identity and byte-matching readiness evidence; partial caches are not adopted.
