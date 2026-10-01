# Current-contract and architecture audit

This audit covers first-party code and headers, reviewed/generated contracts, build configuration,
development tooling, tests and fuzz inputs, current documentation, release/package rules and
hidden repository configuration. Locked upstream sources and cached build artifacts are not
alternate project contracts. Historical changelog entries and obsolete-input rejection fixtures
remain useful evidence.

## Design pass

The record reader already accepts only version 3, requires source and denoising observations,
checks closed canonical claims and rejects `conversion.verified: false`. There is no record
migration engine or older-version dispatch. The format version continues identifying the current
contract; eliminating its number would not remove compatibility code.

Remove superseded entry points without forwarding aliases:

- Continuous acquisition uses `load_source`; remove `load_png_raster` and its PNG-only switch.
  The binary decoder retains its distinct stored-sample semantics and narrower admission.
- Product publication uses complete bundles. Remove public image-only publishers and their
  production writer wrappers. Keep verified image/row writers as codec operations inside the
  bundle transaction. Codec tests declare their own single-file transactions through that API.
- Remove unused `inspect_bundle`; replace generic `read_bundle_file` with `read_bundle_record`.
  Postcommit reconciliation needs the bounded immutable root record before full validation to
  distinguish another invocation from an integrity failure in this invocation.
- Remove the unused page-selection API, dedicated harness/dictionary/corpora and reference cases.
  Multipage input has no implemented product contract. Keep decimal parsing and its fuzz oracle.
- Remove the unused duplicate CLI contract edition and the obsolete OpenCV 4 configuration path.
  OpenCV remains locked to 5.0.0 with exact package admission.
- Derive PNG byte/pixel ceilings from the shared source constants, as JPEG already does.
  Snapshot acquisition uses the shared source authority directly, with no PNG API dependency.

The architecture's existing layer graph remains appropriate: application admission owns meaning,
codecs own representations, the host owns composition and validation, and I/O owns transactional
filesystem effects. The explicitly permitted JSON field-mapping interface keeps report and record
serialization shared; the architecture guide must describe that actual boundary.

## Separate design QA

Challenge removal against users and guarantees rather than text matches alone. Codec-budget,
readback, corruption, cancellation-cutoff, cleanup and uncertain-publication tests depend on the
old image-only wrappers. Move those exercises to the existing transaction, preserving their native
commit seams and assertions. Full bundle/executable tests continue exercising product records.
Do not replace record-first reconciliation with unconditional full validation: that loses the
other-invocation versus integrity-failure distinction. Keep root handles and immutable snapshots.

The decimal parser's old `strtod` conversion reads the embedding process locale, contradicting its
contract. A floating `from_chars` replacement passed the standalone reference test but failed the
macOS 14 deployment build because libc++ makes it available only on macOS 26. Use one bounded
standard-library conversion with an explicit classic locale instead. Retain grammar scanning,
finite/range checks, overflow/underflow refusal, signed zero and the independent conversion oracle.
Linux QA found that libstdc++ stream conversion may round a tiny nonzero decimal to zero without
a failure flag. Check the converted representation explicitly: reject subnormal results and a
zero result with a nonzero mantissa. This is one numerical admission rule on every platform.
Enable backend-error exceptions so an allocation failure during extraction reaches the existing
exception boundary instead of becoming an argument error.
No platform-version dispatch, native locale adapter or extra dependency is necessary.

Current `--version` and CMake target aliases are deliberate interfaces, not retired-project
forwarders. OS/compiler support, ICC interpretation, PNG/JPEG admission and algorithmic applicability
are current external/domain requirements. Preserve them. Source compatibility breaks retire the
removed project APIs directly; no deprecation period, shim or migration is provided.

Current documentation also contained historical false-verification admission and pre-JPEG/pre-D01
capability claims. Correct those descriptions and require root help to list `verify` alongside the
other implemented commands. Actual verification results belong to the delivered commit and logs;
this audit document does not certify unexecuted platforms or eliminate all possible future debt.
