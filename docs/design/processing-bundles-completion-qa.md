# Design QA: recording a run, and reading a bundle back

A review of [the completion design](processing-bundles-completion.md) against the merged tree.
Ten findings; the first four change it. Implementation follows the design **as amended here**.

## Findings that change the design

### F1 — A closed inventory that forgot its own directories rejects every bundle it writes

The design says a file the record does not declare fails verification. Walking a bundle that
contains a mask finds `assets/`, which the record never names as an artifact, so every bundle with
a mask would reject itself.

**Amendment.** The permitted directories are exactly the parent prefixes of the declared paths.
`assets/` is permitted precisely because `assets/protect-mask.png` is declared; any other directory
is undeclared and fails, as does any other file.

### F2 — "Closed" is a property of the file inventory, not of the JSON object

The design left this ambiguous, and the two readings differ sharply: rejecting unknown members
makes any future field a verification failure for older builds, while rejecting unknown files is
the property the feature actually needs.

**Amendment.** Unknown members of the record are tolerated; they cannot add an artifact, because
the inventory is derived only from members the reader knows. Unknown *files* are rejected. What is
closed is what the bundle contains.

### F3 — The assignable flag has to be replaced, not shadowed

`ConversionReport::verified = true` is a free statement after publication returns
([continuous.cpp:134](../../src/host/continuous.cpp)). Sourcing the record from the verification
while leaving that statement in place would give the same claim two owners, which is the defect
this feature exists to remove.

**Amendment.** The publishing step reports what verification performed, the record's
`output.verification` is that value, and the response's `verified` is derived from it. The free
assignment goes away rather than being fed better.

### F4 — The response should keep naming the image

The design left the response's `output` alone without saying so, and it is now the path of one file
inside a bundle. Changing it to the directory would be a contract change with no gain: the bundle
is the parent of that path, the record's own paths are relative, and the response gains a `record`
object carrying the run identity and the record's digest.

**Amendment.** `output` keeps naming `result.png`. Nothing else in the response moves.

## Decisions the design left open

### F5 — The ordered inventory is what makes the record trustworthy

Because files are written in the declared order and any failure abandons the whole transaction, a
`run.json` can only exist if the image and the mask were written, verified and identified first. It
is a property of the transaction rather than a rule the writer must remember, and a test asserts
that a failing image writer leaves no record.

### F6 — The stored mask records what was in force; it does not restate the original

The asset is the canonical mask: the decoded, oriented plane that actually excluded samples. Its
depth may differ from the supplied file, which is why the record carries two digests — `supplied`
for the file as given, `stored` for the asset in the bundle. Neither is a copy of the other, and
the bundle never claims the original.

### F7 — Protection is admitted only for continuous output

The application already refuses illumination and protection for other output modes
([illumination.cpp:91](../../src/app/illumination.cpp)), so `protection_supplied` can never be a
claim about a mask that was accepted and then ignored.

### F8 — Identifying a written file costs no budget

`identify_file` streams through a fixed buffer, so a 128 MiB output does not need 128 MiB of the
working budget to be hashed. It runs after verification, so a digest never describes bytes that
were not checked.

### F9 — The walk must refuse a link rather than resolve it

Entries are inspected with `symlink_status`, so a symbolic link, a directory where a file is
declared, or any other special entry is rejected without being followed. The walk is also bounded
in the number of entries it visits: an unbounded directory is itself a refusal.

### F10 — Reading a record uses its own bound, not the processing budget

`run.json` is read into a buffer bounded by `record_max_bytes`. Verification never allocates a
processing budget, because it never processes anything.

## Confirmed as sound

Assembling the record in `host`, injecting identity and time as one context, ordering the inventory
so the record comes last, and making verification a port over `app` all hold. Every verification
failure being `ErrorCode::input` at exit 3 is right: the bundle is that command's input, and exit 5
keeps meaning that this run's own output failed its decode-back comparison.
