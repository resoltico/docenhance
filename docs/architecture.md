# Architecture

## Authority and composition

The application owns meaning; adapters own effects. The command line parses syntax into
`contract::Invocation`. `de_app` validates it and constructs a private-construction
`ProcessRequest` containing a validated binary/continuous operation and a separate closed
illumination/denoising choices. Only that admitted value can cross `app::Processor`, the processing port.
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
the rendered response. The standalone POSIX scope ignores SIGPIPE and restores its prior disposition, so a closed
response pipe reaches the same output-failure handling as other stream errors. It configures
stdout/stderr as unbuffered before I/O, preventing teardown from retrying failed buffered bytes
after signal restoration. Library calls retain caller stream configuration and signal dispositions. It uses unformatted writes and explicitly flushes only a stream with
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
| `de_methods` | `de_core`, `de_exec`, `de_image` | Pure image operations and typed executable method catalog |
| `de_io` | `de_core`, `de_image` | PNG/JPEG codecs, metadata, hashing and exclusive publication |
| `de_bundle` | `de_core`, `de_image`, `de_methods` | The persistent run record: one written form for the facts an execution produced |
| `de_app` | `de_contract`, `de_core`, `de_image`, `de_methods` | Validated use cases and the explicit processing and verification ports |
| `de_report` | `de_app`, `de_bundle`, `de_contract`, `de_core`, `de_image`, `de_methods` | Renders an outcome as the documented JSON response or as human text |
| `de_cli` | `de_core`, `de_contract`, `de_app`, `de_report` | CLI11 syntax adapter, process streams and exit status |
| `de_host` | `de_app`, `de_bundle`, `de_color`, `de_core`, `de_exec`, `de_image`, `de_io`, `de_methods`, `de_denoise` | Executes admitted requests using codecs, kernels and publication |
| `de_denoise` | `de_core`, `de_image`, `de_methods` | Bounded native NLM execution and resource reservation |
| `docenhance` | `de_cli`, `de_core`, `de_host` | The process entry point and sole production composition root |
| `de_color` | `de_core`, `de_image` | Context-local color interpretation and bounded continuous-tone row conversion |

Only `de_bundle` and `de_report` use nlohmann JSON, `de_cli` uses CLI11, and `de_io` uses libpng in production.
OpenCV core/photo serve D01 through `de_denoise`; other unused imaging packages remain in the
native probe. The bundle field-mapping interface explicitly exposes nlohmann JSON to the report layer under
the manifest's `interface_packages` permission; processing and numerical interfaces use project types. The executable-only `entry` layer
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

Buffer bytes and owning-plane views can be borrowed only from lvalues; converters likewise
refuse temporary raster owners. A WorkRef can name only a live lvalue callable, and synchronous
scheduler tasks remain in scope through all joins. Small shape/parameter accessors return values.
These restrictions reject temporary-owner mistakes but do not extend backing storage lifetime.

An owning `image::Plane` moves without copying samples; a moved-from plane is empty in both storage
and shape. A moved-from surface model is likewise inactive with empty extent and rejects
sample/application access. A borrowed `PlaneView` can only be created through checked extent/stride validation or
from an owning plane. It does not extend storage lifetime. Rows require in-range caller indices.
Kernel entry points reject empty, shape-mismatched and overlapping source/destination storage.
Full backing spans, including padding, are used for conservative overlap checks.

Binarization processing retains a 128 MiB charged-buffer limit shared by its image planes and
libpng/zlib allocations. Complete bundle validation has a separate 1 GiB charged-buffer budget,
which can coexist with live processing buffers; see [processing bundles](bundles.md).
This is **not a process-RSS bound**: small standard-library metadata, the accounting ledger, C stream
buffers, OS thread resources and stacks are outside it. The codec allocator uses a bounded
registry; exhaustion is a resource failure, never permission to allocate elsewhere. Generic
`std::string`/container use is not inaccurately described as zero-allocation.

## PNG codec boundaries

The continuous-tone path is specified in [PNG processing](png-processing.md): immutable bounded
encoded snapshot, checked raw raster, a separate color adapter, and row-wise encode/verify within
the shared publication transaction. Its 1 GiB charged-buffer ceiling does not change the binary
128 MiB ceiling. Public raster and policy types contain no native-library objects.


B02 and B03 accept one regular PNG file, grayscale without transparency, at 1/2/4/8 bits per sample.
Low-bit-depth input expands to 8-bit stored sample values. Gamma metadata does not change threshold
semantics. Color, alpha/transparency and 16-bit input are rejected, not silently converted.
Input is limited to 128 MiB and 40 million pixels; libpng also enforces its dimension and chunk limits.
Output is a single 8-bit grayscale PNG. B03 keeps `sample / 255 <= threshold`; B02 uses the
local population statistics and normalized parameters in [typed binarization](binarization.md).

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
Return-path metadata is allocated before committing. Writer tables are bounded and require
nonnull callbacks before staging starts. Owned files and directories share the native inventory
entry ceiling, and each native path component must be one relative filename. POSIX output paths use `/` as the separator;
a literal backslash remains part of the directory name. Windows accepts native separator spelling.
The reported path preserves the admitted UTF-8 directory spelling on both paths.

Commit is native and atomically non-replacing: Linux `renameat2(RENAME_NOREPLACE)`, macOS
`renamex_np(RENAME_EXCL)`, Windows `MoveFileExW` without replacement or copy flags. There is no
check-then-rename fallback. An existing empty directory, file or symlink cannot be replaced, even
when it appeared after the precheck. Unsupported filesystems fail closed.

Bundle validation stays in `de_host`, composing `de_bundle`'s complete typed record reader with
`de_io`'s native snapshots and PNG observations and `de_color`'s supported ICC checks. The publisher
invokes supplied validation callbacks before commit and when reconciling the destination, without
acquiring a bundle/method layer dependency. The same acceptance contract serves later verification,
staging validation and postcommit observation; none reruns processing.

Cleanup removes only this attempt's known staged files and directories, never recursively. Native
object identities captured at creation refuse cleanup of replaced names. An ambiguous commit retains
staging through destruction until its outcome can be explained. Successful rename remains completed
publication even if later inspection fails; such an inspection failure is `E_OUTPUT_VERIFY` with
`completed`, not false absence or uncertainty about a known effect.
Responses distinguish `not_started`, `not_published`, `completed` and `unknown`. Ambiguous filesystem
errors or failed cleanup produce `E_PUBLICATION_UNKNOWN` (exit 7); callers must inspect paths rather
than retry blindly. Output-stream failure (exit 5 without a complete response) is also not proof
that processing never committed.

This guarantees atomic visibility, **not crash durability**: there is no fsync/directory-sync promise.
The output parent/ancestors must be trusted against hostile replacement. This is not a filesystem
sandbox or a defense against another process with equivalent permissions tampering with owned paths.

## Numerical work and scheduling

Percentile selection validates first and reorders caller-owned scratch in place; it never
creates an input-sized private copy. I01 uses this same nearest-rank primitive over charged
measurement planes. Independent sorted-input and allocation-observation checks cover it.

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
Both modes compile the same first-party targets. The CLI fuzz closure excludes host and codecs. Separate PNG and ICC harnesses exercise the raw representation and native color boundary. PNG/JPEG harnesses use the production byte-span
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
`spec/cli-contract.json` owns the format/mode matrix; its generated descriptor supplies runtime
reporting and both capability schema branches. Response-envelope version comes from the response
schema template. `spec/method-contract.json` owns all implemented method identities, including D01;
the generated descriptors feed typed method values and serialization, with compiled alternatives
checked against the reviewed catalog. Method-option attribution is checked in both directions.
These contracts generate descriptors, reference docs and
fuzz dictionaries. `spec/command-response.schema.json` owns the response template; reviewed method
identities generate its closed alternatives into `schemas/command-response.schema.json`. The run-record template in `spec/run-record.schema.json` shares conversion/illumination report
definitions and reviewed method identities with the response schema during generation. Both schemas
are shipped. The real Draft 2020-12 validator checks executable output, emitted run records and
negative fixtures; the bounded typed record reader additionally validates cross-field relationships. The executable catalog is
built from actual method variant alternatives and must equal the reviewed catalog at compile time.

Required PR jobs cover structural/reference checks, the five-platform native matrix, libFuzzer,
and independent ASan/UBSan and TSan suites. Scheduled campaigns use CTest's authoritative target
registration, including box-mean, rather than a second shell list. Native sanitizer presets instrument first-party code. The isolated PNG fuzz build additionally
instruments pinned libjpeg/libpng/zlib/LCMS and verifies the actual archive symbols; see [fuzzing](fuzzing.md). Documentation records current guarantees
and limits, rather than claiming that every platform or future method already passed.

## Cooperative cancellation

A request-scoped `core::Cancellation` owns its standard stop token and optionally observes a static
interrupt latch. It is execution control, not a method parameter. The scheduler owns the numerical
operation's observation; codecs and publication receive the same control explicitly. See
[cancellation](cancellation.md) for checkpoints, signal restrictions, commit cutoff and test coverage.

## Linear interpretation, illumination and row delivery

`image::LinearSource` exposes bounded, interpreted linear RGB blocks without encoded output
round trips. The color adapter implements this alongside its no-filter row source. `de_methods`
measures and fits I01 without codec/native types or filesystem authority; `de_host` composes the
immutable model, oriented protection mask and downstream quantizer. `de_io` keeps exclusive
ownership of encoding/verification/publication. There is no plugin or generic recipe framework.

`ProcessFailure` is an application result envelope with a core error and optional typed stage
observations. The processing port has closed binary and continuous success alternatives. Continuous success
carries conversion, illumination and denoising observations together; the application still checks
verification and agreement with the admitted request. The report consumes that same continuous
result rather than a second copied representation. Publication observes conversion through a
typed borrowed converter, with no independent callback/void-pointer pair. Successful continuous
results require complete matching illumination diagnostics;
`complete` describes the numerical stage, not a later publication outcome. The generated schema
and executable catalog distinguish method families. [Illumination](illumination.md) owns the exact
mathematics, resource phases, separate design QA and verification obligations.

## Identified PNG and JPEG sources

The continuous host calls the format-neutral source adapter. One bounded immutable encoded snapshot
is hashed and dispatched by signature, never by extension or decoder retries. `image::RasterMetadata`
contains shared ICC/orientation/resolution observations with closed PNG/JPEG container declarations;
no native codec types escape `io`. Common EXIF extraction retains its bounded IFD0 contract. PNG's
color/physical precedence and B02/B03 stored samples retain their existing semantics.

The JPEG adapter promotes the locked static dependency into `de_io`, with the manifest owning that
external-package permission. A bounded framing/metadata scan and charged native memory manager
precede full-resolution native decoding. Native jumps stay inside owner-free wrappers, while the
request's buffers and contexts outlive the frame. JPEG has no separate host, publisher or numerical
method. New records and responses carry typed source observations with explicit format/wire versions;
only the current closed bundle format is supported. See [JPEG](jpeg-processing.md).

## Prepared luminance denoising

`de_denoise` wraps only the pinned CV_16U/L1 operation, with bounded tile extents, native scratch
reservation and contained exceptions. Completed reservation/combined-charge observations are
returned by the native owner while its lease is live; orchestration does not predict them. `de_methods` owns closed NLM settings, scalar correction and
resource estimates without native types. The host composes linear interpretation, frozen I01,
prepared D01 and final integer rows. Completed earlier-stage observations survive downstream
failures; publication remains one complete bundle transaction. The prepared scalar planes and
original source/model outlive encoding and verification. See [denoising](denoising.md).

## Verification observes execution and delivered bytes

The native suite reconciles executable case discovery and actual translation-unit compilation with
fresh per-process results. Catch XML establishes nonzero assertion work without hidden skips or
expected failures; CTest JUnit establishes complete process execution. CLI scripts and fuzz corpus
replays have independent source/manifest registration checks. Strict tooling discovery rejects
empty modules and skipped cases. Fuzz campaigns additionally check declared duration, selected
engine, exact binary/manifest identities and native work reports.

Relocated package inspection compares the tested executable, reviewed schemas/specs and regenerated
locked-source license inventory byte-for-byte, checks OS-only native imports and exercises every
advertised method. A source SPDX inventory remains explicitly separate from binary composition or
legal clearance. Source release archives snapshot one resolved committed Git tree; local workspace
changes cannot silently enter release contents. See [the verification/artifact audit](verification-artifacts-audit.md).
