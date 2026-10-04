# Security policy

## Supported versions

Published releases contain source archives, not signed prebuilt application binaries. Security
fixes target the current `main` branch; there are no security backports or support guarantees for
older release snapshots. See [current capabilities](../docs/status.md) for the implemented
PNG/JPEG processing, bundle verification and explicit unsupported domains.

## Reporting a vulnerability

Report privately through GitHub's private vulnerability reporting on this repository, not in a public issue or pull request. Private reporting is enabled at [Report a vulnerability](https://github.com/resoltico/docenhance/security/advisories/new); no email address is published, and no unverified address should be trusted as a contact for this project.

Please include the platform, compiler and build preset, the exact command, and a reproducing input file where one exists. A crashing input is the most useful thing you can send: fixes land together with the reproducer in `fuzz/regressions/`, so every build re-checks the defect afterwards.

Expect best-effort handling by a single maintainer. There is no response-time commitment, bounty, or CVE-assignment process.

## Scope

In scope today: memory-safety or undefined-behavior defects reachable from the command line; defects in the dependency-acquisition tooling, including incorrect verification of pinned sources or unsafe archive extraction; and any path that writes outside a directory the user named.

Also in scope: malformed-image allocations, codec `longjmp` crossing C++ destructors, integer overflow, decompression bombs, malicious colour profiles, symlink and reparse-point races, and ambiguous publication state. Their current contracts and limitations are linked from [status](../docs/status.md).

Out of scope: features that are simply unimplemented; resource exhaustion produced by limits the user sets deliberately; and dependency vulnerabilities not reachable through this project, which belong upstream, though telling us is welcome so the pins can be reviewed.

## What the project does today

Sanitizer builds abort on any AddressSanitizer or UndefinedBehaviorSanitizer report; manifest-declared fuzz harnesses run under libFuzzer and AFL++, and every build replays their corpora and recorded regressions; parsers are bounded and arithmetic is checked; dependencies are pinned to exact release objects, digest-inventoried and re-verified before every build. These measures reduce risk. They are not vulnerability scanning, signed-release verification, or protection against a compromised upstream maintainer.

The independent nightly job queries OSV for verified source identities and requires review of new
matches; [dependency policy](../docs/dependencies.md#advisory-observations) explains coverage and
reviewed build exclusions. Empty database results are not a vulnerability-free guarantee.

No network access, telemetry, OCR, neural model or GPU code runs at runtime. Dependency acquisition and CI are deliberately online and form a separate trust boundary. Release packages still require dependency, licence and composition review, platform testing and a documented signing policy.
