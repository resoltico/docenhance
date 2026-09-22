# Dependency provenance and feature policy

Verified against upstream metadata on **17 September 2026**, and every pinned source has since been acquired, digest-inventoried and built on macOS arm64 and Linux arm64. The source lock records exact Git release objects or a byte digest; ordinary builds never track a moving `latest` branch.

The MIT license applies to original project code only. License expressions below are upstream declarations, not a relabeling or a comprehensive legal review.

## zlib — 1.3.2

Declared license: `Zlib`. Scope: `runtime-candidate`.

Official source: https://github.com/madler/zlib.git

Release ref: `refs/tags/v1.3.2`. Pinned **tag object**: `216c70c020aa53f0c40920d155f808b6b59c9acb`.

Verification source: https://api.github.com/repos/madler/zlib/git/ref/tags/v1.3.2

## jpeg — 3.2.0

Declared license: `BSD-3-Clause AND IJG AND Zlib`. Scope: `runtime-candidate`.

Official source: https://github.com/libjpeg-turbo/libjpeg-turbo.git

Release ref: `refs/tags/3.2.0`. Pinned **commit object**: `c85e6b905bf237038faa936dab160ebfc5da0344`.

Verification source: https://api.github.com/repos/libjpeg-turbo/libjpeg-turbo/git/ref/tags/3.2.0

## png — 1.6.58

Declared license: `libpng-2.0`. Scope: `runtime-candidate`.

Official source: https://github.com/pnggroup/libpng.git

Release ref: `refs/tags/v1.6.58`. Pinned **tag object**: `fdc7185dfedbddce8c2487bc171f66af4fca24ab`.

Verification source: https://api.github.com/repos/pnggroup/libpng/git/ref/tags/v1.6.58

## tiff — 4.7.2

Declared license: `libtiff`. Scope: `runtime-candidate`.

Official source archive: https://gitlab.com/libtiff/libtiff/-/archive/v4.7.2/libtiff-v4.7.2.tar.gz

`sha512`: `c4dcde3c79e5d69c7231f8862e2e5a83d90d9cce694fb2a4804800b2f8f1bc9db504b9252d81dce872eec8358b33a3a1dbdddcbb6181f6fb8d1d7fc0e9a9fc6a`. The digest comes from the version-matched vcpkg maintainer port, not a locally downloaded archive. Verification port blob: `50e0db0e7f271ae49f562652584d808fc1868100`.

Verification source: https://github.com/microsoft/vcpkg/blob/master/ports/tiff/portfile.cmake

## opencv — 5.0.0

Declared license: `Apache-2.0`. Scope: `runtime-candidate`.

Official source: https://github.com/opencv/opencv.git

Release ref: `refs/tags/5.0.0`. Pinned **tag object**: `9e2ede9628a55ec2742a1b180d3e69b0322281b9`.

Verification source: https://api.github.com/repos/opencv/opencv/git/ref/tags/5.0.0

## leptonica — 1.87.0

Declared license: `BSD-2-Clause`. Scope: `runtime-candidate`.

Official source: https://github.com/DanBloomberg/leptonica.git

Release ref: `refs/tags/1.87.0`. Pinned **commit object**: `13275a278eb55b5746e33f95fbf5a2c8f604b3ab`.

Verification source: https://api.github.com/repos/DanBloomberg/leptonica/git/ref/tags/1.87.0

## lcms — lcms2.19.1

Declared license: `MIT`. Scope: `runtime-candidate`.

Official source: https://github.com/mm2/Little-CMS.git

Release ref: `refs/tags/lcms2.19.1`. Pinned **commit object**: `21c582a594fe5279f90c0b93437c398f93bf62b0`.

Verification source: https://api.github.com/repos/mm2/Little-CMS/git/ref/tags/lcms2.19.1

## cli11 — 2.7.2

Declared license: `BSD-3-Clause`. Scope: `runtime-candidate`.

Official source: https://github.com/CLIUtils/CLI11.git

Release ref: `refs/tags/v2.7.2`. Pinned **tag object**: `919ce47dad39520a6e9a695d3e2d222c6920a445`.

Verification source: https://api.github.com/repos/CLIUtils/CLI11/git/ref/tags/v2.7.2

## json — 3.12.0

Declared license: `MIT`. Scope: `runtime-candidate`.

Official source: https://github.com/nlohmann/json.git

Release ref: `refs/tags/v3.12.0`. Pinned **tag object**: `65ee68451d8eb2b5f3a30b410476ab83deb3289b`.

Verification source: https://api.github.com/repos/nlohmann/json/git/ref/tags/v3.12.0

## picosha2 — 1.0.1

Declared license: `MIT`. Scope: `runtime-candidate`.

Official source: https://github.com/okdshin/PicoSHA2.git

Release ref: `refs/tags/v1.0.1`. Pinned **commit object**: `161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29`.

Verification source: https://api.github.com/repos/okdshin/PicoSHA2/git/ref/tags/v1.0.1

## catch2 — 3.16.0

Declared license: `BSL-1.0`. Scope: `test`.

Official source: https://github.com/catchorg/Catch2.git

Release ref: `refs/tags/v3.16.0`. Pinned **tag object**: `fd79eadb5bc1760e7cbae12fd45b0d0040d1bb73`.

Verification source: https://api.github.com/repos/catchorg/Catch2/git/ref/tags/v3.16.0

## Feature policy and audit

`deps/features.json` is the requested configuration. Common static/prefix/toolchain options are in `cmake/Superbuild.cmake`. `tools/audit_build.py` checks actual configured values and the OpenCV module closure after a native build. A cached flag alone is not a proof that every upstream code path is absent; final binary/link and license review remains mandatory.

OpenCV 5 is restricted to `core`, `flann`, `geometry`, `imgproc`, `photo`; the additional two modules are a real upstream dependency change. No `imgcodecs`, DNN, GUI or video module is selected. Dedicated JPEG/PNG/TIFF adapters remain the future I/O authority. Internal OpenCV downloads fail via its pre-download hook. KleidiCV, Carotene, IPP, ARMPL, ONNX Runtime, DirectML, OpenCL/CUDA/Vulkan, GUI and video backends are disabled. OS runtime libraries remain permitted; Windows Leptonica links its normal user32/gdi32 system libraries, not a bundled GUI toolkit or interactive interface.

Leptonica's normal codec integrations are disabled. Little CMS builds only its base static library, without tools or GPL plugins. TIFF disables JBIG, LERC, LZMA, WebP, Zstd and libdeflate, retaining baseline codecs plus JPEG and zlib compression. Test frameworks never become runtime requirements.

The source lock does not pin the operating system, compiler binaries or developer-wheel bytes. It is a source-identity and feature lock, not a claim of a hermetic reproducible machine image. The CMake/Ninja versions are pinned separately; CI records actual compiler versions. A source digest is not a security endorsement.

## Dependency updates

Review official release notes, source identity, licenses, module closure, options and API changes. Update the lock and feature policy together where necessary. Acquire in a clean named cache entry and build in a new private prefix. Run the native API probe, numerical/CLI tests, feature audit and relocated-package smoke tests. Do not automate acceptance of a new release merely because its version is higher.
