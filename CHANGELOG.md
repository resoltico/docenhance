# Changelog

Notable changes to this project are documented in this file. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- **Breaking:** Require well-formed UTF-8 command text and paths without normalization or byte
  repair. Keep machine-readable identities strict; malformed diagnostics use an explicit fallback.
- Separate validated application requests from the concrete processing host; inject the execution
  port explicitly and keep CLI fuzzing entirely free of filesystem authority.
- Unify production/fuzz target definitions and make full ASan/UBSan and TSan suites independent
  required PR checks. Scheduled campaigns discover all harnesses through CTest.
- Close the machine-response schema and validate it with pinned Draft 2020-12 tooling; remove
  stale capability claims and the terminology ban that obstructed factual design documentation.

### Fixed

- Preserve unknown publication when a processor throws after an unreported effect; reserve the
  fallback outcome before execution instead of misreporting a safe retry or allocating in the catch.
- Deliver exact serialized bytes regardless of caller width/fill settings, then flush and check
  the selected response stream; delayed delivery failures cannot report success,
  and an unused stream cannot invalidate a response or cause processing to repeat.
- Preserve literal backslashes in POSIX output directory names when reporting the published file.
- Align agent and design instructions with implemented capabilities and exception ownership.
- Preserve buffer accounting after its budget owner is destroyed; validate borrowed view extents,
  reset moved-from plane shapes, and reject empty/overlapping kernel destinations.
- Contain worker and thread-launch exceptions; bound scheduling counters without integer wrap.
- Charge PNG codec allocations to the page budget, preserve stored grayscale samples and handle
  malformed data with destructors outside libpng jump frames.
- Publish with native atomic no-replace semantics, preserve concurrent destinations/foreign stages,
  and expose uncertain publication or cleanup instead of incorrectly promising a safe retry.
- Preserve Unicode filesystem paths and normalize Windows command-line arguments to UTF-8.
- Make corpus byte conversions explicit, prevent duplicate or unowned architecture declarations,
  and check direct allocator calls as well as allocation expressions.
- Bind lint exception fingerprints to the actual suppressed code and reject range-wide NOLINT
  blocks; replace the native probe's broad allocator suppression with individual C ABI exceptions.
- Disable unintended C++ module scanning for the header/translation-unit build model without
  weakening compiler warnings or clang-tidy.

## [0.2.0] - 2026-09-22

### Added

- `process` now performs B03 fixed-threshold binarization for grayscale PNG input without alpha (1, 2, 4, or 8 bits), producing an 8-bit grayscale `result.png` in a newly published output directory.
- `methods` reports B03 and `version --json` reports PNG as supported capabilities.

### Changed

- **Breaking:** The command-line contract is reduced to `process`, `methods`, and `version`; `plan`, `inspect`, `presets`, and unsupported processing options are no longer accepted. Integrators must invoke the explicit B03/PNG operation described above.
- **Breaking:** JSON responses no longer contain lifecycle metadata. Consumers must use [command-response.schema.json](schemas/command-response.schema.json) and stop reading the removed fields.
- Source releases now derive their GitHub release notes from the matching dated changelog section and verify the published source archive and checksum against the tagged revision.

### Internal

- The shared box-mean primitive now rejects overlapping storage and initializes large reflected windows by period, preserving deterministic output while avoiding radius-proportional setup work.
- The quality workflow includes Linux x86-64/ARM64, macOS ARM64/Intel, and Windows x86-64; source releases retain source-only provenance rather than publishing binaries.

## [0.1.0] - 2026-09-21

- First release.
