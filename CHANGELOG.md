# Changelog

Notable changes to this project are documented in this file. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- Future GitHub source releases publish the exact categorized Markdown from their
  tagged changelog section; release prose has no second authoring source.
- Replaced lifecycle-branded command response, package, test-suite and tooling names with durable
  interfaces; lifecycle metadata is removed from command responses.
- Moved command input and output-target requirements from the CLI adapter into the application
  layer, where capability and invocation policy are decided.
- Centralized stable semantic-version validation on the CMake project version used by builds,
  packages, source archives, metadata, tags, and release notes.
- Enforced the terminology policy in structural checks; the upstream OpenCV `WITH_AVFOUNDATION`
  feature spelling is the sole exemption.

### Documentation

- Added an authority map, removed the hand-maintained dependency catalog, and updated publishing
  guidance to the protected repository's current operating policy.

## [0.1.0] - 2026-09-21

- First release.
