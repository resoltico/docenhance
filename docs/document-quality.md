# Independent document-quality evaluation

This protocol asks whether a specific explicit method/composition preserves document marks and
improves readability on its intended damaged classes against an unenhanced representation.
Engineering CI, deterministic mathematical references and `verify` remain separate evidence.
No general natural-document improvement, meaning or authenticity is claimed without completed
independent held-out review. A successful processing command alone cannot establish no harm.

## Governed corpus and frozen experiment

Keep natural images in a separately governed asset directory, outside the public source tree.
`spec/document-quality.schema.json` defines the closed evaluation manifest. Preserve each input's
original bytes, source URL, institution/creator, rights basis, evidence and named curator. Permission
fields are attestations requiring independent rights review, not a legal warranty inferred from a
hash. Do not include private user documents, credentials or uncleared third-party material.

An institutional Flickr Commons item may be a candidate when its individual rights label and
institution policy support the intended evaluation use. Retain both pages with retrieval date and
digests. Exclude all-rights-reserved or contradictory items. “No known copyright restrictions” is
an institutional assessment, not a universal public-domain guarantee; see [Flickr's explanation](https://www.flickr.org/programs/flickr-commons/no-known-copyright-restrictions-how-it-works/).
Attribution, catalogue references and any purpose restrictions stay with the asset. Never stamp the
project's MPL license onto corpus images or upstream material.

Freeze the corpus manifest before tuning. `frozen_corpus_sha256` is the SHA-256 of the UTF-8 compact
JSON corpus list with sorted keys, as computed by `document_quality.frozen_digest`. Include separate
held-out and tuning inputs; identical source digests cannot cross splits under different filenames. `document_id` groups
re-encodings/crops of one physical page: they cannot cross splits or count as independent held-out
pages. Curators must preserve that original-page identity.
Freeze the method/composition, all candidate and baseline CLI options, build commit, executable
digest, comparator and review protocol before opening held-out outputs. Record that preregistration independently; the
manifest cannot prove when a reviewer saw an image or whether declared independence is true.

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
executable bytes, invokes real read-only bundle verification, requires distinct bundle/run identities and compares repeated requests,
observations, build facts and output identities, independently decodes integer PNG samples, checks geometry and
protected destinations against the unenhanced comparator, and actually replays frozen candidate, comparator and replicate settings
with the selected executable into private temporary results. Request, execution, output and build
facts must match all scored bundles; artifact agreement cannot invent an unenhanced comparator. The build commit binding still needs independently reviewed build/CI
provenance: a declared SHA or executable hash alone cannot prove which source built it.

```sh
python tools/evaluate_document_quality.py --manifest /cleared/corpus/evaluation.json \
  --executable out/release/bin/docenhance --out /cleared/corpus/evaluation-report.json
```

Use the source checkout and pinned developer environment; this harness is not a product runtime
component and performs no acquisition, OCR, neural scoring or automatic method selection.
Its report retains actual settings, source/build/output facts and each verified record digest.
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
all prior judgments, and issue a separately identified report. Do not overwrite dissent or resolve
it by majority vote. For intended damaged classes, require positive unanimous paired judgments and
an exact one-sided paired sign tail no greater than 0.05 divided by the number of declared classes.
Ties count as nonpositive. Already-good and preservation controls require complete, certain no-harm
review; improvement is not required. This conservative family error bound is a protocol choice,
not a guarantee of population representativeness. Class overlap is not extra independent evidence.

A missing class, inadequate evidence, tuning leakage, missing permission, missing reviewer or any
material regression blocks acceptance. `abstain` is an evaluation result: it does not introduce
new automatic runtime behavior or manufacture a successful no-op. Parameter/model changes require
fresh execution/review against the same locked holdout, with the prior results retained.

## Current evidence boundary

No human panel has been furnished for this redesign. Historical Flickr items can support actual
technical processing, deterministic repetition, resource and preservation pilots, but these pilots
are not blinded readability evaluations or a representative corpus. Missing class coverage or human
reviews blocks measured natural-document/no-harm release claims. Correctness CI can pass while that
product-quality evidence remains incomplete. Reports must identify this distinction explicitly.

