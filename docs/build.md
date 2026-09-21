# Build and developer workflows

## Prerequisites and tool ownership

The build is CMake + Ninja. Python scripts are **development tools**, never runtime workers. Git is used only during explicit acquisition and local source-integrity checks. There is no package-manager dependency at runtime.

Recommended versions are recorded in `deps/tools.json`: CMake 4.4.3, Ninja 1.13.2. C++23 remains the product baseline; a recent compiler/standard library is required, including floating-point `std::from_chars`. Current native compiler families are GCC, Clang/AppleClang, and MSVC. The authoring environment tested only GCC 14.2 and Clang 17 against the dependency-free reference suites; this is not a full platform-support certification.

An optional developer installation route uses an isolated Python environment:

```sh
python3 -m venv .venv
. .venv/bin/activate
python tools/install_build_tools.py
```

Windows PowerShell equivalents:

```powershell
py -3 -m venv .venv
.\.venv\Scripts\Activate.ps1
python tools/install_build_tools.py
```

The explicit installer reads the version pins rather than carrying a second list. It installs the maintainers' CMake/Ninja wheels; these are version-pinned developer distributions, **not** part of the application's source lock or its runtime dependencies. A compiler and Git must already be installed. On Windows, use a Visual Studio Developer PowerShell with the current C++ workload. `tools/ci_windows.ps1` demonstrates activation through Microsoft's installed developer-shell script without an extra third-party Action.

## Acquisition is a separate phase

```sh
cmake -P cmake/AcquireDependencies.cmake
```

This is the only default project command that fetches library sources. It does not install system packages. Sources go to `.cache/deps/sources/`; reviewed receipts go to `.cache/deps/receipts/`. Git release tag **objects** are verified before checkout; annotated tags are peeled to commits only after their own object is checked. TIFF archive bytes are verified against their recorded SHA-512 digest. Every source file is then SHA-256 inventoried.

If verification reports that a cached source changed on macOS, check for `.DS_Store` files first: opening `.cache/` in Finder writes them into the verified source trees, and the inventory covers every file. Delete them (`find .cache -name .DS_Store -delete`) and verify again. The check is deliberately exact and is not relaxed for operating-system metadata.

Acquisition is single-writer. Do not run two acquisition processes against one cache concurrently. Build workers may read a completely acquired cache. A failed acquisition removes only its own staging directory. Cached material is verified, not silently repaired or upgraded. A network error is a build-preparation error, not permission to use whatever system library happens to be present.

## Everyday commands

```sh
cmake --workflow --preset dev
cmake --build --preset dev
ctest --preset dev
cmake --build out/dev --target check-project
cmake --build out/dev --target check-spec
cmake --build out/dev --target check-architecture
```

The workflow expands to configure, build and test. Sources are verified at configure time and before every outer build. Each upstream project has its own binary directory and shares only a **configuration-private** installation prefix. The application is configured afterwards in `out/dev/app/`. IDEs should use the outer preset to prepare dependencies and read `out/dev/app/compile_commands.json` for first-party source navigation.

Projects are built serially, each with two workers by default. This deliberately avoids six upstream projects each spawning an unrestricted set of compiler processes. Override `DE_BUILD_JOBS` in a local user preset if justified; the value must be 1–64. Changing it is not an application memory limit.

## Selecting compilers and personal settings

Do not modify shared presets to accommodate one machine. Create ignored `CMakeUserPresets.json`:

```json
{
  "version": 12,
  "configurePresets": [
    {
      "name": "my-clang",
      "inherits": "dev",
      "binaryDir": "${sourceDir}/out/my-clang",
      "cacheVariables": {
        "CMAKE_C_COMPILER": "clang",
        "CMAKE_CXX_COMPILER": "clang++",
        "DE_BUILD_JOBS": "4"
      }
    }
  ]
}
```

Then run `cmake --preset my-clang`, `cmake --build out/my-clang`, and `ctest --test-dir out/my-clang --output-on-failure`. Never switch compilers, architecture, build type or CRT inside an existing build directory. The x86-64 baseline does not use `-march=native`; ARM64 builds use their corresponding baseline. Cross-compilation is not part of the validated scaffold workflow. Native-per-architecture builds are intended.

## Optimization options

`DE_ENABLE_IPO=ON` explicitly requests release interprocedural optimization and fails if the compiler cannot support it. It is off by default because enabling it on an unverified compiler/archiver combination is not evidence of a working release toolchain. Fast-math is prohibited for first-party numerical code.

## Native packaging

```sh
cmake --workflow --preset release
python tools/package_smoke.py dist/docenhance-0.1.0-foundation-Linux-x86_64.tar.gz
```

Use the actual platform-specific filename written by CPack. The release workflow includes tests before packaging. CPack emits a `.tar.gz` and SHA-256 file. Packaging creates only the application and its metadata; upstream command-line tools, tests, compilers and Python are not included. A separate relocated-package smoke test is mandatory in CI.

The version comes from the top-level `project(... VERSION ...)` command; `0.1.0` filenames above are examples for this delivered scaffold. Do not copy the entire dependency prefix into a release. Static third-party linkage does not mean a fully static libc/OS runtime. macOS signing/notarization, Windows signing, minimum-OS execution and Linux glibc-baseline checks remain later release gates. CI artifacts are development artifacts, not signed final application releases.

## Source packaging

```sh
python tools/package_source.py
```

This creates a deterministic source archive, with sorted entries, normalized ownership, a fixed timestamp (or explicit `SOURCE_DATE_EPOCH`), SHA-256 sidecar, and internal file manifest. No `.git`, downloaded sources, build outputs, personal presets, bytecode caches or local environment is included. It refuses to replace an existing archive with different bytes; choose a fresh output directory after changes.

## Offline reference tests without third-party sources

```sh
python tools/check_project.py
python -m unittest discover -s tests/tooling -v
python tools/check_foundation.py --compiler g++
python tools/check_foundation.py --compiler clang++
python tools/check_foundation.py --compiler g++ --sanitize
```

The last three commands compile the same first-party reference cases used by Catch2. They are **not** a second application build framework or evidence that the native CLI/dependencies build. They are useful for rapid numerical and parser validation while dependency acquisition is unavailable.

## Recovery and diagnostic evidence

A source-integrity mismatch is fatal. First inspect the named source and receipt. Do not edit lock pins merely to make a modified cache pass. To deliberately reacquire a dependency, remove only its named source directory and receipt after reviewing their contents, then invoke acquisition again. If a lock or feature policy changes, create a fresh build directory/private prefix; do not mix old installed libraries with new headers.

For build reports, attach the preset name, compiler/tool versions, `deps/lock.json` hash, the failing command, its output and the relevant `CMakeCache.txt`. Never attach actual private documents to a public bug report. A configured workflow file is not proof of a passing workflow.
