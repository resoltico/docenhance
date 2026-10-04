# Third-party dependencies

DocEnhance is copyright 2026 Ervins Strauhmanis and MPL-2.0-licensed; bundled dependencies retain their own licenses.

This **source archive** does not bundle third-party source files, font files, model files or compiled dependencies. `deps/lock.json` records intended upstream dependencies and their declared license information; [dependency provenance](docs/dependencies.md) records the source of each pin.

After real dependency acquisition, native-package generation copies original upstream license/notice files and emits `share/docenhance/THIRD_PARTY_NOTICES.md`, `licenses/`, `sbom.spdx.json` and `build-info.json`. The SPDX document is a **declared-source dependency inventory**, including test-only sources, not a final binary composition scan. It uses `NOASSERTION` where conclusions have not been established.

Apache-2.0, BSD, IJG, libpng, libtiff, Zlib and BSL-1.0 material is not relicensed to MPL-2.0 by linking it to this project. Little CMS GPL plugins and noncommercial research implementations are not selected. Final distributable composition, patent considerations and license compliance require release review; this file is not a legal-clearance opinion.

When distributed with libjpeg-turbo: **This software is based in part on the work of the Independent JPEG Group.** The original IJG legal notice is retained in `README.ijg` alongside the other JPEG license texts.

## OpenCV NLM source notice

The native NLM source retains this upstream notice in addition to OpenCV's repository license.
Packaged license inventories include the original source files carrying it.

```text
/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                        Intel License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000, Intel Corporation, all rights reserved.
// Third party copyrights are property of their respective icvers.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of Intel Corporation may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/
```
