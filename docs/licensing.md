# Licensing and distribution

Current first-party DocEnhance work is licensed under **MPL-2.0**, beginning with the upcoming
v0.6.0. [LICENSE](../LICENSE) contains Mozilla's unmodified license text. Source files carry the
`SPDX-License-Identifier: MPL-2.0` notice; this policy also covers project-owned documentation,
contracts and synthetic fixtures unless an applicable notice states otherwise. Contributions are
accepted under MPL-2.0; contributors retain their copyright.

Earlier MIT releases and public Git commits retain the MIT permissions already granted. Changing
this checkout does not revoke those grants, rewrite history, or publish v0.6.0. Use the license
and notices accompanying the particular source snapshot you received.

## Upstream material

Dependencies retain their own licenses and notices. The lock and [dependency policy](dependencies.md)
identify the selected sources; [third-party notices](../THIRD_PARTY_NOTICES.md) distinguish their
terms from the project license. Upstream source notices, quoted notices and upstream-generated
profile metadata are not relabeled MPL. Private dependency adaptations retain the upstream notices;
the project-owned additions and their preferred source form remain available in the build recipes.
Neither this document nor the declared-source SPDX inventory establishes legal or patent clearance.

## Distributor obligations

Read [MPL sections 3.1–3.4](https://www.mozilla.org/en-US/MPL/2.0/), including the notice-preservation
requirements. Its copyleft applies to covered files, including modifications; separately licensed
files in a larger work retain their own terms.

When distributing an executable, make the corresponding MPL-covered source available under MPL
and tell recipients how to obtain it. Supply the preferred form for modification for the actual
code distributed, including your changes, relevant generated covered source and dependency
adaptations containing project code. A link to a moving branch, a source inventory, or an unrelated
release archive does not identify that source. Preserve upstream obligations separately.

The repository currently publishes source releases. Native packages are validation artifacts,
not publicly released binaries. Their included license and this guide explain the obligations;
before publishing a binary, provide a working source locator for its exact source and modifications
and verify the recipient can obtain them. The source-release workflow is described in
[publishing](publishing.md). This license change does not add a binary publication service.
