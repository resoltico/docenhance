# Third-party dependencies

DocEnhance is copyright 2026 Ervins Strauhmanis and MIT-licensed; bundled dependencies retain their own licenses.

This **source archive** does not bundle third-party source files, font files, model files or compiled dependencies. `deps/lock.json` records intended upstream dependencies and their declared license information; [dependency provenance](docs/dependencies.md) records the source of each pin.

After real dependency acquisition, native-package generation copies original upstream license/notice files and emits `share/docenhance/THIRD_PARTY_NOTICES.md`, `licenses/`, `sbom.spdx.json` and `build-info.json`. The SPDX document is a **declared-source dependency inventory**, including test-only sources, not a final binary composition scan. It uses `NOASSERTION` where conclusions have not been established.

Apache-2.0, BSD, IJG, libpng, libtiff, Zlib and BSL-1.0 material is not relicensed to MIT by linking it to this project. Little CMS GPL plugins and noncommercial research implementations are not selected. Final distributable composition, patent considerations and license compliance require release review; this file is not a legal-clearance opinion.

When distributed with libjpeg-turbo: **This software is based in part on the work of the Independent JPEG Group.** The original IJG legal notice is retained in `README.ijg` alongside the other JPEG license texts.
