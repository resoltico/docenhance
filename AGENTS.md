# Coding-agent instructions

Apply these rules within the authorized task and governing instructions. User instructions take
precedence over this file. Flag unresolved conflicts; preserve unrelated work.

## Understand and design

Read `docs/status.md`, `docs/architecture.md`, `docs/decisions.md` and relevant reviewed `spec/`
contracts before editing. The roadmap is planning information, not executable admission. For
substantial changes, design first, then challenge the design in a distinct skeptical pass with
counterexamples, implement the simplest single solution, and verify healthy and negative cases.

Use YAGNI, DRY and KISS. Name by responsibility and behavior, not task provenance or supposed
superiority. Prefer removing unnecessary work and using existing owners. Add a concept, dependency,
check or document only for a concrete requirement or demonstrated risk. Preserve the property a
check protects; file counts and test counts are not quality evidence. Measure total change coupling,
public surface, ownership and reviewability before calling a refactor simpler.

Major changes use hard, clean contract breaks. Do not add compatibility readers, migrations,
deprecated aliases or transitional layers. Remove superseded APIs and in-scope scaffolding. Do not
invent future method implementations, successful copies, plugin registries or generic recipes.

## Authorities and boundaries

- `spec/architecture.json` owns the layer graph, direct links, external packages and exception/API
  permissions. Update it only for a genuine design change. Targets link exactly what their files use.
- `spec/cli-contract.json` owns syntax and format admission; `spec/method-contract.json` owns
  implemented method parameters and selector domains. Runtime catalogs come from actual types;
  common geometry belongs to image/request contracts. See `docs/methods.md` for generated metadata.
- `spec/command-response.schema.json` and `spec/run-record.schema.json` own current wire templates.
  Regenerate affected outputs with `python tools/generate_spec.py`; reject obsolete forms.
- `CMakeLists.txt` owns version. `deps/lock.json` owns immutable sources; `deps/features.json` owns
  native features/patches; `deps/tools.json` owns tool versions and deployment floors.
- The CLI owns syntax and one response delivery; the app admits `BinaryPlan | ContinuousPlan`;
  the host executes the fixed stages. Typed observations retain actual work and unstarted stages.
  Numeric kernels have no I/O, process state, allocation expressions, throwing or catching.

Read method guides before touching their behavior: `docs/binarization.md`, `docs/illumination.md`,
`docs/morphological-illumination.md`, `docs/denoising.md`, `docs/tvl1-denoising.md`, `docs/restoration.md`,
`docs/contrast.md`, `docs/sharpening.md`, and `docs/geometry.md`. Codec representation/admission belongs
in `docs/png-processing.md`, `docs/jpeg-processing.md` and `docs/tiff-processing.md`. Preserve their
mathematics, borders, precision, masks, immutable fitted models, resource estimates and references.
Do not advertise any method or domain before complete typed admission, execution and tests exist.

## Effects, ownership and safety

Read `docs/cancellation.md` and `docs/bundles.md` before changing interrupts, scheduling, codecs,
verification or publication. Cancellation is execution control, never method configuration. Keep
bounded checkpoints, real-error precedence, joined workers and the final precommit cutoff. Never
call `request_stop` from an OS handler. Test cancellation deterministically without timing sleeps.

Preserve borrow lifetimes, move readiness, payload/refund accounting, finite native reservations
and foreign-library exception containment. Charged high-water bytes are not a process-RSS limit.
Command text and admitted paths must be valid UTF-8; never normalize or repair identity bytes.

Publication states remain truthful: `not_started`, `not_published`, `completed`, `unknown`.
Escaping processing exceptions retain unknown publication through the preallocated fallback.
Never convert uncertainty to safe retry or lose a confirmed commit after later verification failure.
Exclusive publication promises atomic visibility, not power-loss durability. Rendering/delivery
happen once after execution; explicitly flush the selected stream and never retry failed delivery.
An incomplete response or exit 5/7 is not evidence of no effect. `verify` is read-only artifact/record
agreement, not authenticity or proof of original document meaning.

Keep CPU/local operation and locked native isolation. No runtime Python, network, neural, OCR, GPU
or GUI components. Never silently substitute ambient packages or moving references. Preserve
upstream attribution/licenses and reviewed native feature/security closure.

## Verification and completion

Run `python tools/check_all.py`, `python -m unittest discover -s tests/tooling -v`, relevant
independent reference tests and locally available real CMake workflows. Keep both strict AFL++ and
libFuzzer, every retained regression byte, ASan/UBSan, TSan, compiler-backed architecture checks,
source-security checks and full required native/minimum-OS coverage. Fuzz parser/CLI changes with
`cmake --workflow --preset fuzz` and retain reproducers in `fuzz/regressions/`. Suppressions name exact
rules with code-bound reasons in `tests/exceptions/registry.json`; size limits have no waivers.
Fix findings instead of weakening guards, relaxing evidence, hiding skips or claiming unrun checks.

Keep running checks' inputs stable or isolated. Inspect changes and artifacts after meaningful
operations. Follow changes through callers, generated files, packaging, test discovery, CI and docs.
Use protected reviewed PRs; merge only after fully green latest-head required checks, then verify
actual main, postmerge CI and source/package evidence. The weekly deep checks provide ongoing
coverage and are not a source-release prerequisite. Report exact commits,
commands, platforms, outcomes and concrete external blockers; authored CI is not execution evidence.

`docs/document-quality.md` and `spec/document-quality.schema.json` govern independent held-out
quality evaluation. Clear provenance/rights and human reviews are required. Never infer natural
readability from synthetic formula tests or invent favorable results. Preserve sources and report
per-class harms, uncertainty and abstention separately from engineering correctness.

Documentation describes current truth. Keep one authority per fact; historical matter belongs
only in CHANGELOG.md. Changelog entries describe net release outcomes against the published baseline,
including breaking action and non-guarantees. Retain Unreleased, dated history, tags and published
artifacts. Deliver the whole authorized change with final independent QA and factual evidence.
