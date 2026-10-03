# Clean-checkout build and delivered-product integrity

## Design pass

Exercise the documented source path with an extracted committed archive, no Git metadata,
no personal presets, a newly installed pinned tool environment and freshly acquired dependencies.
Keep configure/build offline after explicit acquisition. Inspect the relocated native package
against that tested build and independently regenerated locked-source licenses and schemas.

Concrete gaps:

- Installing CMake in a source-local virtual environment puts its own modules inside the strict
  project diagnostic scope. The documented setup fails before dependency configuration. Keep build
  tools outside source/build trees and diagnose this unsupported layout before language setup.
  Preserve strict uninitialized, author, deprecated and unused-argument checks.
- Ambient compiler search variables can inject undeclared headers/libraries without appearing in
  compile commands or configuration identity. Check the same environment policy at configuration
  and immediately before outer dependency work and first-party target builds. Refuse compiler
  flag/search overrides; retain explicit compiler selection and native Windows SDK search paths.
- Native packages ship a source README whose processing contracts are absent. Deliver the existing
  reviewed Markdown tree beside the existing schemas/metadata under `share/docenhance`, retaining
  local navigation and original attribution. Inspect the closed byte inventory and actual links.
  Source/build instructions still require the separate source distribution; no build sources or
  runtime tools are added to the native package.
- Source-facing support prose says no release or decoding exists, and a documented release command
  retains an exported analysis compiler incompatible with the platform release contract. Correct
  current support/capability facts and explicitly select each platform's release compiler.

Use the current configuration, source receipt, packaging and verification owners. No new setup
wrapper, package manager, runtime component, compatibility path or product operation is required.

## Separate design challenge

An external tool environment must succeed with the same pinned CMake version and strict presets;
moving it cannot substitute for completing the real workflow. A failed configure may retain its
old Python/Ninja paths: use a fresh build tree when changing the environment rather than assuming
activation rewrites existing cache identity. The installer/environment is not a hermetic SDK.

A compiler-injected `expected` header through CPATH is reached by real AppleClang. Configuration
alone cannot constrain a later build's environment, so a configured first-party target must refuse
new injection before compiling. Windows INCLUDE/LIB are native toolchain prerequisites, not an
undeclared compiler-flag channel; rejecting them would break the supported developer shell.
Compromised compilers, executable search paths, SDKs and hostile build trees remain trust limits.

Documentation presence alone cannot prove usable contracts: remove a linked document or schema
and require the actual local-link check to fail. Byte comparison still binds the delivery to the
reviewed source, while executable/schema/method smoke cases independently check advertised behavior.
License inventories remain explicitly declared-source inventories, not exhaustive binary composition
or legal clearance certificates. OS import inspection and deployment commands do not prove execution
on every historical OS or bit-reproducible compilation.

The hosted normal v0.5.0 release has source archive/checksum assets and a publication timestamp;
that evidence contradicts the old no-release security text. It does not establish a signed native
release. Correct working documentation without rewriting public history or published artifacts.

## Concrete negative controls

The original bootstrap configured successfully with a CPATH-injected header directory; the
corrected bootstrap refuses it with the intended diagnostic. A real compiler resolves the injected
header, independently demonstrating the undeclared search path. Existing-target tests add CPATH
after configuration and fail before producing an executable. Source/build-local CMake tests require
the actionable layout refusal, and every forbidden compiler/linker variable has a direct control.

The clean baseline native release executes all 192 cases and passes its original relocated package
inspection while its shipped README has 24 broken local links. The corrected inspection rejects
missing linked documents/schemas separately from closed-file byte differences. Runtime method/format
checks, original locked-license comparisons and OS import inspection remain required.

[Microsoft's linker environment contract](https://learn.microsoft.com/en-us/cpp/build/reference/linking)
and [Clang's driver contract](https://clang.llvm.org/docs/UsersManual.html) also identify command
editing channels invisible to compilation-database arguments. Refuse LINK/_LINK_ and
CCC_OVERRIDE_OPTIONS alongside CL/_CL_ and compiler search overrides. Empty flag values do not
request changes; ordinary PATH and Windows SDK resolution remain explicit native trust boundaries.

A hosted TSan detection failure exposed a diagnostic gap: the runner preserved child logs in the
build tree but printed only a summary, and the hosted output could not distinguish startup failure
from absent race detection. Report both process exit codes, bounded stderr excerpts and the evidence
directory without changing detection criteria. A controlled startup failure must remain a failure;
its complete log is retained while CTest output remains bounded. Real instrumented/uninstrumented
controls protect success/refusal independently. Linux ARM64 process controls do not establish hosted x86-64 detection; keep that distinction
explicit. Subsequent hosted diagnostics showed benign and fault exits both zero with no race
message, establishing missed fault detection rather than a runtime startup failure.

## Race-control revision and separate challenge

The two-write latch control could complete without a TSan report on hosted x86-64. Single-word
relaxed-handshake variants also missed reports in repeated macOS runs and were discarded. The
adopted control retains a separate heap array of 64 volatile integers and performs 32,768 rounds
of stores per writer. One worker and the calling thread use relaxed start/completion flags outside
the access region; construction completes before start, and the worker remains live until the
calling thread finishes. No synchronization or arithmetic update is inserted into the racy region.
Volatile keeps accesses observable but does not make them atomic. Each process must still produce
a fatal data-race diagnostic within the unchanged supervisor deadline; there are no retries or
accepted missing reports.

[C++ atomic ordering](https://eel.is/c++draft/atomics.order) specifies relaxed ordering; the
[Clang TSan example](https://clang.llvm.org/docs/ThreadSanitizer.html) likewise uses observable
unsynchronized accesses. Forty native macOS ARM64 controls and eighty optimized Linux ARM64
controls detect the array fault. Strict debug/release/ASan/UBSan/TSan builds and actual detection
checks pass. These finite controls challenge compiler elimination and detection; they do not
establish exhaustive detector completeness. An uninstrumented binary must still fail.

The large JPEG marker-byte contract exposes an invalid ten-second throughput assumption under
instrumentation and host contention. Its semantics establish byte/work limits, not a wall-clock
latency promise. The shared CLI supervisor uses a finite thirty-second command watchdog; full
workflow and individual CTest bounds, inputs, assertions, discovery and result reconciliation
remain enforced. A real blocked child is terminated and refused. Serial scheduling alone did not
solve the host-dependent limit and is removed rather than retained as ineffective scaffolding.

Standard unittest discovery must bootstrap each tooling module independently. The strict runner
removes its script-directory advantage before discovery; required modules explicitly import the
common project-root bootstrap. Both entry points must discover and execute the same full set.
