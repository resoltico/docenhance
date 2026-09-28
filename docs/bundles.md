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
complete directory is committed with one exclusive rename. A failure anywhere publishes nothing:
there is no result that succeeded while its record failed. Cleanup removes only what the
invocation created, never a foreign entry and never a directory it did not make.

The record comes last because it carries the digests of the files before it. A `run.json` therefore
exists only if the image — and the mask, when there is one — was written, verified and identified
first. Each file is identified by reading it back under the same bound verification applies, so
what this program publishes is something it can read: an artifact larger than a bundle may hold is
refused before the commit point rather than published into a bundle `verify` would reject.

## What the record says

`run.json` is a closed, versioned object. Every field comes from a value the execution already
held; the bundle layer that writes it defines no default, no option table and no processing rule of
its own.

| Section | Establishes |
|---|---|
| `record` | The record format version, the run identity and the instant it was recorded |
| `build` | Application version, platform, compiler and dependency-lock identity |
| `source` | The digest and size of the exact encoded bytes that were decoded, and the file's name |
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

The inventory is closed in both directions:

| Observation | Result |
|---|---|
| A declared file is missing | Refused |
| A declared file's size or digest differs | Refused |
| A file is larger than a bundle may hold | Refused without reading past the bound |
| A file is present that the record does not declare | Refused |
| A directory exists that no declared path implies | Refused |
| An entry is a symbolic link or any other special file | Refused without following it |
| The record is malformed, oversized, too deeply nested, or of an unsupported version | Refused |

Every refusal is `E_ARGUMENT` at exit 3: the bundle is this command's input. Exit 5 keeps its
meaning of a run's own output failing its decode-back comparison.

Verification never executes the recorded request, never reruns a method, and never opens a path the
record names outside the bundle.

## What agreement is not

A digest detects disagreement with expected bytes. It is not a signature: it identifies no author,
and anyone able to rewrite both an artifact and its record can produce another internally
consistent bundle. Confirming a bundle establishes that its artifacts are the ones its record
names — traceable processing, not authenticated evidence, and not a statement about the meaning or
authenticity of the original document.
