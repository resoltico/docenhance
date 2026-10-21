# Processing bundles and run records

Every result this program publishes travels with a record of what produced it, and it can check
that a bundle holds what its record says. This contract defines what is written, what reading it
back establishes, and what it does not.

## What a run publishes

`process INPUT --out-dir DIRECTORY` publishes one directory:

```text
DIRECTORY/
  result.png
  run.json
  assets/
    protect-mask.png     # only when --protect-mask was supplied
    psf.png              # only for a supplied R01 kernel
```

The [publication contract](architecture.md#prepared-bundle-publication) owns staging, byte-owner lifetime and commit reconciliation.

## Completed facts and failures

All output modes share one eager fixed pipeline: interpretation and exact geometry, illumination,
denoising, restoration, contrast, sharpening, then continuous quantization or binary classification.
A selected stage returns one immutable completed Facts value. Disabled stages have no selected method
and no completed Facts. Numerical identity, no eligible samples and actual application have distinct
method-specific dispositions and reasons; selection alone is never evidence of a completed traversal.

A processing failure carries its explicit publication state, optional failed family and a sparse map
of actually completed earlier stages. The failed stage and later stages cannot appear in that map.
A failure during encoding or publication can retain all completed stages without a failed family.
A confirmed commit followed by failed validation remains completed publication. Private runtime
warning context retains actual partial TV-L1 stop and entered restoration/sharpening observations;
that context is never serialized as a run record or response object.

Warnings are derived from completed Facts and, for failures, available private runtime observations.
They appear once in the command response and human report. A run record stores neither warning IDs
nor cached warning booleans. Available facts do not turn unstarted or interrupted work into completion.

## What the record says

`run.json` is a closed, versioned object. The [current format authority](#source-observations-and-format-compatibility)
admits the actual fixed plan, with reviewed method versions and validated parameters. Missing or unknown
fields, duplicate object keys (including escaped equivalents), out-of-range numbers and inconsistent
observations are refused. Numeric domains are checked before narrowing. Run identities are 32
lowercase hexadecimal characters; recorded instants use `YYYY-MM-DDTHH:MM:SSZ` with a valid
calendar date. Identity text is strict UTF-8 and is never repaired. Each admitted execution takes its own
run identity and instant, including repeated calls through the same native processor. Portable
artifact paths are at most 128 UTF-8 bytes, separated by `/`, without empty, `.` or `..` components,
NUL, backslash or colon. This domain is shared by staging, record reading and application observations.

The generated [run-record schema](../schemas/run-record.schema.json) is shipped with the response
schema. The typed reader remains authoritative and also checks relationships that JSON Schema
cannot express, such as matching dimensions, selected method/Facts pairs, counts and derived fractions.
Schema string length counts characters; native portable-path admission additionally checks its
128-byte UTF-8 ceiling. The instant schema describes lexical shape; native admission checks the
real Gregorian calendar. Neither lexical metadata nor a format annotation substitutes for those checks.
Independent schema tests check records emitted by the real executable and negative examples.

Every field comes from a value the execution already
held; the record layer that writes it defines no default, no option table and no processing rule of
its own.

| Section | Establishes |
|---|---|
| `record` | The record format version, the run identity and the instant it was recorded |
| `build` | Application version, platform, compiler and dependency-lock identity |
| `source` | The digest/size of the exact encoded input snapshot and basename; required typed container/decode observations |
| `request` | Output mode/depth/policies, rotation, nullable selected methods/parameters and supplied mask identity |
| `execution` | Interpretation and actual completed illumination/denoising/restoration/contrast/sharpening/binarization Facts; absent selections are null |
| `assets` | Closed optional canonical protection-mask and supplied PSF roles, with their exact stored identities |
| `output` | The result's relative path, digest, size, dimensions, depth, profile presence and the verification performed |

Two properties are deliberate:

**The record describes processing, never publication.** It is finalized before the commit point, so
it cannot assert that publication succeeded. Publication state belongs to the command response,
which also reports the run identity and the record's own digest — the pair that lets an ambiguous
commit be reconciled against whatever is on disk.

**The record never carries its own digest.** Embedding it would require hashing bytes that contain
the hash. The response reports it instead.

`output.verification` is `decoded_and_compared`: independent decoding compared the encoded result
with the intended integer samples or logical binary codes and canonical metadata. All output modes
do this before staging. Later bundle agreement cannot independently prove that original comparison.

## Identity is taken from the bytes that were used

See [the shared architecture contract](architecture.md#identity-is-taken-from-the-bytes-that-were-used).

## Reading a bundle back

```sh
docenhance verify DIRECTORY [--json]
```

Verification is read-only. It reads `run.json` within its byte, nesting and event bounds, refuses
duplicate decoded keys before DOM construction, and checks the bundle against what the record declares. It
works after the directory has been moved, because every declared path is relative to the bundle
root.

The root is opened once. POSIX reads are relative to the retained directory descriptors with
non-following flags, and regular-file status is checked on each opened descriptor. Windows opens
with reparse-point semantics, rejects reparse/special entries on the handle, and denies delete
sharing to pin the directory pathname while reading. An opened file is read into an immutable
snapshot; its digest, parsing and decoding use those same bytes. Verification never resolves an
arbitrary record-supplied pathname or walks arbitrary nesting: only the root and optional `assets`
directory are visited. Output ancestors remain trusted, and arbitrary concurrent content tampering
is outside the guarantee; observing snapshots is not a globally atomic snapshot of a live directory.
Enumeration is repeated after acquisition to refuse observed added or removed entries. Windows
path-based child access checks the retained root binding before and after access, refusing observed
ancestor relocation rather than following a replacement root.

The record's declared inventory contains only the result and present canonical assets. A successful
verification response reports those artifact identities in their declared order, followed by exactly
one `run.json` identity: its SHA-256 and exact length identify the same immutable bytes parsed and
checked during this verification. This is a separate observed-file list; the record contains no self
digest. The receiving contract requires unique `result.png` and `run.json`, permits only the canonical
mask and PSF roles, and applies each role's byte bound below. It does not authenticate producer claims.

The inventory is closed in both directions:

| Observation | Result |
|---|---|
| A declared file is missing | Refused |
| A declared file's size or digest differs | Refused |
| An opened file measures larger than its bound | Refused before reading its payload |
| A file is present that the record does not declare | Refused |
| A directory exists that no declared path implies | Refused |
| An entry is a symbolic link or any other special file | Refused without following it |
| The record is malformed, oversized, too deeply nested, or of an unsupported version | Refused |

Malformed records and artifact disagreements are `E_INPUT` at exit 3: the bundle is this command's
input. Resource refusal remains `E_RESOURCE`; cooperative cancellation remains `E_CANCELLED`.
Neither is relabeled as corrupt input.

The image must be a supported static, noninterlaced opaque PNG with the writer's minimal chunk
inventory. Continuous results use gray/RGB 8/16-bit samples and the deterministic canonical output
ICC profile specified in [PNG processing](formats/png.md). Verification checks exact canonical
profile bytes without applying a transform. Binary output is physical grayscale PNG1: logical 0 is
black, logical 1 is white. Original pre-combine unused row bits must be zero; general source decoding
has its separate admission rules. Recorded black/white counts agree with decoded logical samples.
Actual dimensions, channels, depth, profile presence and resolution must match the record.

The canonical mask is an 8-bit gray PNG containing only 0/1 samples, without ICC or resolution;
nonzero protects. Its dimensions and protected sample count agree with B-frame geometry. A
90/270-degree turn transposes output dimensions independently of the stored mask dimensions.
The optional PSF asset is decoded under its original bounded raw-kernel rules; normalized centroid
and dimensions agree with recorded PSF facts. Verification never reruns restoration or estimates blur.

Record bytes are bounded by 256 KiB, nesting by 16 and parser events by 2,048. Normalized PSF
coefficient vectors and original paths are absent from the record. The first excessive event stops
parsing before its bookkeeping or later tokens; discarded JSON cannot bypass the ceiling.
Directory enumeration is bounded by 64 entries. Result and mask encoded bytes are bounded by
256 MiB each; PSF encoded bytes by 1 MiB. Snapshots plus decoding use a separate 1 GiB charged-buffer
budget. Result, mask and PSF decoding happens sequentially, so their decoded planes need not coexist.
This validation budget applies to staging and later verification and is additional to live processing
charges. It is not a process-RSS bound: bounded JSON/container metadata, stream buffers and OS
resources are outside charged buffers. Source admission retains its 128 MiB encoded ceiling and
40-million-pixel limit. Reading/hashing use transfers of at most 64 KiB; PNG scans and sample loops
also have bounded checkpoints. Blocking native calls retain the existing cancellation limitations.

Verification never executes the recorded request, never reruns a method, and never opens a path the
record names outside the bundle.

## Preparing and reconciling publication

See [the shared architecture contract](architecture.md#preparing-and-reconciling-publication).

## What agreement is not

See [the shared architecture contract](architecture.md#what-agreement-is-not).

## Source observations and format compatibility

The run-record and command-response contracts each use version 15. Actual C++ Fields own their
closed wire shapes; native schema emission uses the same scalar domains, method descriptors and
compiled discovery catalogs as the writers. `tools/update_schemas.py` writes native-emitted bytes;
`schemas-current` compares the shipped schema artifacts with the current native emitter. Source
authoring alone does not establish that those generated artifacts or any platform have been qualified.
Obsolete forms and unknown fields are refused. There is no compatibility reader or migration.

The command response contains the full actual run-record value, its separate SHA-256 and execution
diagnostics. Processing settings and charged high-water bytes describe that execution; they are not
persistent algorithm parameters or a process-RSS bound. Failure diagnostics are null until the
executor has resolved actual settings. The full [response schema](../schemas/command-response.schema.json)
remains independently versioned from the [record schema](../schemas/run-record.schema.json).

D01 uses binary64 weights and accumulation, with bounded exact lookup-table observations. R01 records
bounded PSF summary facts and references a supplied kernel through its asset identity, without a
coefficient array. B01/B02/B03 classify the final common-pipeline luminance after one terminal Q16
quantization into physical PNG1. Protection excludes photometric analysis and preserves protected
photometric samples; terminal classification still classifies every pixel. Container source bytes
remain absent: later verification checks recorded facts and included artifacts, not original decoder
execution, authenticity or document meaning.
