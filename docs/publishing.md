# Publishing the repository

This repository is published on GitHub. Review changes locally, commit with your own Git identity,
and use protected `main` through reviewed pull requests.

Community-health files live in `.github/`: [security policy](../.github/SECURITY.md), [code of
conduct](../.github/CODE_OF_CONDUCT.md), the pull-request template and the issue forms. GitHub reads
them from there exactly as it would from the root.

## GitHub automation

Two workflow families are checked in:

- **Quality gates** and **Nightly deep checks** acquire locked dependencies and run native builds,
  tests, sanitizers and fuzzing. They create packages only for in-run smoke tests; they upload no
  development binaries and create no GitHub releases.
- **Package source archive** runs source-level integrity, formatting, lint/type and tooling checks,
  then creates and attests the deterministic source archive. On a `v*` tag it publishes the
  source-only GitHub release using the exact Markdown below that version's heading in
  `CHANGELOG.md`; there is no separate release-notes source.

## Repository policy

`main` is protected by the **Required quality gate** aggregate check. It fails if any structural,
native, sanitizer, or fuzzing job fails, is skipped, or is cancelled. Linear history is required;
force pushes and branch deletion are prohibited. Private vulnerability reporting is enabled, and
the security policy names it as the reporting channel.

The **Nightly deep checks** workflow runs both fuzz engines and the sanitizer suite outside pull
requests. `.github/CODEOWNERS` must name the current maintainer before any required-owner review
rule is enabled; do not enable a rule that cannot be satisfied.

Actions are pinned to reviewed commit identifiers, and Dependabot proposes Action updates only;
dependency-lock changes still require the review described in [CONTRIBUTING](../CONTRIBUTING.md).

## Source releases

Publishing source is separate from shipping a binary. Before creating a reviewed, annotated `v*`
tag, move the complete release prose from `Unreleased` under the matching dated changelog heading.
The source workflow validates the tag/version match, creates the release from the tagged archive
and checksum, and rejects any existing release whose body or assets differ; it never edits a release
or replaces an asset. Do not attach a binary, claim that native CI passed before it has, or mark the
release as a usable image enhancer. Release signing, binary distribution, credentials and approvals
belong to a later distribution milestone.

Verify the downloaded archive against both its checksum and the GitHub provenance before trusting
or attaching it:

```sh
shasum -a 256 -c docenhance-<project-version>-source.tar.gz.sha256
gh attestation verify docenhance-<project-version>-source.tar.gz -R OWNER/docenhance
```
