# Foundation audit — 22 September 2026

Baseline: `4f65f823b52ac84dac320461bc392b1bda4fd70d` (v0.2.0).
The downloaded release source reproduces Git tree
`3ed643c2349edfee72a4458f2e55b550e247043c` exactly, including modes.
This review changes the foundation deliberately; it does not add compatibility shims.

## Audit evidence

| Finding | Consequence | Remediation |
|---|---|---|
| `de_app` directly decodes, processes and publishes while the CLI fuzzer invokes it | A supposedly deterministic parser fuzzer can read/write the host filesystem | Explicit processing port; pure application validation; host implementation composed only in `main` |
| `Invocation` strings flow into execution, with independently mutable outcome/exit fields | No typed admission boundary; contradictory responses can be constructed | Validated process request; outcome status derived from its payload |
| `std::filesystem::rename` follows an existence precheck | A concurrent empty destination directory can be overwritten on POSIX | Native atomic no-replace operation; fail closed where unsupported |
| Fixed shared staging name and recursive cleanup | Collisions and unnecessarily broad deletion authority | Exclusive per-attempt staging ownership; remove only the owned result and staging directory |
| Simplified PNG API allocates outside the page budget and applies implicit image interpretation | Memory promises and sample semantics are not established by the implementation | Low-level libpng adapter; charged allocator callbacks; explicit supported grayscale subset |
| Public `PlaneView` accepts inconsistent shape/storage; B03 does not reject overlap | Invalid spans can reach memory-unsafe kernels; aliasing changes results | Checked view factory and common overlap predicate |
| Buffers retain a raw pointer to a stack-owned `Budget` | Escaping an otherwise owning buffer causes use-after-scope | Refcounted accounting state owned by both budget and outstanding buffers |
| `std::jthread` launch/task exceptions are not contained | Expected resource failure escapes; worker exceptions terminate the process | Contain exceptions at the scheduling boundary; join started workers on every path |
| Fuzzer libraries duplicate production source lists; nightly acquisition and target loops drift | Latest campaigns fail before fuzzing; `box_mean` is omitted | Reuse actual layer targets and CTest target discovery; dependency-free CLI effect boundary |
| Corpus reader converts signed `char` implicitly to `uint8_t` | Four sanitizer corpus replays abort on high-bit bytes | Explicit byte-preserving conversion and regression input |
| Reference sanitizer flags omit fatal recovery; ASan failure skips TSan; PR gate lacks full sanitizer suite | Tests can report success after a finding, or miss a whole class of checks | Fatal shared sanitizer policy and independent required sanitizer jobs |
| Schema is permissive and tested by a partial homemade validator; status/docs contradict live code | Invalid responses and unsupported claims can appear authoritative | Full JSON Schema validation, payload alternatives, negative tests, factual current status |
| Source architecture checks ignore explicit allocator calls and can crash on unowned files | Rules can be bypassed without a useful diagnostic | Harden manifest/layout validation and compiler matcher tests |

Live evidence: nightly run `35705919755`, sanitized job `106674686912`, and libFuzzer
job `106674686924`. The sanitized native build completed; four corpus replays failed on
implicit signed-to-unsigned conversions in `fuzz/support/replay_main.cpp`. The fuzz campaign
failed because it acquired CLI11/JSON but configured a build requiring PNG/zlib too.
The dependency-free GCC 14 reference suite passes at the baseline; that is not evidence
that the native pipeline or the pinned five-platform matrix passes.

## Design pass (before implementation)

### Authority and effects

Retain C++23, explicit CMake targets, the reviewed layer manifest, private third-party types,
`std::expected`, non-neural CPU-only processing, and the locked-source build. These foundations
are useful; a wholesale language/build-system replacement has no demonstrated benefit.

The application owns request validation and capability reporting. Its processing port accepts
only an admitted `ProcessRequest`, never raw CLI arguments. `de_host` implements this port using
codecs and kernels. The CLI requires a port explicitly; `main` composes the production port.
Fuzzers and application tests supply a non-I/O port, so they exercise parsing, validation,
dispatch and presentation without filesystem authority. No runtime switch can select the test port.

The existing B03 pipeline remains real. Planned methods stay unavailable. There is no plugin
loader, service locator, dependency-injection framework or speculative universal recipe engine.
The request type is the extension point when another complete method is added.

### Values, resources and failure

Validate borrowed image shape/extent at construction. Kernel-specific checks still reject empty,
shape-mismatched and overlapping views. Buffers own an accounting reference rather than borrowing
the lifetime of their allocating budget. The finite page allocation ceiling covers image and codec
blocks; it is not advertised as a process-RSS limit or a bound on operating-system thread stacks.

Use libpng's custom allocator interface with a fixed-size allocation registry charged to the same
budget. Keep setjmp frames free of nontrivial automatic destructors and keep resource owners outside
those frames. Read stored grayscale samples deliberately; reject unsupported color/alpha/depth.
Release decoder state on all errors and close the encoded file before commit.

Expected filesystem, allocation and thread-launch failures are values. Catching at a foreign-library
or thread boundary is necessary containment, not forbidden business logic; the manifest explicitly
allows those owners. Unexpected worker exceptions become invariant failures on the caller, not
`std::terminate`. Presentation failure after execution does not trigger a second misleading response.

### Publication

Stage next to the destination, claim a new staging directory exclusively, and record its ownership.
On Linux use `renameat2(RENAME_NOREPLACE)`; on macOS use `renamex_np(RENAME_EXCL)`; on Windows use
`MoveFileExW` without replacement/cross-volume-copy flags. Never fall back to check-then-rename.
Only delete paths this attempt owns. Return an explicit publication state; uncertainty/failed cleanup
is not silently reclassified as “not started.” Atomic visibility is not a crash-durability claim.
The output parent must be trusted against hostile ancestor replacement; this is not a filesystem sandbox.

### Build and enforcement

Define production sources once. Fuzz builds instrument the same pure layer targets, with no host/codec
linkage in the CLI fuzz closure. Drive campaigns through CTest rather than a shell-maintained target list.
Keep expensive campaigns scheduled, but make the complete ASan/UBSan and TSan suites independent PR jobs.
Do not weaken warning policies, suppress the byte conversion, or delete reproducing corpus inputs.

Validate command responses with the real Draft 2020-12 implementation (test-only). Pin the validator
alongside other developer tools. Describe verification by commit/platform/check, not evergreen prose
claiming that all suites pass. Keep version 0.2.0 until the maintainer performs a release; describe the
breaking changes under `Unreleased`.

## Design QA (separate, before implementation)

| Challenge | Decision |
|---|---|
| Could injection bypass validation? | The port receives a private-construction request; application tests assert malformed invocations never call it. |
| Does fixing the fuzzer reduce it to parser-only coverage? | No: retain full CLI/JSON execution with an explicit deterministic, non-I/O processor. Add real codec/publication tests separately. |
| Does a “safe” rename still replace a concurrent destination? | Native no-replace is the commit operation itself, not a precheck. Test two publishers and pre-existing files/directories/symlinks. |
| Could cleanup delete another caller's data? | Never use recursive deletion in publication. Only the successful staging creator owns its two known paths. |
| Can libpng jump across a C++ destructor? | Jump frames contain only trivial locals; all owners and allocation registry live in the caller. Test malformed/truncated data and memory refusal under sanitizers. |
| Can a buffer outlive its budget safely? | Both retain the ledger; only the last reference destroys it. Add escaping-buffer and move/refund tests. |
| Does the budget pretend to bound all memory? | No: document charged buffers/codec blocks, metadata and OS-stack exclusions. |
| Does a failed worker leave a joinable thread or kill the process? | Catch work exceptions per item and join every started worker; return deterministic lowest-index task failure. |
| Are the new rules merely text checks that can approve an unparsed file? | Retain fail-closed compiler checks and add negative tests for layout, graph and allocation-call bypasses. |
| Is this an indiscriminate rewrite? | No: keep numerical definitions, public method IDs, pinned dependencies and useful gates; replace only demonstrated unsafe seams. |
| Can local tool limitations be hidden by a green subset? | No: list actual local checks and rely on the published PR's separate CI status for the pinned matrix. |

## Additional enforcement design and QA

During implementation verification, the suppression registry was found to hash only the comment
for Python markers and C++ next-line markers, rather than the code it claimed to protect. Balanced
`NOLINTBEGIN` blocks were also accepted despite the documented ban on range-wide forms.
The remediation binds next-line fingerprints to both marker and target, binds Python markers to
the complete physical source line, and rejects all NOLINT ranges. QA requires negative tests proving
that changing the target invalidates an exception while unrelated line shifts do not. Existing
reasons are retained only after recomputing their code-bound keys; there is no old-key compatibility.

CMake 4.4 also implicitly scanned ordinary C++23 translation units for modules, introducing GCC-only
module flags into clang-tidy's parse. The project has no CXX_MODULES file sets: explicitly disabling
automatic scanning documents that build model without relaxing compiler or analysis warnings.

## Implementation and verification

The application/host split, checked image ownership, bounded codec adapter, exclusive publication,
shared production fuzz targets and fail-closed enforcement are implemented together. No migration
layer or compatibility alias is retained. The old codec capability facade and CLI-owned composition
root are removed. The committed version remains 0.2.0; these are unreleased changes.

Local verification used Linux x86-64, Python 3.13, GCC 14.2, Clang/clang-tidy 23.1.2, CMake 4.4.3,
Ninja 1.13.2 and the locked dependency sources. No warning or sanitizer gate was disabled to obtain
the following results.

| Check | Result |
|---|---|
| Project, lock, generated contract and source architecture | Passed |
| Size, configuration, coverage and code-bound suppression gates | Passed |
| Ruff check/format, mypy, clang-format 23 | Passed; mypy checked 39 files, clang-format checked 61 files |
| Python tooling suite, including actual compiler matcher bypass tests | 75 tests passed; no skips in the LLVM-enabled run |
| Strict GCC native build with clang-tidy and complete CTest suite | 29/29 passed, including compiler-driven architecture and dependency configuration |
| Clang ASan/UBSan first-party native build and complete CTest suite | 28/28 passed with fatal findings |
| Clang TSan/UBSan first-party native build and complete CTest suite | 28/28 passed with fatal findings |
| Dependency-free GCC reference suite | Passed |
| Dependency-free Clang ASan/UBSan reference suite | Passed |
| Committed libFuzzer preset and CTest campaigns | All five targets passed at 60 seconds each; total outer CTest run 324 seconds |

The two local sanitizer builds reused the verified, already-built native dependency prefix while
instrumenting all first-party targets. The extra dependency-configuration test belongs to the
superbuild, explaining 28 versus 29 tests. The committed CI presets perform isolated dependency
builds; these local checks are not a claim that those CI jobs passed.

Regression coverage includes escaped budget lifetimes, moved-from plane state, invalid spans,
overlapping kernels, bounded scheduling and task exceptions, malformed requests, throwing output
streams, malformed/truncated PNGs, codec allocation refusal/refunds, 1/2/4/8-bit grayscale, Adam7,
gamma sample preservation, Unicode paths, existing/dangling destinations and competing publishers.
The full sanitizer run exposed an unnecessary native-to-UTF-8 path round-trip; return metadata now
preserves the admitted UTF-8 spelling before publication, and both sanitizer suites pass that case.

The five-platform release/package matrix, isolated CI sanitizer builds and scheduled AFL++ campaign
must be judged from the published commit's GitHub checks. macOS and Windows were not executed in
this Linux environment. Atomic visibility is still not crash durability, a page budget is not an
RSS ceiling, and the output parent is trusted; those are explicit limits, not hidden guarantees.

Publication uses one feature branch. Temporary pinned-input acquisition and a checksum-verified,
fast-forward-only transfer workflow are preparation steps, not product architecture. The final tree
contains neither temporary workflow nor transfer payload. Preparation commits remain in branch
history; no force push or default-branch write is used. A draft PR is not a statement that CI passed.
