# Architecture, types, ownership and resource accounting audit

## Design pass

Trace the reviewed layer graph through admission, processing, row production, native adapters,
publication and reporting. Keep numeric kernels free of allocation expressions and effects;
allocation belongs to the shared ledger and native adapters. Preserve complete-bundle validation,
unknown publication, worker joins and the final cancellation cutoff.

The ledger already has shared lifetime, atomic charges and subtraction-before-addition refusal.
Checked planes reject overflow before allocation, checked views validate their last occupied row,
and numerical kernels reject overlapping backing spans. Codec owners live outside jump frames;
color profiles/transforms are destroyed before their context. Native allocation registries are
finite and fail closed. These owners remain; replacing them with a universal execution graph or
shared image ownership would add mechanisms without fixing the observed defects.

Remedies at the owning boundaries:

- Borrowed buffer bytes and owning-plane views require a live lvalue owner. Shape access returns
  a small value. WorkRef accepts only lvalue callables; synchronous scheduler calls name their
  tasks. Color converter creation refuses a temporary raster rather than retaining its address.
- Retained illumination row execution owns its cancellation token; prepared denoising refuses
  temporary plane owners. Publication reads conversion through a typed borrowed converter,
  eliminating the independent callback/void-pointer pair.
- The processing port returns a closed binary/continuous success alternative. Continuous success
  carries required conversion/I01/D01 observations together; binary success carries no continuous
  fields. Admission still verifies agreement with the selected request and preserves unknown
  publication when a processor reports the wrong alternative or inconsistent observations.
- Native NLM returns its completed reservation and combined charge observations from inside the
  reservation lifetime. Tile orchestration no longer predicts charges or recomputes scratch.
  The native-free estimator remains in methods, with its existing bounded mathematics.
- Tile traversal advances by each remaining bounded region, so the last partial region cannot
  wrap a uint32 extent. Shared encoded-byte and linear-block limits have one owner.

## Separate design QA

Before implementation, real ASan counterexamples demonstrate heap-use-after-free when borrowing
bytes from a temporary Buffer and stack-use-after-scope when retaining WorkRef of a temporary
capturing callable. Reject those constructions at compile time, including const temporaries,
while permitting temporary view wrappers of live owners. Copies of WorkRef must keep referring
only to the original callable; started workers must still join before its enclosing scope ends.

Challenge the closed success alternative with wrong-family processors and incomplete or
contradictory stage observations. A variant alone does not establish verified output or matching
request parameters. Keep runtime cross-field validation and the prepared unknown outcome.

Challenge reservations at exact and one-byte-short budgets, maximum-size ledger arithmetic,
move replacement, concurrent allocation and refund. The successful native observation is taken
while its lease is live; failed attempts do not create completed-call evidence. This remains a
charged-buffer/reservation limit, not process RSS or a wall-clock deadline.

The bounded native estimator has width/height <=310, patch <=15 and search <=41; its products fit
supported 64-bit size_t before OpenCV casts. Source pixels remain limited to 40 million. Do not
allocate a huge page merely to test traversal: test the region arithmetic at UINT32_MAX and test
real partial tiles against the existing independent full-native comparison.

A borrow restriction cannot prevent a caller from explicitly destroying/moving an lvalue owner
while its views are in use. Document that lifetime rule; do not introduce a retaining pointer
framework or claim complete static lifetime safety. No wire-format migration or backward alias
is needed: this changes the native processing-port contract and all current consumers together.

Fresh native verification exposed an ignored CMP0091 default-policy argument sent to the current
application child. Keep it only for older upstream recipients that consume it. Every owning
cache is recreated for this recipe change; no cache migration or check relaxation is introduced.

Further allocation tracing found an input-sized std::vector copy in the public percentile
primitive, outside the charged owner. I01 also implemented the same nearest-rank selection.
Consolidate in-place selection in image using caller-owned scratch, validate before mutation,
propagate failures through I01, and compare against a separately copied/sorted fuzz oracle.
An independent allocation observer must confirm zero valid-path selection allocations; rejection
fixtures must preserve input. No numerical percentile definition or enhancement semantics change.

The publication boundary accepted null writer callbacks and an arbitrary number/depth of owned
entries. Validate nonnull writers and the file-table ceiling before acquiring a stage; apply the
same native inventory ceiling while creating owned entries. Native path components must remain
one relative filename, including on Windows. Rejection tests check no staging for invalid tables
and confirmed cleanup/refusal for excessive depth. Host bundle meanings remain in the host;
I/O still owns effects and bookkeeping, with no added layer edge.

The allocation observation plumbing is separate from the numerical/native experiment, so one
real interposition/hook owner checks both percentile selection and OpenCV allocations without
copying the mechanism or weakening size/diagnostic gates. Existing code-bound ABI exceptions
move with their exact allocation implementation; no broader suppression is introduced.

Capability formatting now derives both supported-format names and mode descriptions from one
support table. Responses reuse core::build_facts, which records already used, instead of a
second manually assembled build identity. These are representation facts, with no new processing
admission, method, format or layer permission. Real response/record and capability tests verify
those dependents together.

The standalone reference runner must include core's actual build-facts reader too. Generate its
header through real CMake compiler detection and the same production version template, using the
project version and exact lock digest. Do not replace the reader with a fake test implementation
or exclude generated-header users from the source closure. These facts identify that standalone
reference build; they do not substitute for a complete product workflow or its deployment checks.

Final move-state QA found that SurfaceModel retained active/grid metadata after moving away its
logarithm plane. Its move operations now empty the source metadata with the storage, just as
Plane does. A regression checks retained values, inactive/empty origin, rejected sample/application
access, and move replacement. This changes no fit, correction, report or publication mathematics.

D01's quantization tie offset used the unrelated default blend constant. Name the half-sample
rounding term separately and derive byte/word scaling from the representation's sample maxima.
The values and floating operations are unchanged; independent correction/quantization fixtures
continue to verify the specified mathematics.

JPEG's conservative bootstrap charge was represented by an unused 64 KiB dummy Buffer.
Reuse core::Reservation for this native-owned storage instead: the charge precedes native creation
and lives until after context destruction, with the same ceiling/refund/peak semantics and no
extra dummy payload allocation. Existing exhaustive decoder-budget/cancellation fixtures cover
refusal and refund through the actual native decoder.

An ASan copy counterexample caught a flaw in the initial lvalue-only WorkRef design: its callable
constructor could win overload resolution when copying a mutable WorkRef, retaining the wrapper
instead of its callable. Exclude WorkRef itself from that constructor, retain normal copy/move
semantics, and test a mutable copy after its originating wrapper leaves scope. The same real
counterexample must run cleanly after the fix; no wrapper lifetime chain is retained.

Cross-platform QA found a clang-tidy trailing-comma disagreement for a nested one-element test
array under the runner's newer macOS SDK. Name the file descriptor and use a simple array
initializer; retain the exact test, warning requirement and platform checks without suppression.
