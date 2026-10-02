# Verification strength and delivered artifacts

## Design pass

Verification must reconcile independent observations with the artifact or execution it claims to
check. Preserve independent numerical references, real codec/CLI/publication tests, deterministic
cancellation controls and the manifest-driven fuzz campaigns. Do not replace those checks with
metadata, an advertised capability or a test count copied into another inventory.

Remediate the owning boundaries:

- Native CTest execution must compare discovered unit cases with the actual Catch executable,
  require every CLI test script and manifest replay, reject disabled/skippable registrations,
  require nonzero Catch assertion work without hidden or expected-failure cases,
  and reconcile a fresh JUnit result with every discovered test. Check the actual native
  compilation database against source files; a filename in a CMake comment is not compilation.
- Tooling discovery must load every test module, perform nonzero work and fail on skips or
  unexpected successes. Keep standard unittest discovery available; use strict completion in
  the required gates and CTest owner.
- Package inspection must compare the unique shipped specs/schemas/license inventory with the
  current reviewed/generated sources and verified locked upstream bytes, validate every JSON
  response, reconcile executable build/capability facts and inspect actual native imports.
  A declared source SBOM stays a source inventory, not a binary-composition certificate.
- License inventory regeneration must remove obsolete managed outputs after successful
  preparation, so retired or removed notices cannot persist through a reused build directory.
- Source archives must select the committed Git tree and snapshot each blob once for both its hash
  and archive payload. Repackaging arbitrary Gitless directories is not a release-source contract.
  Arbitrary untracked workspace files must not become release contents.

Use actual registrations and executable discovery rather than hand-maintained suite counts.
Keep exclusions tied to configured responsibilities, and retain per-run diagnostics. No runtime
Python dependency, new method, image format, compatibility reader or migration is needed.

## Separate design QA

Before implementation, challenge the boundaries with real negative controls:

- The existing package smoke test accepts a relocated package whose schemas are replaced with
  `{}`, whose SPDX inventory is `{}`, and whose upstream license directories are deleted. The
  native processing check still passes. Presence and permissive validation cannot establish
  delivery of the reviewed contract or notices; compare exact independently generated contents.
- A real CMake project with a disabled failing test and one passing test returns CTest exit zero
  and "100% tests passed". Its JUnit explicitly reports the disabled test. Reject registration
  escapes before execution and reconcile result identities/statuses afterwards.
- Derive Catch cases from its JSON listing, including hidden tags, and compare exact commands,
  not only names. Inspect compilation entries to challenge comment-only source registration.
  Do not use one generated manifest to prove another identical generated manifest.
- Unit-test the result reconcilers with missing, duplicate, skipped, disabled and failed cases;
  execute real CTest skip/disable controls and real unittest skipped/empty-module controls.
- A package may be internally self-consistent but stale: compare current version, lock, complete
  capability matrix and source bytes. Check metadata before launching processing. Exercise each
  advertised method after relocation and retain decoded-sample/reference tests as separate proof.
- Dynamic OS runtime imports are legitimate; workspace/dependency shared-library imports are
  not the declared static package. Platform checks must inspect the actual executable and fail
  when their native inspection tool is absent. Deployment load commands establish a declared
  minimum, not execution on every older OS.
- Source manifest paths must be unique, confined and regular. Snapshotting once prevents a
  manifest from identifying bytes different from its tar payload. Hashes do not establish review
  quality, authenticity or reproducible native binaries.

These challenges retain the design. Sanitizer/coverage symbols establish presence in actual
archives, not instrumentation of every object or assembly instruction. Bounded fuzzing produces
counterexamples and completion evidence, never an exhaustive proof of safety or document fidelity.

Fuzz reconciliation also independently checks engine duration, selected engine and the actual
binary/manifest hashes, together with complete JUnit execution; a runner-provided passed flag
cannot replace those observations.

## Implementation challenges

The locked Catch CMake helper makes skipped cases successful by default. Enable its existing
`SKIP_IS_FAILURE` option instead of permitting a skip exception in the required suite. The first
full run then executed all 168 tests, but CTest truncated longer passing Catch XML records at its
default 1 KiB capture limit. Require a finite 1 MiB capture allowance and still fail when evidence
is truncated or malformed; do not infer missing assertion facts from process success. See
[CMake's output-capture contract](https://cmake.org/cmake/help/latest/variable/CTEST_CUSTOM_MAXIMUM_PASSED_TEST_OUTPUT_SIZE.html).

The clean Linux gate exposed an ambient-build tooling test that previously skipped its layer
registration assertion, plus an AST test that could skip when its tool was absent. Layer coverage
now belongs to the actual native owner, with isolated missing/misattributed-layer negative controls
in tooling; the AST prerequisite is mandatory and installed in source-release CI. Platform link
controls run rather than being silently excluded. Synthetic CTest projects explicitly use the
pinned Ninja generator, so the minimal developer image does not depend on an unadmitted Make tool.

The Apple package's closed inventory rejected 142 AppleDouble metadata entries generated from host
extended attributes. Neither COPYFILE_DISABLE nor clearing staging attributes removed the platform
provenance metadata. The revised producer uses CPack's existing External staging interface and one
shared byte-payload tar writer for source/native archives. It preserves intended executable modes
and regular file contents with explicit portable metadata, rather than rewriting source attributes
or relaxing inventory checks. Native archive bytes are stable for a given stage; reproducible
compilation is still not claimed. See [CPack External](https://cmake.org/cmake/help/latest/cpack_gen/external.html).

Windows inspection must distinguish physical DLLs from core API-set contracts. File existence
incorrectly rejected a supported synchronization import after the full Windows suite passed.
Resolve virtual core contracts with the System32-only OS loader, inspect their actual host path,
and release the inspection handle. Keep dynamic CRT and non-OS import refusal unchanged, with
negative controls for absent/foreign hosts. [Microsoft's API-set contract](https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets)
explains why a successful link is not evidence of a physical DLL file.
