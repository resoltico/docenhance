# Changelog

Notable changes to this project are documented in this file. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
