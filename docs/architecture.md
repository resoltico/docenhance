# Architecture

## Premises

These hold everywhere in the code. A change that breaks one is a change of design, not an
implementation detail.

1. **Processing authority never sits in the adapter, and presentation never sits in a use case.**
   The command line parses syntax and writes bytes. What an option *means*, and whether a
   combination is legal, belongs to the layers below it, in typed form; how an outcome is spelled
   for a person or a program belongs to `de_report`, above them. An outcome crosses that boundary
   as types, never as text.
2. **Lower layers know nothing of higher ones**, and nothing outside the process. Numeric and pure
   layers do not read files, the environment, the command line or the process streams; they take
   values and return values.
3. **Failure is a value, not an exception.** Every operation that can fail returns
   `Result<T>` — `std::expected<T, Error>` — and every such function is `[[nodiscard]]`. Exceptions
   cross exactly one boundary, the CLI11 adapter, which converts them to the same `Error`.
4. **Capabilities are reported, never assumed.** `methods` and `supported_formats` list what is
   implemented, which is currently nothing. A linked library is not a capability, and a placeholder
   that returns success is a defect, not a stub.
5. **Numbers are deterministic.** No fast-math, no locale-dependent parsing, fixed rounding
   conventions, and results that do not depend on how work was scheduled.
6. **The build declares what it uses, and uses what it declares.** A target links a dependency
   exactly when its own files include it, so the dependency graph is evidence rather than
   decoration.

## Layers

`spec/architecture.json` is the reviewed statement of this table: which layers exist, which layers
and third-party packages each may use, and what each is for. Nothing repeats it — CMake reads it to
enforce link edges, the checker reads it to enforce includes and calls, and this table is checked
against it.

| Target | May use | Responsibility |
|---|---|---|
| `de_core` | — | Errors, exit codes and the `Result` vocabulary |
| `de_contract` | `de_core` | The command vocabulary, generated option descriptors and strict value parsers |
| `de_exec` | `de_core` | The schedule: how many workers run a page's independent work items |
| `de_image` | `de_core` | Numerical and colour primitives; later, the owning image representation |
| `de_methods` | `de_core`, `de_exec`, `de_image` | Pure image operations; later, the catalog of complete methods |
| `de_io` | `de_image` | Later: codecs, metadata, hashing and exclusive publication |
| `de_app` | `de_contract`, `de_core`, `de_exec`, `de_io`, `de_methods` | Use cases: what an invocation means, decided in types |
| `de_report` | `de_core`, `de_contract`, `de_app` | Renders an outcome as the documented JSON response or as human text |
| `de_cli` | `de_core`, `de_contract`, `de_app`, `de_report` | CLI11 syntax adapter, process streams and exit status |
| `docenhance` | `de_cli`, `de_core` | The executable: `main`, and nothing else |

A layer may also reach, through the layers it uses, whatever those may reach: `de_methods` sees
`de_core` through `de_image`. What it may *name itself* is exactly the row above.

Third-party packages are allowed the same way, and only where they are used: `nlohmann_json` in
`de_report`, `CLI11` in `de_cli`, nothing anywhere else. No public header may name one, so a
third-party type never appears in what a layer hands to its callers. The codecs, OpenCV, Leptonica and Little CMS
are declared packages with no layer yet allowed to use them; they become `de_io` and `de_methods`
dependencies when the code that needs them exists. `tools/native_probe.cpp` proves meanwhile that
they build, link and run — it belongs to no layer, and nothing in `src/` may depend on it.

Public headers are declared through CMake header file sets, sources are listed explicitly, and
third-party include directories arrive through private imported targets rather than global
settings. Warnings and sanitizer flags are target-scoped and never rewrite an upstream project's
policy.

## How the rules are enforced

Five mechanisms, none of which reads source code as text for anything but the include directives
first-party files write:

- **Link edges, at configure time.** `cmake/ArchitectureChecks.cmake` reads the manifest and fails
  the configure step when a target links a layer or a package its layer does not declare. Every
  first-party target registers its layer and its files as it is defined, so a new target cannot
  quietly escape the rules, and a layer the manifest declares but nothing builds is an error.
- **Links against use.** The registration is written to the build tree, and the checker holds it
  against what the code includes: a target must link every layer and package its own files name,
  and must not link one that none of them names. Fictional dependencies and missing ones both fail.
- **The real include graph.** `tools/check_architecture.py` asks the compiler itself (`-M`) for the
  transitive headers of every translation unit, so a layer or a package reached through three other
  headers counts exactly as much as a direct include. A third-party header belonging to no declared
  package fails too.
- **The real abstract syntax tree.** The same tool runs `clang-query` matchers over each
  translation unit, so a banned call — `getenv`, `system`, `popen`, `imread` — is found through any
  alias, macro or namespace qualification. The same pass enforces premise 3 — no first-party code
  contains a `throw`, and only the CLI adapter contains a `catch` — that only `de_core` writes a
  `new`-expression, so every other layer takes memory a budget accounted for, and that a layer's
  files open only `docenhance::<layer>`, so the directory, the target and the namespace cannot
  drift apart.
- **Headers that stand alone.** Every public header is compiled on its own, and twice in the same
  translation unit, so no header depends on what its includer happened to include first, and no
  public header names a third-party one.

The last four need a configured build and GNU-style compiler options, so they run as the
`architecture` test inside it — on MSVC, where the compilation database offers neither, only the
source rules run and the other platforms carry them. The layout, include-directive and
documentation rules need no build and run in `tools/check_all.py` and in CI as well.
Together they are this project's answer to ArchUnit; [design decisions](decisions.md) explains why
that answer is built from clang tooling rather than from a dedicated library.

## Memory, and pages that do not fit

A page is large. Forty megapixels of 32-bit samples is 160 MiB for one channel, and a pipeline
holds several at once, so memory is a first-class design concern rather than an implementation
detail. Four rules follow from that, and the first three are enforced.

1. **Running out of memory is a value, not an exception.** `core::Budget::allocate` returns
   `Result<Buffer>`; a refusal carries `E_RESOURCE` and exit status 4. Nothing in the processing
   layers allocates through a throwing path, so a page too large for the machine is an ordinary
   answer, not a crash.
2. **Every image allocation is charged.** `--memory-mib` is a working-allocation budget, and it is
   a real object: `core::Budget` counts what is outstanding, atomically, so parallel work can share
   one ceiling. A `Buffer` refunds its charge when it dies, and a `Plane` is a `Buffer` plus a
   checked shape.
3. **Only `de_core` allocates.** The manifest marks it `may_allocate`; every other layer receives
   memory that a budget already accounted for, and `clang-query` rejects a `new`-expression or a
   `malloc` anywhere else. A plane is move-only, so no operation copies a page by accident, and
   algorithms take `PlaneView`, which owns nothing.
4. **Sizes are checked before they are allocated.** `plane_shape` and `plane_bytes` return
   `Result`: a row of 4,294,967,295 samples has no representable size, and saying so is much better
   than overflowing into a small allocation and writing past it. Rows are padded to the buffer
   alignment so every row starts aligned.

## What a kernel is

`methods::box_mean` is the first one, and it sets the shape for the rest. A kernel is a pure
function of the samples it is given: it takes `PlaneView`s, owns nothing, allocates only through
the budget it was handed, returns `Result`, and never learns where its input came from or where
the answer is going.

It is a *primitive*, not a method. `methods` reports an empty catalog and will keep doing so until
a complete, specified method exists; a box mean is the substrate that several of them share — the
blurred term of unsharp masking (S01), the local mean of Sauvola thresholding (B02), the
illumination surfaces of the shadow methods (I01, I02) — and sharing it is the reason to write it
first.

Three properties are checked rather than asserted:

- **It agrees with its own definition.** Every sample of a filtered plane is compared against the
  window summed directly, in the reference suite and in a fuzz harness that generates planes,
  radii and worker counts. A sliding window is an optimisation of a definition, so the definition
  is what it is tested against.
- **It is bitwise reproducible.** The same input gives the same bits at one worker and at eight,
  which the tests and the fuzzer both assert. That is a property of the partition, not of luck:
  tiles are a fixed size, so the order the sums accumulate in cannot change with `--threads`.
- **Its failures are values.** A radius of zero, a destination of the wrong size, overlapping
  source/destination storage and a budget with no room for the intermediate plane are all returned,
  not thrown. A large radius is reduced through the reflected period during initialization, so it
  cannot turn setup into radius-proportional work.

## The schedule is separate from the algorithm

`--threads` promises internal numerical parallelism over one page, and it cannot be forwarded to a
library: OpenCV's own `parallel_for_` ignores a requested thread count on macOS. So the schedule is
this project's, in `de_exec`, and it is kept apart from the work it runs.

- **A kernel is a pure function of the region it is given.** It does not know whether it is one of
  one or one of sixty-four, and it may touch only the data its own index owns. `de_exec` knows
  nothing about images in return: it runs indices, not tiles.
- **The partition is chosen before the workers are.** A page is divided the same way whatever the
  worker count, so the answer never depends on how the work was spread or on who finished first.
  One worker is not a different program, and `exec::Scheduler` runs it on the calling thread.
- **The failure reported is the earliest one.** When several items fail, the one with the lowest
  index wins, which is the failure a sequential run would have reported.
- **The worker count is a function of the budget.** `exec::Concurrency::resolve` takes the request,
  what the machine reports and what one worker's working set costs, and reduces the count until the
  memory fits; a budget too small for a single worker is refused rather than silently shrunk. A
  thread count that ignores memory is how a large page becomes a swap storm.
- **The schedule allocates nothing.** Workers and their result slots are fixed-size, bounded by the
  contract's own maximum of 64, so a schedule never competes with the page for the budget.

Only `de_exec` may include `<thread>`, `<future>`, `<execution>` or their neighbours; every other
layer is a pure function of its inputs, and the rule is checked. A persistent pool and a
`std::execution` backend are both possible later without touching a kernel: that is the point of
keeping the two apart.

## Third-party libraries own no memory and no threads of ours

The imaging libraries are configured and wired so that this project keeps both:

- **OpenCV** operates on memory we already own. A `cv::Mat` can be a header over a `Plane`'s buffer
  — no allocation, no reference counting, no hidden copy — and operations write into a destination
  we supply. That, not a custom allocator, is how a page reaches OpenCV.
- **Leptonica** allocates through `setPixMemoryManager`, so `Pix` data comes from us.
- **Little CMS** keeps its state in a `cmsContext`, so a run holds its own transforms rather than
  sharing global state.
- **libjpeg** takes a memory ceiling (`max_memory_to_use`), **libpng** takes image limits
  (`png_set_user_limits`), and **libtiff** reports through a handler we install rather than writing
  to stderr. These are decompression-bomb defences, and they are set before anything is decoded.
- **Parallelism is ours, not the libraries'.** OpenCV 5 can load a TBB or OpenMP parallel backend
  from the machine at runtime; `PARALLEL_ENABLE_PLUGINS` is off, so it cannot. The `--threads`
  contract is this program's scheduling decision over pages and tiles, not a request forwarded to a
  library. On macOS OpenCV's own `parallel_for_` uses Grand Central Dispatch, which ignores a
  requested thread count — a platform difference the probe reports rather than hides.

None of this is a claim: `tools/native_probe.cpp` exercises each hook and runs as the
`dependency-native-link` test in every build that has the native dependencies.

## Contract ownership

The top-level `project(... VERSION ...)` command owns the version. `deps/lock.json` owns source
identities, `deps/features.json` the requested upstream feature values, and `deps/tools.json` the
tool pins. `spec/architecture.json` owns the layer graph. `spec/cli-contract.json` and
`spec/method-contract.json` are the reviewed authoring sources for arguments and planned methods; `tools/generate_spec.py` turns them into typed C++
descriptors, documentation and the fuzzing dictionary, and the build fails when those are stale.
`schemas/command-response.schema.json` owns the shape of every JSON response.

## How a command becomes a response

Four steps, each owned by one layer, and nothing does two of them:

1. `de_cli` turns `argv` into a typed `contract::Invocation`, rejecting only what the grammar forbids.
   Which options a command accepts is a property of the contract — `option.scope.contains(command)` —
   not a scope string the adapter interprets.
2. `de_app` decides what that invocation *means* — including required inputs, output targets, and
   the implemented capability set — and returns an `app::Outcome`: the command, the exit code, the
   build identity, and a typed payload (`Help`, `Version`, `Methods`, `Failure`).
   It composes no text and knows no output format.
3. `de_report` renders that outcome, as the JSON response
   (`schemas/command-response.schema.json`) or as human text. It is the only owner of the wire
   format, and the only layer that links a JSON library.
4. `de_cli` writes the rendered bytes to the streams it was given and returns the exit code.

A second front end — a library API, a batch runner — reuses steps 2 and 3 unchanged. That is the
reason presentation is not part of the use case.

## What the executable does today

It dispatches help, version and capability discovery, and refuses every processing command with
`E_NOT_IMPLEMENTED` before opening an input. `de_methods` and `de_io` return empty capability
lists rather than plausible-looking names. The numeric primitives implement exact scalar formulas
with stated boundary conventions: sRGB encode and decode, relative luminance, neutral-axis luminance
transport, nearest-rank percentiles and `REFLECT_101`. They are building blocks, not a colour-managed
codec and not an enhancement algorithm.

There is deliberately no plugin framework, service layer, network client, dependency-injection
container, interactive terminal or database. New machinery has to solve a demonstrated problem.
