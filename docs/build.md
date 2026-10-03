# Build and developer workflows

## Prerequisites and tool ownership

The build is CMake + Ninja. Python scripts are **development tools**, never runtime workers. Git is used only during explicit acquisition and local source-integrity checks. There is no package-manager dependency at runtime.

Recommended versions are recorded in `deps/tools.json`: CMake 4.4.3, Ninja 1.13.2. C++23 remains the product baseline; a recent compiler/standard library is required, including `std::expected`, stop tokens and integer `std::from_chars`. Floating decimal
admission uses an explicit classic locale and does not require floating `from_chars`. Current native compiler families are GCC, Clang/AppleClang, and MSVC. The `dev`, `sanitize` and `tsan` presets require the supported LLVM major rather than whatever `c++` resolves to; see [the compiler contract](#the-compiler-contract-every-preset-states) below. Verification is commit-specific; use the relevant CI checks rather than inferring support from a compiler version.

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

The explicit installer reads the version pins rather than carrying a second list. It installs CMake/Ninja and the pinned JSON Schema test validator/stubs; these are version-pinned developer distributions, **not** part of the application's source lock or its runtime dependencies. A compiler and Git must already be installed. On Windows, use a Visual Studio Developer PowerShell with the current C++ workload. `tools/ci_windows.ps1` demonstrates activation through Microsoft's installed developer-shell script without an extra third-party Action.

## Acquisition is a separate phase

```sh
cmake -P cmake/AcquireDependencies.cmake
```

This is the only default project command that fetches library sources. It does not install system packages. Sources go to `.cache/deps/sources/`; reviewed receipts go to `.cache/deps/receipts/`. Git release tag **objects** are verified before checkout; annotated tags are peeled to commits only after their own object is checked. TIFF archive bytes are verified against their recorded SHA-512 digest. Every source file is then SHA-256 inventoried.

If verification reports that a cached source changed on macOS, check for `.DS_Store` files first: opening `.cache/` in Finder writes them into the verified source trees, and the inventory covers every file. Delete them (`find .cache -name .DS_Store -delete`) and verify again. The check is deliberately exact and is not relaxed for operating-system metadata.

Acquisition is single-writer. Do not run two acquisition processes against one cache concurrently. Build workers may read a completely acquired cache. A failed acquisition removes only its own staging directory. Cached material is verified, not silently repaired or upgraded. A network error is a build-preparation error, not permission to use whatever system library happens to be present.

## Local Linux gate

On macOS/Windows, start Docker and run `python tools/check_all.py`; it includes the full Linux
release workflow with isolated cached native state. Use `python tools/check_linux.py` for that
gate alone. The image/tool/source pins, retained log and precise coverage are described in
[local Linux verification](quality.md#local-linux-verification-with-docker).

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

Projects are built serially, each with two workers by default. This deliberately avoids six upstream projects each spawning an unrestricted set of compiler processes. Set `DE_BUILD_JOBS` in the environment, or override it in a local user preset, if justified; an explicit `-DDE_BUILD_JOBS` or preset value takes precedence over the environment. The value must be 1–64. The same count bounds the architecture check, which runs one compiler process per source file and per public header and reports exactly what it would report serially, in the same order, and the number of tests the suite runs at once. Changing it is not an application memory limit.

## The compiler contract every preset states

Every shared preset names the toolchain it is validated with in `DE_TOOLCHAIN`, and
`cmake/CompilerPolicy.cmake` rejects any other compiler at configure time. Nothing falls back to
whatever `c++` happens to resolve to: Apple clang implements fewer `-Wextra` diagnostics than the
supported LLVM major and GCC, so a local analysis run could otherwise pass on code that the required
CI jobs reject.

| Contract | Presets | Required compiler |
| --- | --- | --- |
| `analysis` | `dev`, `sanitize`, `tsan`, `fuzz`, `fuzz-afl` | LLVM clang at the `deps/tools.json` major (MSVC on Windows) |
| `platform` | `release` | The platform's own release toolchain: GCC, Clang/AppleClang or MSVC |

`release` builds the shipped binary, so it deliberately uses each platform's own toolchain — GCC
on Linux, Apple clang on macOS, MSVC on Windows — exactly as the five required native CI jobs do.
It still refuses a compiler outside those families. On Windows the pinned contract also resolves
to MSVC: there is no validated LLVM-clang build of the dependency superbuild, and required CI
builds and packages Windows with `cl.exe`.

Install the analysis compiler with `python tools/install_llvm.py --compiler` (Homebrew `llvm` on
macOS, apt.llvm.org on Linux), then name it before configuring a **fresh** build directory. On
macOS, where `c++` is Apple clang:

```sh
export CC="$(brew --prefix llvm)/bin/clang" CXX="$(brew --prefix llvm)/bin/clang++"
cmake --workflow --preset dev
cmake --workflow --preset sanitize
cmake --workflow --preset tsan
```

On Linux, where apt.llvm.org installs versioned names:

```sh
export CC=clang-23 CXX=clang++-23
cmake --workflow --preset sanitize
```

Substitute the major recorded in `deps/tools.json` for `23`. Never change compilers inside an
existing build directory. A configure that reports `DE_TOOLCHAIN=analysis requires LLVM clang` is
naming the actual compiler CMake found; install or select the supported major rather than relaxing the
contract.

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
        "CMAKE_C_COMPILER": "/opt/homebrew/opt/llvm/bin/clang",
        "CMAKE_CXX_COMPILER": "/opt/homebrew/opt/llvm/bin/clang++",
        "DE_BUILD_JOBS": "4"
      }
    }
  ]
}
```

Then run `cmake --preset my-clang`, `cmake --build out/my-clang`, and `ctest --test-dir out/my-clang --output-on-failure`. A personal preset selects *where* the analysis compiler lives; it inherits the preset's `DE_TOOLCHAIN` contract and cannot substitute another compiler for it. Never switch compilers, architecture, build type or CRT inside an existing build directory. The x86-64 baseline does not use `-march=native`; ARM64 builds use their corresponding baseline. Cross-compilation is not part of the validated workflow. Native-per-architecture builds are intended.

## Optimization options

`DE_ENABLE_IPO=ON` explicitly requests release interprocedural optimization and fails if the compiler cannot support it. It is off by default because enabling it on an unverified compiler/archiver combination is not evidence of a working release toolchain. Fast-math is prohibited for first-party numerical code.

## Native packaging

```sh
cmake --workflow --preset release
python tools/package_smoke.py dist/docenhance-<project-version>-Linux-x86_64.tar.gz --build out/release/app
```

Use the actual platform-specific filename written by CPack. The release workflow includes tests before packaging. CPack emits a `.tar.gz` and SHA-256 file. Packaging creates only the application and its metadata; upstream command-line tools, tests, compilers and Python are not included. A separate relocated-package smoke test is mandatory in CI.

The version comes only from the top-level `project(... VERSION ...)` command. Substitute the value
shown by `docenhance version` for `<project-version>` above; do not maintain it separately in this
document. Do not copy the entire dependency prefix into a release. Static third-party linkage does
not mean a fully static libc/OS runtime. macOS signing/notarization, Windows signing, minimum-OS
execution and Linux glibc-baseline checks remain later release gates. CI artifacts are validation
artifacts, not signed final application releases.

## Source packaging

```sh
python tools/package_source.py
```

This archives the resolved committed Git tree, with sorted entries, normalized ownership, a fixed timestamp (or explicit `SOURCE_DATE_EPOCH`), SHA-256 sidecar, and internal file manifest. Dirty tracked files and untracked workspace files are excluded. Creating a release-source archive requires a Git checkout; ordinary builds from an extracted source archive remain supported. Each immutable blob supplies both its manifest hash and tar payload. No `.git`, downloaded sources, build outputs, personal presets, bytecode caches or local environment is included. It refuses to replace an existing archive with different bytes; choose a fresh output directory after changes.

## Offline reference tests without third-party sources

```sh
python tools/check_project.py
python -m unittest discover -s tests/tooling -v
python tools/run_tooling_tests.py
python tools/check_reference_suite.py --compiler g++
python tools/check_reference_suite.py --compiler clang++-23
python tools/check_reference_suite.py --compiler g++ --sanitize
```

The last three commands compile the same first-party reference cases used by Catch2. Name the
compiler explicitly, as the required jobs do: `--compiler` otherwise defaults to the host `c++`,
which on macOS is Apple clang. Use `"$(brew --prefix llvm)/bin/clang++"` there for the analysis compiler. They are **not** a second application build framework or evidence that the native CLI/dependencies build. They are useful for rapid numerical and parser validation while dependency acquisition is unavailable.

## Recovery and diagnostic evidence

A source-integrity mismatch is fatal. First inspect the named source and receipt. Do not edit lock pins merely to make a modified cache pass. To deliberately reacquire a dependency, remove only its named source directory and receipt after reviewing their contents, then invoke acquisition again. If a lock or feature policy changes, create a fresh build directory/private prefix; do not mix old installed libraries with new headers.

For build reports, attach the preset name, compiler/tool versions, `deps/lock.json` hash, the failing command, its output and the relevant `CMakeCache.txt`. Never attach actual private documents to a public bug report. A configured workflow file is not proof of a passing workflow.

## Configuration ownership

Use one fresh build directory and its `prefix` per native configuration. The build identity binds
source/tool/feature policy and dependency recipes to the compiler executables, build kind, SDK,
architecture, CRT and instrumentation choices. Identical configuration reuse is supported; changing
those inputs requires a fresh tree. Old unbound caches are refused without migration. Execution
worker counts and fuzz duration may change without changing native compilation identity.

`DE_BUILD_TESTS` and isolated fuzzing own test registration. `BUILD_TESTING` is not an application
option. Required warnings and clang-tidy cannot be disabled. Ambient compile/link flags, toolchain
files, cross-compilation, compiler launchers and nondefault CMake flags are refused rather than
ignored between the parent and child builds. macOS uses the single native architecture and the
exact reviewed deployment target; the selected SDK is forwarded to every child. The release
compiler is GNU on Linux, AppleClang on macOS and MSVC on Windows. Analysis uses LLVM's supported
major on Linux/macOS and native MSVC on Windows; it does not claim identical LLVM patch versions.

The configured dependency plan determines compilation, importing and auditing. Disabling the
development probe excludes its TIFF/Leptonica builds; fuzzing uses its own complete codec/CLI
closure. Package metadata remains the explicitly labeled full declared-source inventory, not a
binary-composition certificate. Native dependency audit runs before the application build even
when unit tests are disabled. Package directories, includes and archives stay in the owning prefix.

The Docker gate selects the Linux daemon's native architecture explicitly. `DOCKER_DEFAULT_PLATFORM`
is refused; its image and native volumes bind architecture, image contents and dependency/build
policy. One gate owns a checkout at a time. Each run retains its own diagnostic log, with
`latest.log` a convenience copy. An interrupted writer claim requires inspection before removal.

## Complete execution and package evidence

The native workflow reconciles actual compiler entries, Catch JSON discovery, CLI scripts,
manifest-declared corpus replay and fresh CTest JUnit. Required tests cannot be disabled, skipped,
empty assertion work or expected-failure cases. Catch uses `SKIP_IS_FAILURE`; its XML summaries
prove assertion work, while CTest results prove every registered process completed. Passing output
has a finite 1 MiB capture bound; truncation or missing evidence fails rather than becoming success.
The strict tooling runner likewise requires every test module and refuses skipped cases.

Package inspection requires the independently tested application build directory (`--build`). It
compares the relocated executable byte-for-byte, all specs/schemas and the closed file inventory,
regenerates upstream notices/licenses/SPDX from verified locked sources, checks complete runtime
build/capability facts and native OS imports, then exercises B02/B03/I01/D01 and verifies their
bundles. License regeneration replaces only its configured build's owned `package-metadata`
directory after successful preparation, removing stale generated files.

These checks establish execution, agreement and declared support. They do not establish exhaustive
fuzz coverage, every-object/assembly instrumentation, legal clearance, document authenticity,
performance or execution on every older OS named by a deployment load command.

Native packaging uses CPack External for the owned install stage and a byte-payload tar writer.
The resulting `.tar.gz` preserves intended executable permissions and excludes host extended
attributes/resource forks; the generator's JSON is build metadata, not an additional runtime
payload. Native binaries are still not claimed to be bit-reproducible across builds.

## Required filesystem test fixtures

The full native suite must be able to create file and directory symbolic links in its temporary
workspace. On Windows, enable Developer Mode or provide the symbolic-link creation privilege for
the test process. Failure to create a required link is a failed prerequisite, not a passing cleanup
or refusal test. POSIX FIFO checks remain POSIX-specific; Windows handle replacement has its own
native cases. These test prerequisites do not add a product runtime privilege requirement.
