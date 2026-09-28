# Design: verifiable processing bundles with persistent run records

Status: design under review. Nothing here is implemented yet. This document is the input to a
separate design-QA pass; it is removed or replaced by a current-state document before the pull
request, because shipped documentation describes what exists.

Baseline: `main` at `8f431f521942faeacabf270ff3827d1337a39f76`.

## The problem, as the merged code actually stands

Three findings, each checked against the merged tree rather than assumed.

**The image and its explanation have different lifetimes.** `app::PublishedImage` carries the
output path plus an optional `image::ConversionReport` and `methods::IlluminationReport`
([process.hpp:52](../../include/docenhance/app/process.hpp)). Those reports reach the command
response and nothing else. The publication transaction writes exactly one file, `result.png`
([png_publish.cpp](../../src/io/png_publish.cpp), `reserve_stage` hard-codes the name). Keep the
image, lose the settings, the protection mask, the resolved illumination decisions.

**The bytes that were processed are never identified.** The only digest in the product is
`dependency_lock_sha256`, a build constant ([dispatch.hpp:22](../../include/docenhance/app/dispatch.hpp)).
`read_png_snapshot` already produces the exact immutable encoded buffer the decoder consumes
([png_snapshot.hpp](../../src/io/png_snapshot.hpp)), and that buffer is discarded without ever
being identified. `picosha2` is pinned and linked only by `tools/native_probe.cpp`; no layer uses
it.

**The two output paths make different guarantees.** Continuous output encodes, then independently
reopens and checks every row before commit — `publish_png_rows` composes `encode_png_rows` with
`verify_png_rows` ([png_publish_rows.cpp:33](../../src/io/png_publish_rows.cpp)). Binary output
calls `encode_png` alone ([png_publish.cpp](../../src/io/png_publish.cpp), `publish_png`). Worse,
`ConversionReport::verified` is set by assignment after publication succeeds
([continuous.cpp:134](../../src/host/continuous.cpp)) rather than derived from the verification
event. A persistent record must not inherit a flag that a second statement can set.

## What is being built

One input image, the existing methods, one complete result directory:

```text
output/
  result.png
  run.json
  assets/
    protect-mask.png     # only when a mask was supplied
```

## Layer decisions

The existing graph is in `spec/architecture.json`. This adds one layer and one package edge.

**`bundle` — a new layer that owns the on-disk record.** Its type, its serialization, its parsing
and its validation live together, because a writer and a verifier that disagree about the format
are the defect this feature exists to prevent. `uses: core, contract, image, methods`; external:
`nlohmann_json`. It does not use `app`, so `app` may hold the identifiers the response reports
(run identity, manifest digest) as plain strings without a cycle.

**Digests belong to `io`.** Hashing happens exactly where bytes are already in hand: the snapshot
buffer at acquisition, and the encoded output after verification. `io` gains the `picosha2`
package. `core` keeps no third-party dependency.

**`io` never learns what JSON is.** It receives the serialized record as bytes and writes them into
staging with the other artifacts. The publication transaction is about inventories and exclusive
commit, not about formats.

**Verification is a port, like `Processor`.** `app::Verifier` is an interface the application
admits requests against; `host::Verifier` implements it over `io` and `bundle`. The application
layer stays free of the filesystem, and CLI fuzzers keep linking the pure application.

Resulting flow: `host` acquires and identifies inputs, executes, encodes and verifies the artifact,
builds a `bundle::RunRecord` from the typed request and the typed reports, asks `bundle` to
serialize it, and hands the complete inventory to `io` for one exclusive publication.

## The record

`run.json` is a closed, versioned object. Every section is derived from a typed value that already
exists; none of it is reconstructed from CLI strings, and no processing default is restated here.

| Section | Establishes |
|---|---|
| `record` | Record format version and the run identity. Nothing else in the file is self-referential. |
| `build` | Application version, contract and method versions, dependency-lock identity. Unknown stays explicitly unknown. |
| `source` | Digest and size of the exact encoded bytes consumed, the name under the privacy rule, the decoded descriptor, and interpretation observations. |
| `request` | The complete validated choices, distinguishing explicit from automatic where that distinction is real. |
| `execution` | Resolved parameters, profile/alpha/orientation decisions, illumination applicability and solver observations, truthful stage outcomes. |
| `protection` | Original mask digest, canonical stored-mask digest, dimensions, polarity, oriented coordinate frame. |
| `output` | Relative path, encoded-byte digest and size, dimensions, channels, depth, profile information, and the verification actually performed. |

Two rules the QA pass must hold this to:

**No self-referential digest.** `run.json` carries the digests of the image and the mask. Its own
digest is computed over the finished bytes and returned in the command response; embedding it
would require hashing bytes that contain the hash.

**The record describes processing, never publication.** It is finalized before the commit point, so
it cannot assert that publication succeeded. Publication state belongs to the response. The response
reports the run identity and the manifest digest, which is what later lets an ambiguous commit be
reconciled against whatever is on disk.

## Publication becomes an inventory transaction

`publish_generated_png` is already the right shape: reserve an owned staging directory, write,
observe the cancellation cutoff, commit by exclusive rename, and abandon with an honest publication
state ([png_publish.cpp](../../src/io/png_publish.cpp)). It generalizes from one hard-coded file to
a declared inventory:

1. acquire and identify inputs
2. execute the admitted operation
3. encode `result.png` and independently verify it — **both** output paths
4. digest the verified encoded output
5. write `assets/protect-mask.png` when a mask was supplied, and digest it
6. serialize `run.json` and validate it against the same schema the verifier uses
7. validate the complete staged inventory against what the record declares
8. close every writer
9. observe the final cancellation cutoff
10. one exclusive rename
11. report the real publication outcome

A manifest failure prevents publication exactly as an image failure does; there is no "image
succeeded, record failed" success. Cleanup removes only what this invocation owns — the existing
non-recursive `Stage::cleanup` extends to the declared inventory, never to a prefix match.

## `docenhance verify DIRECTORY --json`

Read-only, and a new command in `spec/cli-contract.json`. It validates the record against its
schema and supported versions, the declared inventory, and the actual sizes and digests of the
artifacts present. It works after the directory has been moved, because every path in the record is
relative to the bundle root.

A supplied bundle is untrusted input: paths stay inside the root, symbolic links and special files
are refused rather than followed, parsing is bounded in depth and size, and no field can cause a
read outside the bundle. Verification never executes the recorded request and never reruns a
method.

Two guarantees, kept apart in the output and in the documentation:

- **During processing**, decode-back comparison establishes that the encoded image matches the
  intended integer samples and metadata.
- **During verification**, checks establish that the artifacts agree with their record. Without the
  source and a rerun this does not prove the enhancement was correct.

Digests detect disagreement; they are not signatures. Anyone who can rewrite both the image and the
record can produce another internally consistent bundle. This is traceable processing, not
authenticated evidence, and the command's own output must not imply otherwise.

## Privacy

The record excludes absolute source paths, shell text, usernames and arbitrary source metadata by
default. A retained basename can still carry personal information, so the bundle is never described
as anonymized. The original document is not copied into the bundle.

## Not in this package

Batching, multipage inventories, recipe files, new codecs, new enhancement methods, a plugin or
migration framework, signatures or any authenticity claim. `B02`/`B03` arithmetic and interpretation
are unchanged; quantization stays final. Binary output gains verification, not new numerics.

## Questions the design-QA pass must answer

1. Does any fact end up with two owners — particularly `ConversionReport::verified`, which today is
   an assignable flag rather than a consequence of verification?
2. Is `bundle` a layer or a split brain? It must not become a second place where processing
   defaults live.
3. What exactly is the run identity, and what is it good for if it is not a signature?
4. Where does the binary path get its verification, and does it change any sample?
5. What does the verifier do with a record whose declared inventory and actual inventory differ in
   each possible direction?
6. Which failures must leave nothing published, and which must leave publication explicitly
   unknown?
