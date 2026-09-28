# Design: recording a run, and reading a bundle back

The second half of [processing bundles](processing-bundles.md), as amended by
[its QA](processing-bundles-qa.md). The record type, its bounded reader and the inventory
transaction exist; what remains is filling a record from a real run, publishing it with the image,
and a command that reads a bundle back. Nothing here is implemented yet.

## Where the record is assembled

`host` is the only place that holds everything at once: the admitted request, the identified source
bytes, the typed execution reports, the written image and the mask actually in force. It assembles
`bundle::RunRecord`; `app` carries it; `report` renders the parts a response shows.

The publication call changes shape. Today `host` calls `io::publish_png_rows`, which stages,
encodes, verifies and commits one file. It will instead declare an inventory to
`io::publish_bundle`:

| Order | File | Written by |
|---|---|---|
| 1 | `result.png` | the existing encode-and-verify writer, then identified |
| 2 | `assets/protect-mask.png` | the canonical mask, encoded, verified and identified — only when a mask was supplied |
| 3 | `run.json` | `bundle::serialize`, last, because it carries the digests of the two above |

The order is load-bearing and the transaction preserves it. A record can only describe artifacts
that already exist.

## Identifying what was written

`io` gains `identify_file`, which streams a written file through the same digest with a fixed
buffer. It is used after verification, so a digest never describes bytes that were not checked.
Reading the file back to hash it is deliberate: hashing the bytes we intended to write would
identify our intent rather than the artifact.

The protection loader joins the two image loaders in returning the identity of the bytes it
consumed, so the record can state both the mask as supplied and the canonical mask stored.

## Run identity and time

`host::Processor` takes a `bundle::RunContext` — 128 bits from `std::random_device` rendered as
hex, and a UTC instant — produced at the composition root and injected in tests. Nothing below
`host` reads a clock or an entropy source, so a recorded run is reproducible under test.

The identity exists before the commit point. That is its purpose: after an ambiguous commit, the
destination's record can be read and matched against this run rather than guessed at.

## What the response adds

`app::Processed` and `app::ContinuousProcessed` gain the run identity and the record's own digest.
The response reports them under `record`, which is also where publication state stays — the record
on disk asserts nothing about publication, because it was written before the commit.

`ConversionReport::verified` stops being an assignable flag. The writer reports what verification
performed, and both the record's `output.verification` and the response's `verified` field are
derived from that one fact.

## `docenhance verify DIRECTORY [--json]`

A new command in `spec/cli-contract.json`, read-only, with the same shape as `methods [ID]`: a
positional subject and `--json`.

- `app::prepare_verify` admits the request; `app::Verifier` is a port, like `Processor`, so the
  application layer stays free of the filesystem and CLI fuzzers keep linking the pure application.
- `host::Verifier` reads `run.json` within its byte bound, parses it through `bundle::read_record`,
  and then checks the bundle against what the record declares.

What is checked, and what each failure means:

| Observation | Outcome |
|---|---|
| A declared file is missing | rejected: the bundle does not contain what it claims |
| A declared file's size or digest differs | rejected: the artifact is not the one recorded |
| A file is present that the record does not declare | rejected: a closed inventory is closed in both directions |
| An entry is a symbolic link, a directory where a file is declared, or any other special file | rejected without following it |
| The record is malformed, too large, too deep, or of an unsupported version | rejected by the reader |

Every failure is `ErrorCode::input`, exit 3: the bundle is this command's input. Exit 5 keeps its
meaning of this run's own output failing verification.

Verification never executes the recorded request, never reruns a method and never reads a path the
record names outside the bundle. It works after the directory is moved, because every declared path
is relative to the bundle root.

The result reports the run identity, the recorded instant and the artifacts confirmed. It does not
say the document is authentic: digests detect disagreement, they are not signatures, and anyone who
can rewrite an image and its record can produce another internally consistent bundle.

## Not in this package

No batching, no recipes, no new codecs or methods, no signatures, no repair of a bundle that fails
verification, and no rollback of a published directory.

## Questions the QA pass must answer

1. Does the mask asset change what protection meant, or only record it?
2. What happens when the record is written but the image writer already failed — and can the record
   ever describe an artifact that was not verified?
3. Is `verify` able to read a bundle this build did not write, and should it try?
4. What does a closed inventory do about a directory the bundle itself created, such as `assets/`?
5. Where does the response's `verified` field come from once the assignable flag is gone?
6. Can any failure leave a bundle published with a record that disagrees with it?
