# Build configuration and dependency integrity audit

## Design pass

Trace supported native builds from the reviewed tool/source/feature policy through presets,
compiler selection, source acquisition, dependency configuration/importing, application compilation,
Docker/CI execution and packaging. Preserve current numerical and runtime contracts. Keep one
owner per policy fact and reject unsupported configuration instead of ignoring it.

Concrete findings and remediation:

- A cached package `*_DIR` bypasses `NO_DEFAULT_PATH`. Validate it before executing package code;
  confine imported headers/archives to the owning prefix. Read exact versions from the source lock.
- A configured build tree and installed prefix could be reused after changing recipes, features,
  compiler/SDK/architecture or instrumentation. Bind those inputs once; require a fresh tree on
  change and reject unbound older caches. Each superbuild owns exactly one prefix and app child.
- `BUILD_TESTING=OFF` configured successfully with `DE_BUILD_TESTS=ON` but registered zero root
  tests. Remove the second application control; derive registration from actual test/fuzz builds.
- The `pinned` compiler selector accepted different LLVM patch versions and native MSVC. Retire
  that misleading selector. Analysis requires the reviewed LLVM major on Linux/macOS and MSVC on
  Windows; release uses the validated native GNU/AppleClang/MSVC families. Probe required standard
  library features under the actual SDK and deployment floor. No floating `from_chars` is required. Require the pinned Python JSON Schema validator before
  dependency preparation, with checks that remain active under optimized Python execution.
- Ambient/nondefault flags and native toolchain overrides could differ between parent and child.
  Refuse unreviewed flags, remapping and launchers. Bind macOS to the native architecture and
  reviewed deployment target; forward the actual SDK to dependencies and the application.
- Dependency feature auditing skipped expanded build paths and missing optional-looking caches.
  Audit the actual selected closure, including expanded paths and resolved codec providers, before
  building the application even when unit tests are disabled. Do not compile unused probe packages.
  Fresh builds exposed six unused Little CMS keys; retire them and send common arguments only
  to recipients that consume them. Upstream unused-argument diagnostics become errors.
- Archive source/receipt mutation together passed verification. Derive the expected source
  inventory from the original digest-checked archive, with bounded ordinary files/directories and
  no links/duplicates. Git must ignore caller environment/global configuration and inventory
  nested `.git`-named source directories. Receipts supplement immutable evidence rather than replace it.
- Cache preparation lacked enforced single-writer ownership. Add one exclusive claim mechanism
  for dependency acquisition, Linux gate state and source-built LLVM tools. Partial LLVM caches
  require both tools and matching source/recipe/byte readiness evidence; no existence-only adoption.
- Docker image/native state did not bind the daemon architecture and dependency recipes. Select
  the native Linux daemon platform explicitly, reject ambient emulation, isolate image/architecture/
  recipe state, and retain a distinct log per invocation. Share canonical worker limits with CMake/CI.

## Separate design QA

Real CMake negative controls demonstrated cached foreign provider execution and zero registered
root tests despite enabled project tests. A real archive/receipt fixture demonstrated the mutable
receipt gap. Challenge the remedies at those same boundaries rather than with source-text matches.

Path validation must tolerate canonical host aliases such as macOS `/var` versus `/private/var`,
without admitting a different prefix or executing foreign package code. Validate the existing
parent before creating its owned prefix. Fresh configuration succeeds; unchanged reuse succeeds;
changed or missing configuration bindings fail. Mutable worker/fuzz-duration controls do not
change compiled dependency identity. Preserve precise failure diagnostics and do not relax gates.

The same policy must reach the application child and every dependency. A source hash establishes
identity, not review quality. Configuration and cache bindings are not a hostile-build-directory
sandbox, a compiler security endorsement or a reproducible-machine-image certificate. Package
metadata remains labeled as the full declared-source inventory; it does not certify binary composition.

Retire old controls and unbound caches directly, with no forwarding aliases or migrations. Tests,
consumers, CI, documentation and package checks must move together. Actual workflow/platform
results belong to the delivered commit and logs, not to this design record.

CI challenged tool selection with generic LLVM 18 and major-qualified LLVM 23 installed together.
The validator refused the wrong default; prefer the major-qualified installed tool and retain
that regression for both clang-tidy and clang-query. The version requirement is unchanged.

Windows QA found that the locked Little CMS release never consumes its POSIX thread switch on
Windows. Declare that switch once in the Unix feature policy and apply/audit the same scope;
keep unused-argument errors enabled on every platform.

Windows also confirmed the header-only PicoSHA2 adapter consumes no package search prefix.
Do not pass that irrelevant lookup control to its install-only configuration.
