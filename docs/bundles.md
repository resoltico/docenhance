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
```

The files are written into a staging directory this invocation owns, in that order, and the
complete directory is committed with one exclusive rename. A failure before commit prevents publication. A successful rename establishes completed publication;
a later integrity failure does not undo that fact. Cleanup removes only what the
invocation created, never a foreign entry and never a directory it did not make.
Effect paths are anchored before callbacks without lexical normalization; response spelling retains
the admitted directory text. Windows drive-relative publication paths such as `C:result` are refused;
use a fully qualified drive path or an ordinary path relative to the operation's initial directory.
POSIX staging directories and files are created with owner-only modes, not chmodded after creation.

The record comes last because it carries the digests of the files before it. A `run.json` therefore
exists only if the image — and the mask, when there is one — was written, verified and identified
first. Each file is identified by reading it back under the same bound verification applies, so
what this program publishes is something it can read: an artifact larger than a bundle may hold is
refused before the commit point rather than published into a bundle `verify` would reject.

## What the record says

`run.json` is a closed, versioned object. Version 3 admits exactly the supported binary or
continuous operation, with reviewed method versions and validated parameters. Missing or unknown
fields, duplicate object keys (including escaped equivalents), out-of-range numbers and inconsistent
observations are refused. Numeric domains are checked before narrowing. Run identities are 32
lowercase hexadecimal characters; recorded instants use `YYYY-MM-DDTHH:MM:SSZ` with a valid
calendar date. Identity text is strict UTF-8 and is never repaired. Each admitted execution takes its own
run identity and instant, including repeated calls through the same native processor. Portable
artifact paths are at most 128 UTF-8 bytes, separated by `/`, without empty, `.` or `..` components,
NUL, backslash or colon. This domain is shared by staging, record reading and application observations.

The generated [run-record schema](../schemas/run-record.schema.json) is shipped with the response
schema. The typed reader remains authoritative and also checks relationships that JSON Schema
cannot express, such as matching dimensions, counts, derived fractions and conversion warnings.
Independent schema tests check records emitted by the real executable and negative examples.

 Every field comes from a value the execution already
held; the bundle layer that writes it defines no default, no option table and no processing rule of
its own.

| Section | Establishes |
|---|---|
| `record` | The record format version, the run identity and the instant it was recorded |
| `build` | Application version, platform, compiler and dependency-lock identity |
| `source` | The digest/size of exact decoded source bytes and basename; required typed container/decode observations |
| `request` | The admitted operation and its parameters, and whether a mask was supplied |
| `execution` | The conversion observations and the illumination report, as the response states them |
| `protection` | The supplied mask's digest, the stored canonical mask, its size, polarity and coordinate frame |
| `output` | The result's relative path, digest, size, dimensions, depth, profile presence and the verification performed |

Two properties are deliberate:

**The record describes processing, never publication.** It is finalized before the commit point, so
it cannot assert that publication succeeded. Publication state belongs to the command response,
which also reports the run identity and the record's own digest — the pair that lets an ambiguous
commit be reconciled against whatever is on disk.

**The record never carries its own digest.** Embedding it would require hashing bytes that contain
the hash. The response reports it instead.

The `output.verification` section names what was performed rather than asserting a bare truth: the
encoded image was reopened and compared against the intended integer samples and metadata. Both the
continuous-tone and binary paths do this before publication, so the claim means the same thing for
either output.

## Identity is taken from the bytes that were used

A source digest identifies the immutable snapshot the decoder consumed, not a later reading of the
same path. The output digest is taken from the written file after it was verified. A mask carries
two digests, because they answer different questions: `supplied` identifies the file as given, and
`stored` identifies the canonical mask in the bundle — the oriented plane that actually excluded
samples, which may differ in depth from the file supplied.

Absolute source paths, shell text and usernames are not recorded. A retained file name can still
carry personal information, so a bundle is not anonymized, and the original document is never
copied into it.

## Reading a bundle back

```sh
docenhance verify DIRECTORY [--json]
```

Verification is read-only. It reads `run.json` within its byte bound, parses it without exceptions
and with an explicit nesting limit, and checks the bundle against what the record declares. It
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

The image must be a supported static, noninterlaced gray/RGB PNG with 8/16-bit samples and the
writer's minimal chunk inventory. Its actual dimensions, channels, depth, profile presence and
resolution must match the record. Continuous output must carry the deterministic canonical output ICC profile specified in
[PNG processing](png-processing.md). Framing, supported class, sample-space agreement and exact
canonical profile bytes are checked using the writer's profile definition, without applying a
transform. Binary results additionally contain only
0/255 samples. The stored mask is an 8-bit gray PNG containing only 0/1 samples, without a profile
or resolution; nonzero protects. Its dimensions, fixed polarity/frame declarations and protected
sample count must agree with the oriented output and execution observations.

Record bytes are bounded by 1 MiB, nesting by 16, and parser events by 1024. The first excessive
event stops parsing before its bookkeeping or later tokens; discarded JSON does not bypass that
ceiling. Directory enumeration
is bounded by 64 entries, each artifact by 256 MiB, and snapshots plus decoding by a separate 1 GiB
charged-buffer budget. The result and mask are decoded sequentially; their decoded planes need
not coexist. This validation budget applies to staging and later verification and is additional
to any live processing budget. It is not a process-RSS bound: bounded JSON/container metadata,
stream buffers and OS resources are outside charged buffers. The result decoder uses the 256 MiB
artifact byte domain; source admission retains its 128 MiB ceiling and 40-million-pixel limit.
Reading and hashing observe
cancellation in transfers of at most 64 KiB; PNG scans and sample loops also have bounded checkpoints.
Blocking native calls remain subject to the existing cooperative-cancellation limitations.

Continuous conversion reports require `verified: true`, agreeing with the completed
`output.verification` comparison. Historical false verification claims are refused.

Verification never executes the recorded request, never reruns a method, and never opens a path the
record names outside the bundle.

## Preparing and reconciling publication

Every writer checks write, flush and close completion, including returned cancellation paths.
Manifest transfers and PNG reread sample comparisons observe cancellation in at most 64 KiB
blocks. A delayed stream failure takes precedence over cancellation; earlier genuine errors remain
primary. Before the final cancellation cutoff, the
host rereads and validates the staged bundle using the same reader and artifact checks as `verify`.
It compares the exact manifest digest and run identity with the values this invocation prepared.
I/O owns the native transaction and invokes one host validator before and after commit; it contains no processing
record parser or method admission rules.

After the exclusive rename, I/O checks that the destination is the retained owned directory object.
The host validates this run identity, exact manifest digest and complete artifacts through one native
bundle root without a separate record reopening. Observation after the cutoff does not accept cancellation
as a replacement for an irreversible outcome.

| Commit and observation | Outcome |
|---|---|
| Successful rename and matching complete bundle | Success, `completed` |
| Ambiguous rename error, matching owned native directory and matching complete bundle | Success, `completed` |
| Conclusive refusal | Output failure; clean only proven-owned staging |
| Ambiguous error with absent, foreign or unreadable destination | `E_PUBLICATION_UNKNOWN`, exit 7, `unknown`; preserve potentially relevant staging |
| Owned directory is at the destination but record/artifacts are unavailable or disagree | `E_OUTPUT_VERIFY`, exit 5, `completed`; retain the published directory |
| Successful rename followed by unavailable or disagreeing inspection | `E_OUTPUT_VERIFY`, exit 5, `completed`; retain the published directory |

Known publication is never downgraded to uncertainty by a later inspection failure. A foreign
destination is never modified. Staging retention also disables destructor cleanup. Native object
identities captured when files are created guard cleanup against replacement of a known name;
replacement or unverifiable ownership preserves the entry and produces uncertainty. Cleanup remains
nonrecursive and is not a guarantee against arbitrary simultaneous tampering by an equally
privileged process. There is no rollback, automatic processing retry, copy fallback or stale-stage
sweep, and atomic visibility still does not promise crash durability.
Native leases keep object identifiers from being recycled while checks are live. The transaction
retains at most 65 metadata handles: one root and at most 64 bounded owned entries. Windows compares
the full 128-bit file identifier and volume identity, with object leases opened by ID so descendant
path handles do not prevent directory commit. Windows publication requires native open-by-ID support;
SMB output destinations are refused. Use a supported local volume. Unsupported identity observation
or object opening fails closed.
Root, parent and created-entry identities are checked around writer/validation callbacks and the
commit cutoff. Known replacement prevents further effects, and copied record bytes cannot prove
publication origin. Rename/unlink cannot atomically compare a caller-supplied object identity; these
checks remain outside a guarantee against arbitrary equally privileged namespace changes between
checks and native operations.

## What agreement is not

A digest detects disagreement with expected bytes. It is not a signature: it identifies no author,
and anyone able to rewrite both an artifact and its record can produce another internally
consistent bundle. Confirming a bundle establishes a structurally valid supported record and agreement with the
observable properties of its included artifacts. Source identity, build identity, parameter use,
processing observations and the producer's original sample comparison remain recorded historical
claims; verification cannot establish those events without the original execution. It never reruns
illumination or binarization. Agreement is not a statement about the meaning or authenticity of the
original document.

## Source observations and format compatibility

Current native output uses record version 5. It requires closed source decoding observations,
a denoising request and complete execution report, alongside verified conversion and output facts.
The reader accepts only this format; obsolete versions and unknown fields are refused. No backward
compatibility reader or migration exists. The command response uses schema version 5.

D01 observations include typed settings, native float strength, 16-bit/L1 policy, global reflection,
tile size, eligible/protected/evaluated/corrected/changed pixels, completed native calls and reserved/
combined preparation charge peaks. Reader validation checks method settings, completion, count
relationships, exact tile count and source-derived native reservation. Working-space change is not
inferred from whether output bytes differ. See [denoising](denoising.md).

JPEG and PNG source bytes remain absent: later verification checks recorded facts and included
artifacts, not historical decoder execution or authenticity.
