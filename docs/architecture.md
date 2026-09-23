# Architecture

## Authority and composition

The application owns meaning; adapters own effects. The command line parses syntax into
`contract::Invocation`. `de_app` validates it and constructs a private-construction
`ProcessRequest`. Only that admitted value can cross `app::Processor`, the processing port.
`de_host` implements the port with the image/codec pipeline. The `entry` layer is the only
production composition root: it supplies the concrete host to the CLI. On Windows it converts
wide CRT arguments to UTF-8 before parsing; filesystem adapters use native wide paths.
The CLI rejects malformed UTF-8 before handing any token to CLI11. Direct application admission
independently validates input and output paths. Validation preserves byte spelling: no Unicode
normalization or replacement is performed, and embedded NUL remains invalid in a path.

Application tests and CLI fuzzers supply a deterministic non-I/O processor. There is no runtime
flag that substitutes it. Help, version and methods discovery never call the processing port.
Adding another complete method extends admission and execution deliberately; it does not require
a plugin loader, service locator or dependency-injection framework.

Outcomes carry a typed payload and build identity. The exit code is derived from the payload,
not separately writable state. `de_report` alone chooses JSON/text spelling. The CLI alone writes
the rendered response. It uses unformatted writes and explicitly flushes only a stream with
response content, then checks that stream's state. Caller width/fill settings cannot alter the
serialized bytes. The adapter does not directly access an unselected stream; standard stream
ties and exception masks remain in effect. A write,
flush or rendering failure after execution returns an output failure and never repeats processing
or attempts a second, potentially misleading response. This is stream delivery, not proof that a
consumer read the response or that a file is crash-durable. JSON `exit_code` describes the rendered
command outcome. A later delivery failure can make the process exit 5 even after a complete success
object was emitted; callers inspect both statuses, and must not replay processing based on exit 5.

Before crossing the processing port, the application prepares a complete unknown-publication
outcome. An escaping processor exception, including allocation failure or a non-standard exception,
returns that outcome by a statically checked non-throwing move. It cannot imply `not_started` after
an unreported effect. A processor's returned `Result` retains its explicit publication state.
Exceptions during admission still occur before any processing effect.

JSON serialization is strict for identity fields, including the reported output path. A malformed
identity fails rendering; it is never rewritten with replacement characters. An invalid diagnostic
message alone is rendered as an explanatory ASCII fallback while retaining its error code and
publication state.

## Layers

`spec/architecture.json` is authoritative. CMake reads it for direct link edges; the architecture
checker reads it for include closure, API restrictions and this mechanically checked table.

| Target | May use | Responsibility |
|---|---|---|
| `de_core` | — | Errors, owned allocation budgets and the Result vocabulary |
| `de_contract` | `de_core` | The command vocabulary, generated option descriptors and strict value parsers |
| `de_exec` | `de_core` | The schedule: how many workers run a page's independent work items |
| `de_image` | `de_core` | Checked owning planes, borrowed views and numerical primitives |
| `de_methods` | `de_core`, `de_exec`, `de_image` | Image operations and the catalog of implemented methods |
| `de_io` | `de_core`, `de_image` | Codecs, metadata, hashing and exclusive publication |
| `de_app` | `de_contract`, `de_core`, `de_methods` | Validated use cases and the explicit processing port |
| `de_report` | `de_core`, `de_contract`, `de_app` | Renders an outcome as the documented JSON response or as human text |
| `de_cli` | `de_core`, `de_contract`, `de_app`, `de_report` | CLI11 syntax adapter, process streams and exit status |
| `de_host` | `de_app`, `de_core`, `de_image`, `de_io`, `de_methods` | Executes admitted requests using codecs, kernels and publication |
| `docenhance` | `de_cli`, `de_core`, `de_host` | The process entry point and sole production composition root |

Only `de_report` uses nlohmann JSON, `de_cli` uses CLI11, and `de_io` uses libpng in production.
Other pinned imaging libraries remain isolated in the native dependency probe until a real method
needs them. Public headers never expose third-party types. The executable-only `entry` layer
explicitly declares that it has no public header directory; it is not a fake reusable library.

## Values, ownership and budgets

`core::Result<T>` is `std::expected<T, Error>`. Expected failures are returned as values.
Foreign-library callbacks, scheduler workers, filesystem publication, the application processing
port and the process/CLI boundary contain exceptions at their respective authority boundaries. Only layers explicitly authorized in the manifest can
catch; numeric kernels cannot throw or catch. A worker exception becomes a caller-visible resource
or invariant failure after all started workers join, not `std::terminate`.

`core::Buffer` is move-only, aligned and charged to a finite `Budget`. Each live buffer holds a
reference to the accounting ledger, so it can safely outlive the `Budget` object. Only the last
reference destroys the ledger. Allocation/refund counters are atomic. Calling methods on a budget
while destroying that same object is still invalid caller behavior.

An owning `image::Plane` moves without copying samples; a moved-from plane is empty in both storage
and shape. A borrowed `PlaneView` can only be created through checked extent/stride validation or
from an owning plane. It does not extend storage lifetime. Rows require in-range caller indices.
Kernel entry points reject empty, shape-mismatched and overlapping source/destination storage.
Full backing spans, including padding, are used for conservative overlap checks.

The B03 host has a 128 MiB charged-buffer limit shared by image planes and libpng/zlib allocations.
This is **not a process-RSS bound**: small standard-library metadata, the accounting ledger, C stream
buffers, OS thread resources and stacks are outside it. The codec allocator uses a bounded
registry; exhaustion is a resource failure, never permission to allocate elsewhere. Generic
`std::string`/container use is not inaccurately described as zero-allocation.

## PNG codec boundary

B03 accepts one regular PNG file, grayscale without transparency, at 1/2/4/8 bits per sample.
Low-bit-depth input expands to 8-bit stored sample values. Gamma metadata does not change threshold
semantics. Color, alpha/transparency and 16-bit input are rejected, not silently converted.
Input is limited to 128 MiB and 40 million pixels; libpng also enforces its dimension and chunk limits.
Output is a single 8-bit grayscale PNG with black iff `sample / 255 <= threshold`.

The codec uses libpng's custom memory callbacks charged to the caller's budget. Error callbacks
jump only into dedicated C-facing frames with trivial automatic state. All owning C++ objects,
allocation slots, diagnostic storage and file handles live outside those frames. No `longjmp`
crosses a C++ owner. libpng's creation API contains its own jump frame, and `png_create_info_struct`
uses its non-raising allocation API. The exact locked upstream implementation is part of this audit.
Malformed/truncated data, strict CRC failures and refused allocations are regression-tested.

## Exclusive publication

The output must be a new directory whose parent already exists. Prechecking the target improves
errors but is not the correctness boundary. A bounded search exclusively creates a private sibling
staging directory; occupied paths are never adopted. The encoder closes the staged PNG before commit.
Return-path metadata is allocated before committing. POSIX output paths use `/` as the separator;
a literal backslash remains part of the directory name. Windows accepts native separator spelling.
The reported path preserves the admitted UTF-8 directory spelling on both paths.

Commit is native and atomically non-replacing: Linux `renameat2(RENAME_NOREPLACE)`, macOS
`renamex_np(RENAME_EXCL)`, Windows `MoveFileExW` without replacement or copy flags. There is no
check-then-rename fallback. An existing empty directory, file or symlink cannot be replaced, even
when it appeared after the precheck. Unsupported filesystems fail closed.

Cleanup removes only this attempt's known staged file and directory, never recursively.
Responses distinguish `not_started`, `not_published`, `completed` and `unknown`. Ambiguous filesystem
errors or failed cleanup produce `E_PUBLICATION_UNKNOWN` (exit 7); callers must inspect paths rather
than retry blindly. Output-stream failure (exit 5 without a complete response) is also not proof
that processing never committed.

This guarantees atomic visibility, **not crash durability**: there is no fsync/directory-sync promise.
The output parent/ancestors must be trusted against hostile replacement. This is not a filesystem
sandbox or a defense against another process with equivalent permissions tampering with owned paths.

## Numerical work and scheduling

Scalar definitions remain deterministic: no fast-math, locale-independent decimal parsing, explicit
rounding and border conventions. Fixed-threshold results are checked exhaustively for 8-bit samples.
The shared box-mean kernel is checked against direct window sums and across worker counts. It is a
primitive, not a separately advertised complete method.

`de_exec` schedules indexed work, not images. The partition is chosen before the worker count.
It bounds workers by the requested concurrency and working-set budget; zero reported machine
concurrency has an explicit fallback. Counters cannot wrap at `SIZE_MAX`. One worker runs inline;
multiple workers use scoped `std::jthread` ownership. Every started worker joins, including after
partial thread-creation failure. The lowest-index task failure is reported; thread-launch failure
has resource-failure priority. The fixed result slots do not make OS thread creation allocation-free.
The present public CLI does not expose `--threads`; this remains an internal kernel API.

## Enforced boundaries, not just a diagram

CMake rejects undeclared direct layer/package links. Every production target registers its files,
headers and links. Native builds must contain every declared layer. An isolated fuzz build must
contain the complete transitive closure of its named root, not a manually duplicated source list.
Both modes compile the same first-party targets. The CLI fuzz closure excludes host and codecs. Separate PNG harnesses use the shared byte-span
decoder; only those targets link the codec layer.

Source checks validate manifest shape, duplicate targets, directory ownership, direct includes and
this table. Compiler-backed checks read the real include graph, require public headers to compile
alone/twice, and compare actual includes with registered links. `clang-query` checks forbidden calls,
throw/catch boundaries, explicit allocator calls as well as new/delete expressions, and namespaces.
An unparsed translation unit or unanswered matcher is a failure, never an empty success result.

Strict compiler warnings, clang-tidy, file-size limits, formatting, Ruff, mypy and reviewed local
suppression identities remain enforced. Suppressions name a specific unavoidable boundary and its
reason; no blanket waiver or historical-grandfathering path is introduced.

## Single sources of truth and verification

The top-level CMake project owns versioning. `deps/lock.json` owns source identities;
`deps/features.json` owns upstream feature policy; `deps/tools.json` owns developer-tool versions.
`spec/cli-contract.json` and `spec/method-contract.json` generate descriptors, reference docs and
fuzz dictionaries. `schemas/command-response.schema.json` owns closed response payloads, validated
by the real Draft 2020-12 implementation against executable output and negative fixtures.

Required PR jobs cover structural/reference checks, the five-platform native matrix, libFuzzer,
and independent ASan/UBSan and TSan suites. Scheduled campaigns use CTest's authoritative target
registration, including box-mean, rather than a second shell list. Native sanitizer presets instrument first-party code. The isolated PNG fuzz build additionally
instruments pinned libpng/zlib and verifies the actual archive symbols; see [fuzzing](fuzzing.md). Documentation records current guarantees
and limits, rather than claiming that every platform or future method already passed.
