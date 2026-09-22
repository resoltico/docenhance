# Design decisions

The decisions that govern this project, with the reasoning behind them. This is not a history:
when a decision changes, this page changes with it, and the [changelog](../CHANGELOG.md) records
that it moved.

## Language and build system

C++23 with CMake 4.4 and Ninja, using preset schema 12, configure/build/test/package workflows,
target-scoped usage requirements, header file sets and an isolated dependency superbuild. There are
no handwritten makefiles, global include or link directories, or downloads during a normal build.

A pre-standard C++26 baseline, `import std`, or modules across C libraries would add portability
risk without solving a present problem. A second build framework would add machinery, not answers.

## Dependencies are pinned, verified and isolated

Every dependency is pinned to an exact Git release object or archive digest in `deps/lock.json`,
acquired in a separate explicit step, digest-inventoried into a receipt, and re-verified before
every build. A moving branch or tag is never followed, and no host library is silently substituted.
Each upstream project is configured and installed into a per-preset private prefix.

Pins detect change; they are not signed-release verification and not a guarantee against a
compromised upstream. Source and licence review remains a human step, described in
[CONTRIBUTING](../CONTRIBUTING.md).

**OpenCV's module closure is `core`, `flann`, `geometry`, `imgproc`, `photo`.** OpenCV 5 changed the
graph: `photo` requires `geometry`, which requires `flann`. Neural, GPU, GUI, video and external
acceleration downloads are disabled, and a CMake hook fails any attempted nested download. Linking
OpenCV does not authorize `imread` or `imwrite` in algorithm code; dedicated codecs own image I/O.

## The contract is machine-readable, and generated code follows it

`spec/cli-contract.json` and `spec/method-contract.json` are the reviewed authoring sources.
`tools/generate_spec.py` emits the typed C++ option descriptors, the argument and method
documentation, and the CLI fuzzing dictionary. Generated files are never edited by hand, and the
build fails when they are stale.

A JSON catalog is a storage format, not a validator: the typed `EffectiveRecipe` and method variants
remain required before any processing runs, and no option may be silently ignored once its operation
exists. `schemas/command-response.schema.json` declares every field the executable emits, and the
CLI contract test validates real responses against it.

## Architecture is enforced against the compiler, not against source text

C++ has no established equivalent of ArchUnit, ArchUnitNET or Tach. The named candidate
(`blurman-ai/archcheck`) has almost no adoption, `cpp-dependencies` analyses include graphs but
asserts nothing and has slowed, `clang-uml` draws diagrams, and ArchGuard is a multi-language
workbench aimed at larger estates. Include-what-you-use is actively developed but answers a
different question, and clang-tidy's `misc-include-cleaner` already covers it here.

So the rules are built from the clang tooling that is actively developed and already pinned: the
compiler's own dependency output (`-M`) for layer and package reach, `clang-query` AST matchers for
forbidden calls, `throw` and `catch`, and `-fsyntax-only` for header self-containment, all driven
by the compilation database the build already writes. This is stronger than a text-based checker,
which cannot see a dependency reached through another header and cannot tell a call from the same
word in a comment or a string. It costs a configured build, which is why those rules run as a test
inside the build rather than as a source-only pass.

The layer graph itself is stated once, in `spec/architecture.json`, because it was previously
repeated in the build files, the checker and the documentation, and the copies drifted. CMake reads
the manifest to reject a forbidden link while configuring, and every first-party target registers
its layer and files as it is defined, so a new target cannot quietly escape the rules and a layer
nothing builds is an error. That registration is written into the build tree, which lets the
checker hold the declared links against the includes the code actually writes: a target must link
every layer and package its own files name, and must not link one that none of them names.

Bazel's `layering_check` and visibility rules would enforce the same properties in the build system
itself, but changing build systems is a much larger break than this project needs.

## Use cases decide in types; one layer spells them

`de_app` returns an `app::Outcome` — a command, an exit code, the build identity and a typed
payload — and `de_report` turns that into the documented JSON response or into human text. Required
input and output-target rules, and the capability set itself, are decided in `de_app`; the CLI only
translates argument syntax into the typed invocation. The
application layer previously composed both forms itself, which made it half a command line: the
wire format, the help layout and the use-case rules lived in one file, and any second front end
would have had to parse strings or repeat the logic.

The split also gives the JSON response one owner. `schemas/command-response.schema.json` declares
what it contains, `de_report` is the only layer that links a JSON library, and no public header of
any layer names a third-party header, so the types crossing these boundaries stay first-party.

What an option applies to is contract data, not adapter logic. `spec/cli-contract.json` states each
command's usage line and scope symbol, and the generated catalog carries a typed `CommandSet`, so
both the adapter and the renderer ask `option.scope.contains(command)` instead of searching a scope
string for a letter.

## The vocabulary types are the standard ones

Fallible operations return `std::expected<T, Error>`, aliased as `Result<T>`. The project previously
carried a hand-written variant-based equivalent; the standard type is understood by every reader and
every tool, composes through `and_then` and `transform`, and costs nothing to maintain. It is
available on every supported compiler at the supported floors, including Apple clang at macOS 14.

## Memory is budgeted, owned and fallible

A page is large enough that memory is a design decision rather than an implementation detail, so
the vocabulary came before the pipeline. `core::Budget` counts what is outstanding and hands out
`core::Buffer`, which is aligned, move-only and refunds its charge when it dies; `image::Plane` is
that buffer plus a shape whose arithmetic is checked before anything is allocated. Allocation
returns `Result`, because a page too large for the machine is an ordinary answer to a legitimate
request, not an invariant violation — `E_RESOURCE`, exit status 4.

Only `de_core` may write a `new`-expression or call `malloc`; every other layer receives memory a
budget already accounted for, which is why `--memory-mib` can mean something. A rule enforces it on
the real syntax tree, so the shape of the code cannot drift away from the promise in the contract.

A shared allocator behind `operator new` (an arena, a pool) was rejected for now: it would hide
which subsystem holds a page, and the budget answers the question that actually matters. Tiling and
streaming for pages that exceed the budget belong to the pipeline that does not exist yet.

## The schedule is a layer, not a library call

`--threads` is honoured by `de_exec` over the tiles of one page, not by asking a dependency to
parallelise. That is partly forced — OpenCV's `parallel_for_` cannot be pinned on macOS, where it
dispatches through Grand Central Dispatch — and partly deliberate: a library's thread count knows
nothing about the memory budget, and per-worker working sets are what actually decide whether a
large page fits.

Separating the schedule from the algorithm is the same idea Halide made explicit, and it is what
keeps determinism affordable: kernels stay pure functions over regions, the partition is fixed
before the worker count is known, and the scheduler reports the earliest failure rather than the
first one noticed. `exec::Scheduler` hands out indices from an atomic counter, which balances
uniform tiles as well as work stealing would, and starts `std::jthread` workers per call rather
than keeping a pool alive. Per-call threads cost about as much as a thousand rows of a page and
avoid a long-lived pool with its own state; a persistent pool, or a `std::execution` scheduler when
that is standard, can replace the inside of `for_each` without touching a kernel.

Google's Highway, Intel's TBB and a hand-rolled work-stealing pool were all rejected for now: the
first solves a different problem (portable SIMD, which belongs inside a kernel), and the others add
a dependency or a pile of machinery to a project whose kernels do not exist yet.

## The imaging libraries get no memory and no threads of their own

Each dependency is wired so that this project keeps ownership: OpenCV computes into buffers we
already own through `cv::Mat` headers rather than allocating its own, Leptonica allocates through
`setPixMemoryManager`, Little CMS keeps state in a `cmsContext`, and the codecs take explicit
ceilings and limits before decoding anything. `tools/native_probe.cpp` exercises every one of those
hooks as a test, so the claims are checked on each platform instead of being believed.

Two findings from that audit changed the build. OpenCV 5 registers dynamic TBB and OpenMP parallel
backends and can load one from the machine at runtime, which would let an unpinned library decide
this program's threading and memory: `PARALLEL_ENABLE_PLUGINS` is now off, and the audit test
verifies it. OpenCV also logs to **stdout**, where the JSON response goes, so every library is
silenced at startup and the probe's own test fails if a diagnostic appears there.

Parallelism stays this program's decision. `--threads` will schedule pages and tiles in our own
code rather than being forwarded to a library, partly because it is the only way to bound memory
per worker, and partly because it cannot be forwarded: on macOS OpenCV's `parallel_for_` uses Grand
Central Dispatch, which ignores a requested thread count. The probe reports the framework on each
platform rather than pretending the behaviour is uniform.

## Capability claims must be true

The executable advertises only what is implemented, which is currently nothing:
`methods` and `supported_formats` are empty, and every processing command fails with
`E_NOT_IMPLEMENTED` and exit 4 before opening an input. Exit 7 keeps its meaning of unknown
publication state and is never reused. No placeholder returns success by copying its input.

## Strict tooling, no grandfathering

clang-tidy 23, clang-format, Ruff with every rule and mypy in strict mode run as errors. Findings are
fixed rather than suppressed: a suppression must name its rule and carry a registered reason,
file-size limits have no waivers, and no translation unit or target may escape linting. Sanitizer
builds abort on the first report. [Quality gates](quality.md) describes the whole set.

## Fuzzing is engine-agnostic and checks correctness

Harnesses implement `LLVMFuzzerTestOneInput` and use a first-party byte reader, so they compile with
every supported compiler. They check results against independent references — a separate parser, a
regular-expression grammar, exact 128-bit arithmetic, a slow reflection loop, the output contract —
rather than only watching for crashes, because a crash-only harness misses wrong answers.

libFuzzer from the pinned LLVM runs on every pull request; AFL++ 5.03c, built from a pinned release
commit, joins it nightly. Every normal build replays the seed corpora and recorded regressions as
ordinary tests, on every compiler. Google FuzzTest was rejected because it would add GoogleTest,
Abseil and RE2 to a pinned graph that already uses Catch2; honggfuzz has had no release since 2024.

## macOS 14.0 is the minimum

`deps/tools.json` holds the value, CMake applies it, and the package smoke test asserts the shipped
binary declares it. macOS 13 no longer receives Apple security updates; macOS 14 still runs on Macs
from about 2018, including the Intel machines this project builds for.

Raising the floor further would drop users for nothing this code needs — floating-point
`std::from_chars`, for instance, requires macOS 26, and the decimal parser does not use it. GitHub
retires its macOS 14 runners on 2026-11-02, so CI executes the ARM64 binary on macOS 26 while the
compiler enforces the floor. Intel macOS validation remains a release requirement until a pinned
LLVM 23 distribution is available on a hosted Intel runner.

## Documentation describes the present

These documents describe what the project is now. The original 2,370-line target-product blueprint
was removed: a design document written before implementation drifts from the code the moment work
starts, and a public repository should not ship a specification that contradicts its own source.
What survived it lives here, in the [architecture](architecture.md), the reviewed contracts under
`spec/`, and the [roadmap](roadmap.md) for work not yet done. Product requirements for a method
belong in that method's own specification, written when the method is implemented.
