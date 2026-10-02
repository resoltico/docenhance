# Coding-agent instructions

Apply these principles within the authorized task and governing instructions; resolve conflicts by instruction precedence and flag unresolved conflicts.

## Instructions

Use hard, clean contract breaks. Do not maintain backward compatibility, legacy readers or
migration paths. Update current contracts and tests together, and reject obsolete forms.

Read `docs/status.md`, `docs/architecture.md`, `docs/decisions.md` and the reviewed contracts under
`spec/`. The roadmap describes planned work; inspect current capability contracts before choosing
a package. Names describe responsibilities, not a project's age, maturity or delivery phase.

`spec/architecture.json` owns the layer graph and API permissions. Update it when a genuine design
change requires a new edge or exception boundary; do not duplicate rules in build scripts or docs.
A target links exactly the layers and packages its own files include. Numeric kernels remain free
of I/O, process state, allocation expressions, throwing and catching. The CLI owns syntax and
response delivery, never processing rules. `de_app` admits `ProcessRequest`; `de_host` executes it.
The application contains unreported processing exceptions as unknown publication, not safe retry.

Do not advertise a method or format until its complete contract and tests exist. B02 Sauvola and B03 fixed-threshold
grayscale-PNG processing, I01 continuous-tone illumination and D01 bounded 16-bit NLM-L1 denoising
are implemented; other method entries remain plans. Continuous-tone
PNG representation is separately implemented; read docs/png-processing.md. It is not a fictional
enhancement method. Keep binary sample meaning separate from color/profile interpretation. Never use no-op/copy
stubs to manufacture successful processing. `E_NOT_IMPLEMENTED` uses exit 4 and `not_started`;
`E_PUBLICATION_UNKNOWN` uses exit 7 and `unknown`. An incomplete response is not proof of no effect.

Command text and admitted paths must be well-formed UTF-8. Do not normalize paths, repair invalid
identity bytes or let diagnostic fallback change a filename. Rendering and delivery happen once,
after execution. Explicitly flush the selected stream and handle its failure without retrying.

Use locked sources and explicit feature configuration. Do not follow moving references or silently
substitute system packages. Do not add neural/OCR/GPU/GUI/network/runtime Python components. Do not
stamp MIT on upstream code or copy restricted research implementations.

Edit `spec/cli-contract.json`, `spec/method-contract.json` and the response schema template
`spec/command-response.schema.json`; regenerate with
`python tools/generate_spec.py`. Extend typed admission together with a complete processing method;
a JSON catalog is not executable validation, and unsupported options must not be silently ignored.

Run `python tools/check_project.py`, `python tools/check_gates.py`, `python tools/check_format.py`,
`python -m ruff check`, `python -m mypy`, `python -m unittest discover -s tests/tooling -v`, relevant
reference tests and the real CMake workflow. Fix findings, rather than weakening the gates. Every
suppression names its rule and has a code-bound reason in `tests/exceptions/registry.json`; size
limits have no waivers. Fuzz parser/CLI changes with `cmake --workflow --preset fuzz` and retain
reproducers in `fuzz/regressions/`. Report precisely which checks and platforms ran; authored CI,
partial local builds and absent dependencies do not establish a passing full workflow.

For numerical work implement the specified mathematics, borders, precision, resource estimates,
failures and reference fixtures before optimizing. A charged-buffer budget is not a process-RSS
limit. Preserve unknown state rather than converting it to success. An enhanced image is not a
certificate of the original document's meaning or authenticity.

For binarization, read `docs/binarization.md`. Preserve validated method alternatives, strict
option presence and normalized units. The runtime catalog comes from actual variant types and
must match reviewed generated metadata; a catalog flag alone does not implement a capability.

Cancellation is explicit execution control, never method configuration. Preserve the final precommit
cutoff, real-error precedence, worker joins and truthful publication states. Add bounded checkpoints
inside new long-running loops and test them without timing sleeps. Read `docs/cancellation.md` before
changing interrupts, scheduling, codecs or publication; never call `request_stop` from an OS handler.

For illumination read `docs/illumination.md`. Keep the operation opt-in and its mask in already-oriented
coordinates. Fit once on eligible linear samples, verify the true solver residual and reuse the same
immutable model during output verification. Do not change binary semantics, refit on verification,
double-count observations, downsample on resource refusal or call numerical failure an automatic skip.

## Name by meaning, not incidental development history

Name files, directories and identifiers for their responsibility, behavior or domain concept, using the project's idioms. Keep terminology consistent within a context and make necessary distinctions between contexts explicit.

Prefer names that convey meaning in context. Broad names such as `utils`, `manager` or `data` deserve scrutiny when they hide responsibility or collect unrelated concerns. Clarify responsibility before renaming; refactor only when warranted and within scope.

Do not name things merely for implementation generations, task provenance, development status or unsupported superiority. `new`, `legacy`, `v2`, `temp` and `improved` are examples to examine, not banned words. Distinguish alternatives by meaningful properties: `StreamingParser` and `BufferedParser`, not `Parser` and `ImprovedParser`.

States, stages, ordering, lifetimes and versions are valid when they describe the domain, algorithm, contract, compatibility boundary or artifact. `api/v2`, `LegacyEncoding`, `TemporaryDirectory` and `FinalInvoice` can be precise. A versioned public contract need not lose its versioned name when older versions retire.

Apply this to comments, documentation, logs, errors and test descriptions too. Retain history where it explains current constraints or belongs in a historical record.

Review names introduced or affected by the task, including your prose. Fix misleading names within scope and update references. Respect external conventions, persisted formats and established compatibility commitments; use an authorized migration or deprecation path where needed.

## Simplicity

Build the simplest design that fully solves the stated problem and is easy to understand and change. Minimize concepts, special cases and hidden state, not lines of code. Keep complexity required for correctness, security, error handling and stated requirements. Flag avoidable complexity in explicit requirements without silently overriding them.

**Understand first.** Read relevant code, contracts and tests; validate assumptions before changing them. Prefer root-cause fixes; identify necessary mitigations and their limits.

**Look wider than the matter at hand.** Examine related defects, duplicated mechanisms and downstream effects for shared causes. Consider fundamentally different approaches, including replacement, where simpler. Keep removals and refactors within scope; report related work outside it with a recommendation.

**Before adding anything** (code, file, abstraction, option, dependency, check, document):

- Name the concrete need: a requirement, defect, maintenance burden or real risk. Without one, do not add it. Tie verification and documentation to these needs.
- Do not build speculative options, extension points or compatibility layers. Establish compatibility commitments from instructions, supported interfaces, persisted data and consumers. Coordinate changes with consumers and rollout; break commitments only when authorized.
- Prefer removing unnecessary work, suitable platform features, existing project code, then a maintained dependency or new code according to total complexity. Do not force reuse or a dependency that fits poorly.
- Keep one authoritative source per fact; derive or check dependent copies. Avoid hand-maintained derived inventories, not necessary source data. Keep test oracles independent of the logic under test.
- Avoid needless duplicate mechanisms; share abstractions only when they reduce total complexity. Do not merge distinct contracts merely because their code looks similar.

**Before removing anything:**

- Understand its users and the property it protects. If unsure, investigate; absence of search hits or test coverage is not sufficient evidence for deletion.
- Preserve required safety, security and correctness properties when replacing checks. Remove unnecessary mechanisms rather than adding tests or documentation to justify them.
- Delete what the task demonstrably makes obsolete, including your own superseded scaffolding.

**Judge the whole, not the part.** Improving a metric alone does not establish simplification; neither does shifting complexity to callers or users. Count concepts, moving parts, state and indirection across the system.

**Before finishing:** remove needless changes, then verify intended behavior and protection against regressions. Explain necessary complexity.

## Design before building

Scale design and review to risk, not line count: a one-line change to authorization or data deletion can be high-risk. Routine low-risk edits need only a brief sanity check; use distinct design and challenge passes for non-trivial changes to behavior, contracts or mechanisms.

1. **Design.** Establish the problem, relevant constraints, alternatives and chosen approach. Consider a fundamentally different approach where it could be simpler, and effects on callers, data, tests, documentation and operations.
2. **Challenge it.** Review the design in a separate skeptical pass. Test assumptions against code, experiments or counterexamples; try to make it fail. Revise where the evidence disagrees, rather than restating the design.
3. **Build.** Implement the reviewed design. If implementation invalidates an assumption, revisit and challenge that part before proceeding.

## Finish the whole change

Follow the change through its dependents, not just the edited files.

- Find affected callers, configuration, lockfiles, generated files, build/CI rules, documentation, tests and external consumers. Update in-scope dependents; identify needed external coordination.
- Search relevant inputs, including hidden, ignored or binary files when needed. Check tool exclusions and stale references; retain valid history, compatibility and rejection-test references. No text matches does not prove completeness.
- Check that intended files reach deliverables and relevant files reach required checks, including test discovery. Inspect tracking, ignore and packaging rules; regenerate affected outputs per project policy.

Report implementation, verification and external rollout separately. Do not claim an unmet requirement is satisfied or widen scope without authorization.

## Checks must prove something

A check, test, gate or record must support a property someone relies on; match the strength of the claim to the evidence.

- Hashes can verify identity or integrity against a reference, not review quality. A completion claim does not establish execution; mocks alone do not demonstrate real-system integration. Use real-boundary verification where the property requires it.
- Test intended behavior, boundaries and relevant failures. Give guards a case they must reject. Use regression tests or negative controls to demonstrate detection, not just code execution.
- Keep controls proportionate. Do not build a check mainly to appease another check or inflate coverage; address the underlying risk. Independent checks of the same property can be justified.
- Fix real findings; correct invalid checks against established requirements. Do not lower required assurance without authorization. Exceptions must be explicit, narrow and justified.
- Investigate relevant failures, including pre-existing ones. Fix in-scope issues, continue independent safe work, and report unresolved failures or unavailable checks. Never report a failing or unrun gate as passing.

## Verification

An exit code establishes only what that command's success criteria mean. Check the intended result; local success does not establish success in the authoritative environment.

- After a meaningful state-changing operation or related batch, inspect its effects: changed files, commit contents, generated outputs or installed versions. Preserve failure visibility; do not let a later success mask an earlier failure.
- Check expected outputs and test discovery, including skipped tests and exclusions. Investigate unexpectedly empty, small or missing results before treating them as success; expected quiet output is not itself a failure.
- Run relevant workflows that are available locally before pushing a candidate for CI, including affected compiler, sanitizer and fuzz-engine modes. Check prerequisites early and use pinned, isolated environments. Fix local failures first; identify unavailable platforms or runtime conditions precisely. CI still supplies authoritative platform evidence and does not replace locally available verification.
- Diagnose from the full relevant failure output, retaining details without dumping sensitive or excessive content.
- Use targeted checks during iteration; run required verification where authorized and available. Prefer an isolated, clean environment matching the required platform, toolchain and restore mode without discarding user work.
- Verify the delivered state, not an earlier working tree. Report what ran, outcomes and untested platforms or conditions.

## Documentation and comments state the current truth

Describe supported behavior, assumptions and reasons. Keep history in change or decision records unless it explains a current constraint or migration; do not erase useful rationale. Prefer links, generated content or consistency checks over manually duplicated reference facts. The authoritative source may be code or configuration, not a document. Update affected documentation, examples and error text with the change, and check affected links and generated blocks proportionately. Do not create documentation machinery merely to satisfy this rule.

## Working hygiene

- Check inputs and prerequisites cheaply before long runs.
- Keep running checks' inputs stable or isolate them; intended watch/reload workflows are fine.
- Preserve unrelated work. Clean up your temporary resources, not deliverables or useful failure evidence. Leave requested services running.
- Keep output focused; do not dump large generated files or expose secrets.

## Easy to start

Document the shortest setup from a clean checkout, prerequisites and actionable errors. Reuse commands; do not mandate setup wrappers or doctor tools. Declare each toolchain version authoritatively; derive or check required copies. Pin inputs for reproducible builds; retain supported dependency ranges and deliberate compatibility probes.

## Changelog: record release outcomes, not development history

Record notable net changes for users, operators, integrators and contributors against the relevant published baseline. Lead with what changed and who is affected; state breaking changes and necessary action, including breaking fixes. Substantiate claims and retain material commands, public identifiers, limits and non-guarantees. Link detailed explanations rather than narrating implementation; do not invent benefits or present unfinished capabilities as delivered.

Consolidate related pending work into its final outcome. Omit routine churn and wholly reversed, unpublished work with no remaining consequence. Classify against released behaviour, not commit labels. Fold repairs to never-released functionality into the completed feature; retain meaningful consequences for public-prerelease users.

Use established categories consistently and omit empty ones. Internal is for noteworthy implementation, verification or release-process outcomes, not routine churn or concealed compatibility changes, new build requirements or user-visible effects. Keep technical method and protocol identifiers; omit development-phase labels.

Retain Unreleased and release history. Preserve version attribution, dates, links and publication-status distinctions. Published entries may be clarified, consolidated or reclassified without changing what they say shipped; factual corrections require evidence. Do not prune or relocate history without an explicit policy or request, or alter tags or published artifacts when editing the working file.
