# Design QA: verifiable processing bundles

A review of [the design](processing-bundles.md) against the merged tree, not against itself. Ten
findings; six change the design, four bind decisions it left open. Implementation follows the design
**as amended here**.

## Findings that change the design

### F1 — `app` should use `bundle`; the design avoided a cycle that does not exist

The design had `app` hold the run identity and manifest digest as loose strings, reasoning that
`bundle` must stay out of `app`. But `bundle` uses `core`, `contract`, `image` and `methods` — never
`app` — so `app → bundle` is acyclic. Passing strings would mean the record is assembled in one
place and its identity re-stated in another.

**Amendment.** `app` uses `bundle`, and `app::PublishedImage` carries the typed `bundle::RunRecord`.
One assembly site, one type.

### F2 — "both paths verify" would create two verifiers

The design requires binary output to earn the same claim as continuous output but does not say how.
The obvious route — a plane-shaped decode-back check beside the existing row-shaped one — states the
guarantee twice, which is the defect this feature exists to remove.

**Amendment.** Adapt the binary plane to `image::RowSource` and reuse `verify_png_rows` unchanged
([png_verify_rows.cpp:103](../../src/io/png_verify_rows.cpp)). The adapter presents exactly the
samples the encoder wrote. One verifier, and B02/B03 arithmetic is untouched because verification
reads what was encoded rather than recomputing it.

### F3 — `verified` must be a consequence, not an assignable field

`ConversionReport::verified` is set by a statement after publication returns
([continuous.cpp:134](../../src/host/continuous.cpp)). A record built from that field inherits a
claim that a second statement could make without any verification happening.

**Amendment.** The verification step returns evidence, and the record's `output` section is
constructed from that evidence. The response keeps its field, sourced from the same evidence rather
than set independently. Nothing writes `verified = true` as a free statement.

### F4 — The record needs a time and an identity, and the design named neither source

A processing record without a time is of little use, and reconciliation after an ambiguous commit
needs an identity that exists *before* the commit. Both are environment inputs, which no layer below
the composition root may read.

**Amendment.** One injected `RunContext` — 128 bits of identity from `std::random_device`, rendered
hex, plus a UTC timestamp — produced in `host` and injected in tests. Nothing below `host` reads a
clock or an entropy source, so records stay reproducible under test.

### F5 — "bounded parsing" has to mean something specific, or `verify` inherits two hazards

nlohmann throws on malformed input and parses by recursive descent. As written, `bundle` would need
`may_catch` for an avoidable reason, and a deeply nested bundle could exhaust the stack before any
validation ran.

**Amendment.** Parse with `allow_exceptions = false` through the SAX interface with an explicit
depth limit and a byte cap, rejecting the document at the first violation. `bundle` then needs no
exception permission, and depth is refused rather than survived.

### F6 — A failed verification had no exit code

`output_verify` (exit 5) means *this run's own output* failed decode-back. A supplied bundle that
disagrees with its record is a different event.

**Amendment.** The bundle is the command's input, so verification failure is `ErrorCode::input`,
exit 3, as is a malformed or unreadable record. No new error code, and exit 5 keeps its meaning.

## Decisions the design left open

### F7 — An undeclared file must fail verification

A closed record is only meaningful if the inventory is closed in both directions. A declared file
that is missing and an undeclared file that is present are different failures and must read
differently; neither is a pass.

### F8 — Serialization order is load-bearing

The record contains the output digest, so it can only be serialized after encode, verify and digest
— inside staging, before the cancellation cutoff. A serialization failure there is a clean abandon
with nothing published. This ordering is a requirement, not an implementation detail.

### F9 — The guard against `bundle` becoming a second brain

`bundle` serializes values that already exist and defines no default, no option table and no
processing rule of its own. A field it cannot obtain from a typed value is evidence that the type is
missing a fact — the fix is to the type, never a literal in the serializer.

### F10 — The record says what was verified, not merely that it was

`"verified": true` invites a reader to conclude more than decode-back establishes. The `output`
section names the verification performed — the encoded image was reopened and compared against the
intended integer samples and metadata — so a later reader cannot over-read a boolean.

## Confirmed as sound

The layer split (`bundle` owning the format for both writer and verifier), digests in `io` where
the bytes already are, `io` never learning JSON, and verification as an application port over
`host` all hold. Generalizing `publish_generated_png` from one hard-coded name to a declared
inventory preserves its exclusive-rename commit, its cancellation cutoff and its non-recursive
cleanup, which are the parts worth keeping.

## Ending state of these two documents

Both are working documents on the feature branch. Before the pull request they are removed and
replaced by a current-state document under `docs/` describing what exists, because shipped
documentation describes the product rather than its plan.
