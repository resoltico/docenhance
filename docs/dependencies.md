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

Quality-gate and nightly CI restore only the `archives`, `sources` and `receipts` directories
inside `.cache/deps` with an exact OS-specific key bound to the source lock and
acquisition/verification implementation. There are no prefix-match restore keys.
Acquisition still runs unconditionally: restored Git release objects, archive digests and complete
source inventories must pass the ordinary checks before use. A miss is saved only after that
step succeeds. Changed existing sources or missing receipts fail instead of being silently
repaired; absent sources follow the ordinary locked acquisition path.
Source bytes are architecture-independent; OS separation preserves filesystem behavior without
pretending to identify a native compiler configuration. No build tree, installed prefix, object,
application binary or prior test result is restored. Native and fuzz jobs build fresh configurations
and retain source, feature, compilation, sanitizer, test and package checks. See the
[CI performance evidence and cache design](ci-performance.md).

The private zlib build retains its in-memory compression/decompression core and excludes the
unused gzip-file translation units. Its upstream recipe identity is reviewed before adaptation;
the native audit checks actual compiler work to reject reintroduction. Upstream source bytes and
licenses remain intact. This private prefix is not a general-purpose zlib SDK.

The JSON build uses a checked private copy of the locked single header. Its recursive destruction
avoids an allocating traversal stack inside noexcept cleanup; production DOM inputs are admitted
at depth <=16, while record/response builders have fixed typed shapes. The source cache and upstream
attribution are untouched. Feature policy pins the reviewed source-header digest; the build audit
compares the installed header with the private build input and an independent reviewed output
digest. Allocation-failure tests establish the
cleanup behavior separately from that byte-identity check. See [the resource audit](resource-limits-audit.md).
Native C++ integrations must consistently use the corrected installed header from the project's
private prefix; mixing it with stock JSON headers would violate the inline definition contract.

The TIFF build uses a hash-bound private copy of the locked source with one per-handle JPEG
memory installer before native JPEG header parsing. It retains original upstream notices and
requires explicit static installation. A second checked adaptation requires complete Deflate
striles, including checksum trailers; producing the expected pixels alone is insufficient. Build audit checks adapted-source identity, actual
compilation and the installed installer ABI; native resource-refusal/refund tests separately
exercise the allocation behavior. TIFF, JPEG, zlib and PNG are all included in the instrumented
codec fuzz closure. This private TIFF prefix is not a general-purpose TIFF SDK.

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

## Advisory observations

Run `python tools/check_advisories.py` after acquisition. The independent nightly job runs the
same command. It verifies cached sources, queries [OSV](https://google.github.io/osv.dev/post-v1-query/)
using peeled Git commits, follows bounded pagination and refuses failed or malformed observations.
The TIFF archive uses an OSS-Fuzz package-version query, whose coverage is narrower than an exact
commit query. An empty database result is not proof that a source is vulnerability-free.

Every match is reported. New matches and changes to reviewed source, feature policy or advisory
evidence require review. A valid OSV `modified` timestamp is observation metadata and does not
change the review binding by itself; every other field, including unknown fields and withdrawal,
remains bound. Missing or malformed timestamps are refused. See the
[OSV field definitions](https://ossf.github.io/osv-schema/#id-modified-fields).
The reviewed exclusions concern zlib gzip-file writing, OpenCV JPEG-2000 decoding,
and OSV-2026-1068 in libjpeg-turbo's TurboJPEG compression API. The first is removed
from the private build; OpenCV's affected imgcodecs/OpenJPEG path is outside the
module/provider closure. The JPEG advisory reports tj3Compress8 -> jpeg_abort:
the pinned build excludes the TurboJPEG library and compression entry point, while
retaining the classic libjpeg decoder (which also uses jpeg_abort). The configured
WITH_TURBOJPEG=OFF alone is insufficient: the native build audit independently
rejects the TurboJPEG compilation units and an installed TurboJPEG library. These reviews are code-bound in the exception registry and
rely on independent native build audits; hashes establish freshness, not review quality. They do
not claim that the original upstream source is fixed. Review upstream/CNA evidence and actual
reachability before changing sources or recording another exclusion.

Acquisition claims one writer per cache. Git commands ignore inherited Git environment/global
configuration and grant only the exact owned working directory as safe. Release objects and
source inventories remain mandatory. The Intel LLVM tool cache similarly requires both tools,
source/recipe identity and byte-matching readiness evidence; partial caches are not adopted.
CI may reuse these verified analysis tools; each application/dependency build still starts in its
own native build tree. Reusing a compiler installation is not proof of binary reproducibility.


The nightly OSV job also writes a source-bound JSON observation report, including the
lock and feature-policy hashes, exact finding-evidence fingerprints and an explicit
incomplete/failure state when OSV queries cannot finish. Artifacts are retained for
30 days. Neither a reviewed exclusion nor an empty OSV result means a vulnerability
has been repaired or cannot exist.
