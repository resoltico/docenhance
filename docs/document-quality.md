# Independent document-quality evaluation

This protocol asks whether a specific explicit method/composition preserves document marks and
improves readability on its intended damaged classes against an unenhanced representation.
Engineering CI, deterministic mathematical references and `verify` remain separate evidence.
No general natural-document improvement, meaning or authenticity is claimed without completed
independent held-out review. A successful processing command alone cannot establish no harm.

## Governed corpus and frozen experiment

The [public corpus policy](corpus.md) governs source-image admission, provenance and redistribution;
`corpus/manifest.json` identifies the included inputs and their separate licenses. Keep evaluation
plans, outputs and reviews in a governed experiment directory.
`spec/document-quality.schema.json` defines the closed evaluation manifest. Preserve each input's
original bytes, source URL, institution/creator, rights basis, evidence and named curator. Permission
fields are attestations requiring independent rights review, not a legal warranty inferred from a
hash or an automated acquisition assessment. Do not include private user documents, credentials or
uncleared third-party material. An image's publication in the technical corpus does not establish
its admission to a frozen held-out evaluation.

The closed plan and report use protocol version 2; obsolete shapes are refused. Freeze one
`protocol` object before opening held-out outputs. Its SHA-256 binds source/build/executable
identities, the separately identified corpus, exact per-page candidate/comparator/replicate argv,
mask/PSF asset bytes, panel membership, review-protocol bytes, blinded-assignment key and fixed
class/claim policy. Canonical bytes are strict UTF-8 compact JSON with sorted keys and unchanged
array/argv order. Duplicate decoded keys, nonfinite values and invalid Unicode are refused.
Its own digest, subsequent executions/reviews/revisions and independent preregistration receipt
remain outside that object. The separately byte-identified receipt must contain precisely
`protocol_sha256`, referencing the frozen protocol digest. Hashes cannot prove independence,
rights, natural provenance or that registration happened before anyone saw outputs.

`frozen_corpus_sha256` separately binds the original corpus list. Each sample declares
`source_kind` as `natural_document` or `synthetic_control`; synthetic controls can exercise
engineering correctness but always abstain from natural-document claims. Natural declarations
require independent curator/rights review. `document_id` groups re-encodings/crops of one physical
page: they cannot cross tuning/held-out splits or inflate independent held-out counts.
Explicit `tuning.performed=false` is valid and admits no tuning inputs or receipt. Declared tuning
requires cleared tuning inputs and a byte-identified receipt containing their exact
`tuning_corpus_sha256`; undeclared tuning is invalid. Build provenance references bind the source
commit, executable digest and actual build facts, subject to independent build/CI review.

Per-page settings and assets may differ when frozen before evaluation. The retained assignment
maps each unique blind pair to its physical sample, the complete panel and `candidate_side`
(left/right); its frozen byte identity binds those choices without claiming to prove randomness.

Cover handwriting, print, mixed marks, age/degradation, alpha, color text, stains, bleed-through,
skew, oversharpening, already-good pages, faint marks and exact-preservation controls. The schema
owns class names. Container/alpha adaptations of a historical scan remain labeled derivatives;
they do not become additional independent natural pages or justify empirical alpha-class benefit.
Require multiple independent pages per intended class and retain unsuccessful/refused inputs.
Do not cherry-pick easier pages after looking at outputs or report only an aggregate benefit score.

## Comparator, execution and exact preservation

Produce an unenhanced preserve/gray comparator with the same admitted representation, orientation,
quarter-turn, alpha/profile/depth policy and source bytes. Binary candidates retain their own
stored-gray semantics and cannot be given protection or continuous-stage options. Produce a second
candidate run with exactly the frozen settings. Store complete responses, records, source/build
provenance, output identities and protected-mask assets with each run.

`tools/evaluate_document_quality.py` requires a complete manifest, hashes actual source and
executable bytes, invokes real read-only bundle verification, requires distinct bundle/run identities
and compares every semantic record section: source, request, execution, protection, output and build.
It independently decodes integer PNG samples, checks geometry and protected destinations against
the unenhanced comparator, and actually replays frozen candidate, comparator and replicate settings
with the selected executable into newly owned retained replay directories. All six semantic sections must match
the scored bundles; artifact agreement cannot invent an unenhanced comparator. The build commit
binding still needs independently reviewed build/CI provenance: a declared SHA or executable hash
alone cannot prove which source built it.

```sh
python tools/evaluate_document_quality.py --manifest /cleared/corpus/evaluation.json \
  --executable out/release/bin/docenhance --out /cleared/corpus/evaluation-report.json
```

Use the source checkout and pinned developer environment; this harness is not a product runtime
component and performs no acquisition, OCR, neural scoring or automatic method selection.
After valid protocol admission the evaluator retains every intended page and every role as
`completed`, `refused`, `failed` or `missing_evidence`. Raw stdout/stderr digests, exact actual argv,
response, process exit/timeout and effect evidence remain separate from human review completeness.
Missing evidence yields a complete factual abstention report rather than dropping a denominator or
aborting on the first page. Completed roles require full response/record schema validation,
delivered run/record/output identity agreement, real verification/replay and integer fidelity.
Replay substitutes only its owned output destination. Each attempted replay reports actual argv,
complete raw stdout/stderr identities, exit/timeout, parsed response/effect, errors and retained
artifact-directory location. The existing joined subprocess owner provides bounded waits and
separate captured streams. Original and replay response interpretation is bounded to 1 MiB; JSON nesting is bounded to 64 levels; oversized raw output is
still retained and byte-identified, with unknown effect rather than fabricated success. Successful
and failed replay artifacts remain in their fresh corpus-local directories for inspection; there
is no automatic retry or cleanup of uncertain outputs. Declared original-role evidence stays
separate from these actual later attempts.

Actual subprocess return codes are retained as integers in the range -2147483648..4294967295,
including negative POSIX signal returns and unsigned Windows statuses; they are never normalized
into shell exit codes. Exits outside the documented delivered product outcomes, including 5/7,
signal/abnormal termination, timeout or incomplete responses, cannot establish no effect. Confirmed
completed publication survives later command/delivery failure; otherwise ambiguous effects remain
unknown. Failed/refused/ambiguous roles are never automatically replayed. The report retains
original role declarations even when incoherent evidence forces abstention. Per-class engineering
failures, refusals, missing evidence and incomplete human review are reported separately from harms,
uncertainty and class acceptance. The report has its own digest, separate from protocol and corpus.

Reports require a fresh output path; existing sources and reports are never overwritten. Negative controls must include an erased faint mark, clipped glyph,
new contour, degraded good page, geometry/protection mismatch and altered source/build/output bytes.

## Blinded human review and conservative decision

Use at least three independent human reviewers, none of whom selected/tuned the candidate. A
curator assigns opaque pair IDs, conceals method/build identity, randomizes left/right presentation
using a retained assignment key, and shows full-resolution pages plus preregistered faint-mark
regions. Keep originals accessible to the curator. Reviewers independently score paired readability
from -2 (materially worse) to +2 (materially better), explain their judgment, mark uncertainty and
record lost marks, clipped glyphs, artificial contours, degraded good pages or geometric/protection
harm. The full panel reviews every held-out pair exactly once. Do not put synthetic or model-authored
judgments into this panel. A curator must verify reviewer independence and assignment-key integrity.

Any observed material harm or negative score refuses the affected class regardless of mean benefit.
Disagreement or uncertainty yields abstention; adjudicate through a fresh blinded review, preserve
all prior judgments, and issue a separately identified report. Retained revision references are
byte-checked and parsed as closed `{sample, reviews}` objects bound to the frozen pair/panel.
Current reviews, prior revision references and their judgments remain in the report. Prior
negative/harm/uncertain judgments block acceptance under this conservative unchanged protocol;
a later majority or omitted revision cannot erase them. Do not overwrite dissent or resolve
it by majority vote. For intended damaged classes, require positive unanimous paired judgments and
an exact one-sided paired sign tail no greater than 0.05 divided by the complete schema class count.
Ties count as nonpositive. Already-good and preservation controls require complete, certain no-harm
review from at least two independent physical pages per control class; improvement is not required. This conservative family error bound is a protocol choice,
not a guarantee of population representativeness. Class overlap is not extra independent evidence.

A missing class, inadequate evidence, tuning leakage, missing permission, missing reviewer or any
material regression blocks acceptance. `abstain` is an evaluation result: it does not introduce
new automatic runtime behavior or manufacture a successful no-op. Parameter/model changes require
fresh execution/review against the same locked holdout, with the prior results retained.

## Current evidence boundary

No human panel has been furnished for this redesign. The [public technical corpus](corpus.md) can
support processing, deterministic repetition and preservation checks; it does not constitute a
blinded readability evaluation or representative coverage. Missing class coverage or human reviews
blocks measured natural-document/no-harm release claims. Correctness CI can pass while that
product-quality evidence remains incomplete. Reports must identify this distinction explicitly.

