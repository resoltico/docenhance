# Third-party dependencies

Project-owned DocEnhance code is MPL-2.0-licensed; source notices identify its copyright holders; bundled dependencies retain their own licenses.

This **source archive** does not bundle third-party source files, font files, model files or compiled dependencies. `deps/lock.json` records intended upstream dependencies and their declared license information; [dependency provenance](docs/dependencies.md) records the source of each pin.

After real dependency acquisition, native-package generation copies original upstream license/notice files and emits `share/docenhance/THIRD_PARTY_NOTICES.md`, `licenses/`, `sbom.spdx.json` and `build-info.json`. The SPDX document is a **declared-source dependency inventory**, including test-only sources, not a final binary composition scan. It uses `NOASSERTION` where conclusions have not been established.

Apache-2.0, BSD, IJG, libpng, libtiff, Zlib and BSL-1.0 material is not relicensed to MPL-2.0 by linking it to this project. Little CMS GPL plugins and noncommercial research implementations are not selected. Final distributable composition, patent considerations and license compliance require release review; this file is not a legal-clearance opinion.

When distributed with libjpeg-turbo: **This software is based in part on the work of the Independent JPEG Group.** The original IJG legal notice is retained in `README.ijg` alongside the other JPEG license texts.

The private libtiff build modifies `tif_jpeg.c` for per-handle charged JPEG control and
`tif_zip.c` to require completed Deflate striles, including checksum trailers. Original libtiff source notices remain intact; the adapter
header and build recipe are project-owned. Native packages retain libtiff's original license.

The private pocketfft header uses fallible empty factor-vector construction and direct shape/stride
copies so Debug iterator proxy allocation failures can cross the native exception boundary. Original
notices remain intact. Native-package license inventories retain both the repository license and
complete upstream header so the inline attributions remain present.
