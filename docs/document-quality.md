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

The closed plan uses protocol version 3 and requires `protocol.evidence_contract` equal to
`docenhance-quality-v3` **inside** the canonical hashed protocol. Completed output is one closed
`quality_run` container of that version and contract, containing the intended plan, immutable
referenced snapshots, fixed invocation index, events, nested factual report and terminal accounting.
A bare report, protocol2 artifact, outer-only version flip or reused old preregistration receipt is
refused. There is no compatibility reader or migration. Historical evidence remains unchanged.

Freeze the protocol before opening held-out outputs. Its SHA-256 binds source/build/executable
identities, corpus, exact per-page candidate/comparator/replicate argv, supplied mask/PSF bytes,
panel, review-protocol reference, blinded assignment and fixed class/claim policy. Canonical bytes
are strict UTF-8 compact JSON with sorted keys and unchanged array/argv order. Duplicate decoded
keys, nonfinite values and invalid Unicode are refused. Original evidence nesting remains limited
to64 levels. The completed-run reader permits exactly four additional enclosing levels (68 total),
then independently enforces the original64-level plan/reference domains and the closed v3 wrapper
and semantic relationships. This allowance does not extend raw response or metadata admission.
Subsequent executions/current and prior reviews and the independent receipt remain outside the
protocol hash, but are retained in the container. The separately byte-identified receipt is exactly
`{"protocol_sha256": H}`, with the new protocol digest. Hashes cannot prove independence, rights,
natural provenance or registration chronology.

`frozen_corpus_sha256` separately binds the original corpus list. Each sample declares
`source_kind` as `natural_document` or `synthetic_control`; synthetic controls can exercise
engineering correctness but always abstain from natural-document claims. Natural declarations
require independent curator/rights review. `document_id` groups re-encodings/crops of one physical
page: they cannot cross tuning/held-out splits or inflate independent held-out counts.
Explicit `tuning.performed=false` is valid and admits no tuning inputs or receipt. Declared tuning
requires cleared tuning inputs and a byte-identified receipt containing their exact
`tuning_corpus_sha256`; undeclared tuning is invalid. Build provenance references bind the source
commit, executable digest and actual build facts, subject to independent build/CI review.

The same independently referenced build receipt has exactly `source_commit`, `executable_sha256`,
`build` and `capture_scope`. `build` remains the exact native RecordedBuild value. `capture_scope`
contains `native_producer` and `interpreter` using the existing path/SHA reference type, plus
`child_environment`. Both evidence bodies enter the same acquired reference inventory. The
admission owner, `tools/quality_provenance.py` in a source checkout, checks their selected producer,
source/configuration/compile/link/dependency and interpreter/encoder correspondence before J,
journal ownership, directory preparation or launch. Only the reviewed ordinary uninstrumented
native configuration is initially admitted; evidence for an instrumentation route must cover
its additional logging/symbolizer/child behavior before that route can be admitted.
The current header-evidence reader consumes the existing compiler `-H` trace. Actual MSVC
header-capture evidence remains an unmet evaluation prerequisite; a hand-written header index
cannot replace it. POSIX import admission retains the existing trusted operating-system-library
policy and does not require fictitious standalone files for libraries in the Darwin shared cache.

The child launch contract is explicit: POSIX uses exactly an empty environment; Windows uses
exactly the one frozen, validated `SystemRoot` value. Every child receives that same mapping.
There is no ambient inheritance, PATH search, shell, later environment refresh or optional
environment profile. The executable is absolute and processing keeps the frozen corpus working
directory and separate pipes. This is the selected ordinary CLI's contract, not a claim that
arbitrary programs require no environment.

Interpreter evidence identifies actual module/core images and loaded JSON code, binary64 traits,
canonical/read owners and options, reviewed imports/startup and the CPython source correspondence
for the conditional serialization allowance. The evaluator requires bytecode suppression from
interpreter startup (`python -B` or its startup environment equivalent); it never sets that flag
late to qualify a run. Admission checks the live canonical module/options bindings and omitted
encoder/indent defaults before comparing runtime metadata; a code-object hash alone cannot bind
that route. This is evaluation-specific evidence, not a global Python pin. Missing,
stale or unsupported capture premises refuse before journal/directory/child effects. Once admitted,
later capture failures still retain all roles, prior harms and observed effects. Independent review
must establish what the identified bytes mean: hashes cannot authenticate a build or establish
source-to-binary correspondence alone. Direct-child return and pipe EOF never certify descendants.

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

For continuous candidates, produce a genuinely unenhanced preserve/gray comparator with identical
output mode/depth, source bytes, orientation, quarter-turn and alpha/profile policies. For binary
candidates, freeze a genuinely unenhanced **gray16** comparator under those same source and common
geometry/alpha/profile policies. Every comparator photometric selection and completed stage, and
its binarization selection/stage, must be absent. Zero-strength selected methods do not qualify.
The gray16 codes represent unenhanced source Q16 at the common terminal boundary; they are not
raw stored source gray, enhanced preclassification luminance or exact hidden binary64 values.
Binary output is physical PNG1 and may use the common photometric stages and protection policy.
Produce a second candidate run with exactly the frozen settings. Store complete responses,
records, source/build provenance, output identities and canonical protection assets with each run.
A changed comparator command requires a newly frozen protocol digest and independent receipt
before outputs/reviews; never rewrite old commands, receipts, reports or human judgments.

`tools/evaluate_document_quality.py` requires a complete manifest, hashes actual source and
executable bytes, invokes real read-only bundle verification, requires distinct bundle/run identities
and compares every semantic record section: source, request, execution, assets, output and build.
It acquires run.json once and binds its encoded length and SHA256 to actual verifier observations.
Each result and canonical mask is also acquired once within its verified encoded extent, checked
against that verified identity, and those same immutable bytes enter independent integer decoding.
It checks geometry and continuous protected final integers against the unenhanced comparator, and actually replays frozen candidate, comparator and replicate settings
with the selected executable into newly owned retained replay directories. All six semantic sections must match
the scored bundles; artifact agreement cannot invent an unenhanced comparator. The build commit
binding still needs independently reviewed build/CI provenance: a declared SHA or executable hash
alone cannot prove which source built it.

Exact continuous protected final-integer equality is an observable fidelity criterion, not proof of
hidden binary64 luminance. For a binary candidate with an independently decoded **nonzero**
canonical protection region, the existing exact protected-final-integer comparison against the
unenhanced gray16 comparator is unavailable;
report explicit fidelity `missing_evidence` and abstain after all available identity, role, geometry,
policy and mask checks. This is neither observed harm nor a product refusal. An all-zero mask
makes only this preservation obligation vacuous, after its actual bound PNG8 grayscale0/1
content, B-frame shape and protected count are checked. Never trust the declared zero alone.
Candidate/baseline protection must agree; their whole asset arrays may differ for a candidate PSF.
All prior/current human harms, uncertain judgments, physical-page/class denominators and other
evidence/review requirements remain active when automatic fidelity is unavailable.

## One append-only factual run

```sh
python -B tools/evaluate_document_quality.py --manifest /cleared/corpus/evaluation.json \
  --executable out/release/bin/docenhance --out /cleared/corpus/quality-run.json
```

Use the source checkout and pinned developer environment. This is an offline engineering harness,
not a product runtime component or an acquisition/OCR/neural scoring/method-selection feature.
Source authoring does not authorize evaluation: the selected producer/dependency/write/no-child,
reader lifetime, serializer/J, resource and required native/platform proof must be independently
closed before an executable grant. Missing evidence, D02 RED and missing human reviews remain
active failures or abstention; a source contract is not a pass.

The report path must be fresh. One exclusive binary handle owns one JSON root, opened once:

```text
{"kind":"quality_run","protocol_version":3,"evidence_contract":"docenhance-quality-v3",
 "protocol_sha256":H,"intended":I,"events":[...],"report":R,"terminal":T}
```

The physical representation has the specified member order and one final newline. `I` retains the
complete original plan and declarations, ordered `{reference,value}` snapshots and the fixed work
index. Unavailable parsed reference values are null with fixed causes elsewhere; no favorable
replacement is invented. For N intended held-out pages there are3N original roles, in assessment
and candidate/baseline/replicate order. Each role has scored verification, replay processing and
replay verification:9N planned slots. Each original has one `original_observed` event; every slot,
including skipped work, has one `invocation_prepared` and one `invocation_finished`:21N events in a
finished factual container. No page or denominator is dropped when launch admission closes.

Fresh invocation names and exact relative paths are planned **in memory** before resource
admission. They do not claim directories exist. J and per-volume admission precede exclusive
report ownership and the flushed intended prefix, which precedes invocation preparation/effects.
Directories and raw files are created exclusively just in time under the already attached attempt.
A collision preserves the foreign entry and closes future launch admission; no new name or retry
is selected. Replay argv changes only the actual frozen `--out-dir` value. Verifier argv is derived
from its bound scored/replay target, not supplied freely. Unavailable derivation is null with a
cause; a known planned target does not imply an existing output.

Each canonical event is checked against its charged allowance before append, then flushed before
continuing. No seek, truncation, reopening, overwrite, concatenated documents, second report file
or delivery retry is used. Final role snapshots equal the FINISHED observations. PREPARED is
historical intent: allowed transitions retain stable identities and causes, rather than demanding
it equal the final state. An unfinished prepared prefix may have launched and has unknown effects;
it is never proof of `not_started`. A complete container needs actual count/index/dependency,
derived-argv, reference, snapshot and adjudication checks after its static closed schema.

`T.state=finished` means all roles/slots were accounted, including skipped work, not acceptance,
durable storage or complete capture. `R.report_sha256` hashes only canonical R before inserting
that identity. Final flush/close failure can leave apparently complete bytes; they alone cannot
prove the former close succeeded or promise power-loss durability. Preserve the file and return
failed delivery. A prefix, missing terminal, extra suffix or old bare report cannot admit as a
completed v3 run.

## Bounded raw evidence and actual effects

L=1,048,576 bytes is the operational per-stream ceiling for **new** processing and verifier stdout
and stderr. It is not a product input limit or proof every diagnostic fits. Streams remain separate.
Exactly L is complete only with actual EOF. Overflow retains the actual prefix (at most L), stops
the owned producer, closes all future launch/preparation admission and yields missing evidence.
There is no merged stream, head/tail omission, prefix-as-complete, invented total or retry. A read
may observe up to L+65,536 bytes; record that actual prefix count rather than assuming L+1.
Protocol3 deliberately replaces the impossible unconditional retention of all future failure bytes;
historical oversized files remain untouched, byte-identified and never truncated or evicted.

Original raw and referenced JSON are acquired through one regular-file handle with supported
initial extent B, at most B+1 bytes and transfers of at most64KiB. Length/hash/extent observations
and parsing use those SAME acquired bytes. Unstable, nonregular or unavailable inputs refuse or
become unavailable. Only complete original stdout of at most L is interpreted. Parsed metadata
uses an actual finite acquisition/RAM owner based on its observed extent, without an invented new
metadata cap. Acquisitions close sequentially; the run does not retain9N original raw descriptors.
These are observations, not permanent path immutability against arbitrary external mutation.

Closed observations distinguish EOF, persistence, exact/at-least/unavailable input count, retained
file extent/hash and actual close state. A retained digest identifies FILE bytes, not omitted input.
If a failure freezes a counter, later EOF cannot make it exact unless the complete bounded in-memory
stdout candidate actually establishes its full length. Preserve that candidate independently of
file writes so coherent complete stdout can confirm publication even after stderr/hash/close or
report failure. Original stdout must additionally be identity-bound; an unattributed parsed prefix
cannot establish delivered effects. Acceptance still requires complete separate raw evidence.

Each original role retains its declared evidence separately. New attempts retain actual requested
and derived argv, owned artifact directory only after creation, launch state, positive native PID
or null/unavailable cause, actual exit, timeout and direct/descendant cleanup observations. Actual
subprocess returns remain -2147483648..4294967295, including signals and Windows statuses. A
verifier has publication:null and cannot erase the originating process commit. Exits5/7, abnormal
termination, timeout and incomplete delivery cannot establish absence. Coherent completed
publication survives every later independent failure; otherwise ambiguous effects remain unknown.

Each attempt has31 fixed cause slots and one nullable reference to its first main-owner cause;
stdout-before-stderr breaks simultaneous pending-notice ties, not claimed OS chronology. Later
independent finalization causes keep their own slots. Originals/pages/roles and the run use their
specified finite validation slots. Causes are closed `{code,errno,winerror,subject}` values, with
actual supported native metadata or explicit unavailability. Arbitrary exception text/chains are
not appended. This explicitly loses verbatim *derived tool-exception prose*; original admitted
review/declaration text and complete retained product messages remain unchanged.

Only main enters the existing owned process/signal scope with two separate pipes. Capture failures
notify it promptly. Stop/reap the producer before joining actually started readers, and finalize a
destination only after its writer stops. Reader2-start failure never joins an absent reader.
Unconfirmed joins retain real strong reader/pipe/file owners; no detached success or close/hash
under a live writer is permitted. Direct-child reaping does not prove descendant cleanup, especially
on Windows. Child wait600/120s and bounded polling/join conventions are not portable hard total
operation deadlines for blocking OS I/O. The selected no-child/pipe-handle proof remains required.

## Logical write allowance and failure accounting

One actual projection/finite-domain owner computes J for the complete intended work before effects:
framing, I, original events, prepared/finished events, all duplicated R observations/current and
prior reviews, fixed causes, class members, terminal, commas and newline. Known values use actual
canonical byte lengths; finite branches use their longest encoded alternative. Dynamic complete
responses use the conditional32L serialization envelope, with the selected pinned finite-float/
serializer source and runtime property separately required. J is a conservative upper allowance,
not a fabricated maximum-success report, minimum file size or a second manually drifting schema.
Every actual frame is checked before append. A mismatch closes admission and cannot trigger extra
work, omitted fields, a recomputed allowance after effects or a raised ceiling.

All potentially eligible9N invocation slots are charged. Let K count actual attempted/may-have-started
replay ownership entries and V verifier entries; skipped work is accounted but not launched.
K<=3N, V<=3N+K and new raw<=2L(K+V)<=18NL are conditional arithmetic. Each process payload allowance
is256MiB result +256KiB run.json, plus256MiB iff a mask is selected and1MiB iff a raw PSF is selected.
Use EACH role's actual selections. The coarse1,633,419,264-byte/page sum before J is not an attainable
baseline-with-PSF claim or physical peak. Encoding is RAM to prepared spans to ONE staging directory;
rename moves that SAME directory. Charge retained failure stages and completed bundles while later
work runs; neither rename nor abstention refunds disk payload accounting.

Per-volume ledgers cover owned logical raw/payload/control/preparation writes at actual target
volumes and separately inventory existing retained evidence without eviction. Available-space
checks are **non-reserving** observations. They are not quotas or guarantees against competing
writers, allocation-unit/COW/filesystem overhead, malicious external code or power loss. There is
no reserve file, disk image, global quota, cache cleanup or new infrastructure. Real ENOSPC can
leave partial raw/journal/stages and unknown or confirmed-completed publication. It never earns
acceptance, successful delivery or safe retry.

Retention, overflow, startup, preparation, join uncertainty, serialization and report-admission
failures monotonically stop ALL later launch types, including verifiers and directory preparation.
Continue safe in-process accounting of every intended role, current/prior harms and class denominator.
Known product refusal remains distinct from failed execution, missing evidence, observed mark harm
and human uncertainty. Failed/refused/ambiguous roles are not automatically replayed. Complete
engineering roles still require actual source/build/record/output identity, distinct executions,
real verification/repetition, comparator and integer fidelity, independently of human completeness.

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

No human panel has been furnished for this redesign. D02 RED and all retained adverse evidence remain active; neither a v3 format change nor synthetic controls resolve them. The [public technical corpus](corpus.md) can
support processing, deterministic repetition and preservation checks; it does not constitute a
blinded readability evaluation or representative coverage. Missing class coverage or human reviews
blocks measured natural-document/no-harm release claims. Correctness CI can pass while that
product-quality evidence remains incomplete. Reports must identify this distinction explicitly.

