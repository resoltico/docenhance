# Architecture

## Authority and composition

The application owns meaning; adapters own effects. The command line parses syntax into
`app::Invocation` through an owned tokenizer and generated option identities, retaining raw presence. `de_app` validates it and constructs a private-construction
`ProcessRequest` owning one closed `methods::Plan`, resource paths and protection selection.
All output modes share that fixed photometric plan. Only an admitted request can cross
`app::Processor`, the processing port.
`de_host` implements the port with the image/codec pipeline. The `entry` layer is the only
production composition root: it supplies the concrete host to the CLI. On Windows it converts
wide CRT arguments to UTF-8 before parsing; filesystem adapters use native wide paths.
The CLI rejects malformed UTF-8 before assigning any token role. Direct application admission
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

The application transfers returned first-party processing values with their explicit effects and
completed Facts. Actual methods, record serialization, native snapshots and artifact comparison own
validation; retained wire Fields/context validate received response values. Read-only verification
transfers its reported Error or observation and contains exceptions with `not_started` publication.
Before crossing the processing port, the application prepares a complete unknown-publication
outcome. An escaping processor exception, including allocation failure or a non-standard exception,
returns that outcome by a statically checked non-throwing move. It cannot imply `not_started` after
an unreported effect. A processor's returned `Result` retains its explicit publication state.
Exceptions during admission still occur before any processing effect.

Error code/message and publication are independent observations; no general code/publication table
revalidates a first-party return. Wire errors retain the closed code and actual Error-to-exit mapping.
Read-only failure responses have `not_started` and no processing work. The explicit cancellation
invariant admits `E_CANCELLED` only with `not_started` or `not_published`; completed failure has no
failed family, and failed-family ordering excludes that family and later completed Facts.

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
| `de_exec` | `de_core` | The schedule: how many workers run a page's independent work items |
| `de_image` | `de_core` | Checked owning planes, borrowed views and numerical primitives |
| `de_fft` | `de_core`, `de_exec`, `de_image` | Charged binary64 block Fourier transforms and foreign exception containment |
| `de_methods` | `de_core`, `de_exec`, `de_image`, `de_fft` | Pure image operations and typed executable method catalog |
| `de_app` | `de_core`, `de_image`, `de_methods`, `de_record` | Command vocabulary, generated option descriptors, strict value parsers, validated use cases and execution ports |
| `de_report` | `de_app`, `de_record`, `de_core`, `de_image`, `de_methods` | Renders an outcome as the documented JSON response or as human text |
| `de_cli` | `de_core`, `de_app`, `de_report` | Owned argument tokenizer, process streams and exit status |
| `de_pipeline` | `de_core`, `de_exec`, `de_image`, `de_methods`, `de_codec`, `de_color` | Pure owned interpretation, fixed eager methods and verified encoded outputs |
| `de_host` | `de_core`, `de_exec`, `de_image`, `de_methods`, `de_app`, `de_record`, `de_fs`, `de_codec`, `de_pipeline`, `de_color` | Executes admitted requests using codecs, kernels and publication |
| `docenhance` | `de_cli`, `de_core`, `de_host` | The process entry point and sole production composition root |
| `de_color` | `de_core`, `de_image` | Context-local color interpretation and bounded continuous-tone row conversion |
| `de_record` | `de_core`, `de_image`, `de_methods` | The persistent run record: one written form for the facts an execution produced |
| `de_codec` | `de_core`, `de_image` | PNG/JPEG/TIFF byte codecs, metadata and streaming PNG comparison |
| `de_fs` | `de_core` | Bounded immutable snapshots, digest, owned byte staging, exclusive publication and bundle reading |

Only `de_record` and `de_report` use nlohmann JSON; `de_codec` uses libpng in production.
The eager production pipeline uses first-party NLM-L1 and the bounded pocketfft adapter for R01.
Native calibration, source-security and package closure checks remain independent requirements. The record field-mapping interface explicitly exposes nlohmann JSON to the report layer under
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

Buffer bytes and owning-plane views can be borrowed only from lvalues; interpreters likewise
refuse temporary raster owners. A WorkRef can name only a live lvalue callable, and synchronous
scheduler tasks remain in scope through all joins. Small shape/parameter accessors return values. Decoded/output sample depth and EXIF orientation
are admitted values with private constructors, so consumers cannot carry an unsupported precision
or silently reinterpret an invalid orientation. Encoded PNG depth is a separate container fact.
Raster metadata borrows likewise require lvalues, and row adapters check their range before
borrowing. Moved requests consume the source path; native ports reject unready values before I/O.
These restrictions reject temporary-owner mistakes but do not extend backing storage lifetime.

An owning `image::Plane` moves without copying samples; a moved-from plane is empty in both storage
and shape. A borrowed `PlaneView` can only be created through checked extent/stride validation or
from an owning plane. It does not extend storage lifetime. Rows require in-range caller indices.
Kernel entry points reject empty, shape-mismatched and overlapping source/destination storage.
Full backing spans, including padding, are used for conservative overlap checks.

Every output mode uses the selected charged-buffer processing limit shared by image planes, codec
allocations, native reservations and method workspaces; execution controls select that limit. Complete bundle validation has a separate 1 GiB charged-buffer budget,
which can coexist with live processing buffers; see [processing bundles](record.md).
This is **not a process-RSS bound**: small standard-library metadata, the accounting ledger, C stream
buffers, OS thread resources and stacks are outside it. The codec allocator uses a bounded
registry; exhaustion is a resource failure, never permission to allocate elsewhere. Generic
`std::string`/container use is not inaccurately described as zero-allocation. Charged payload storage
begins uninitialized; consumers initialize input samples before reading and write every output
sample. Padding is excluded from pixel semantics. Codec row merging and progressive coefficients
retain explicit bounded zero-initialization and cancellation checkpoints.

## PNG codec boundaries

[PNG processing](formats/png.md) defines immutable bounded encoded snapshots, checked raw
rasters, color interpretation and independent output comparison. Public raster/policy values contain
no native objects. PNG/JPEG/TIFF use the same source dispatch and fixed pipeline; their individual
codec contracts retain their exact representation admission and refusal rules.

Binary output consumes the final luminance through one terminal Q16 quantization and B01/B02/B03
classification. It stores logical black0/white1 directly as physical grayscale PNG1, with zero unused
row bits. B01 retains 4,096 bins and deterministic earliest ties; B02 retains exact reflected integer
moments; B03 retains its equality rule. Every pixel is classified, including protected photometric
samples. General input PNG1 decoding does not impose canonical-output padding rules.

The codec uses libpng's custom memory callbacks charged to the caller's budget. Error callbacks
jump only into dedicated C-facing frames with trivial automatic state. All owning C++ objects,
allocation slots, diagnostic storage and file handles live outside those frames. No `longjmp`
crosses a C++ owner. libpng's creation API contains its own jump frame, and `png_create_info_struct`
uses its non-raising allocation API. The exact locked upstream implementation is part of this audit.
Malformed/truncated data, strict CRC failures and refused allocations are regression-tested.

## Exclusive publication

Filesystem effects bind relative paths before callbacks; admitted/report spelling remains intact.
[Bundle ownership](record.md) requires native object leases,
opened-handle admission and deterministic namespace mutation cases. Staging retains at most one
root and 64 entry leases. Native publication ownership stays in `de_fs`; one complete host validator
serves before and after commit, with no separate partial-record reopen or observation enum.

The output must be a new directory whose parent already exists. Prechecking the target improves
errors but is not the correctness boundary. A bounded search exclusively creates a private sibling
staging directory; occupied paths are never adopted. The host has already encoded and independently
compared the result before staging. Return-path metadata and the actual serialized record are prepared
before commit. The bounded file table contains owned byte spans, not arbitrary encoding callbacks;
one host validation callback checks the complete staged/committed bundle. Owned files and directories share the native inventory
entry ceiling, and each native path component must be one relative filename. POSIX output paths use `/` as the separator;
a literal backslash remains part of the directory name. Windows accepts native separator spelling.
The reported path preserves the admitted UTF-8 directory spelling on both paths.

Commit is native and atomically non-replacing: Linux `renameat2(RENAME_NOREPLACE)`, macOS
`renamex_np(RENAME_EXCL)`, Windows `MoveFileExW` without replacement or copy flags. There is no
check-then-rename fallback. An existing empty directory, file or symlink cannot be replaced, even
when it appeared after the precheck. Unsupported filesystems fail closed.

Bundle validation stays in `de_host`, composing `de_record`'s complete typed record reader with
`de_fs`'s native snapshots and `de_codec`'s PNG observations and `de_color`'s supported ICC checks. The publisher
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

## Prepared bundle publication

The prepared result, optional mask/PSF assets and finally `run.json` are written into a staging
directory this invocation owns. The complete directory is committed with one exclusive rename. A failure before commit prevents publication. A successful rename establishes completed publication;
a later integrity failure does not undo that fact. Cleanup removes only what the
invocation created, never a foreign entry and never a directory it did not make.
Effect paths are anchored before callbacks without lexical normalization; response spelling retains
the admitted directory text. Windows drive-relative publication paths such as `C:result` are refused;
use a fully qualified drive path or an ordinary path relative to the operation's initial directory.
POSIX staging directories and files are created with owner-only modes, not chmodded after creation.

The host encodes and independently compares the intended output before staging. It identifies the
actual encoded result, canonical mask and retained supplied PSF bytes, constructs the complete record,
and serializes and identifies that record before publication begins. Filesystem staging consumes these
owned byte spans synchronously. The owners remain live through write, flush, close and both bundle
validation calls. An artifact beyond its admitted role bound is refused before commit.


## Identity is taken from the bytes that were used

A source digest identifies the immutable snapshot the decoder consumed, not a later reading of the
same path. The output digest identifies the encoded bytes independently decoded and compared before staging;
staged and committed bundle validation then checks those exact bytes. A mask carries
two digests, because they answer different questions: `supplied` identifies the file as given, and
`stored` identifies the canonical mask in the bundle — the admitted B-frame plane, which may differ in depth from the file supplied. G02 transforms
this plane identically into C for processing; its canonical asset is not turned. See [geometry](pipeline.md).

The image input is identified by its basename rather than its absolute input path; shell text is
not recorded. A supplied R01 kernel retains its exact admitted encoded bytes as `assets/psf.png`;
its original path is absent from the persistent request. The mask and PSF are confined bundle assets.
Names can still carry personal information, so a bundle is not anonymized. The original image input
is not copied into it.


## Preparing and reconciling publication

Each staged byte stream checks write, flush and close completion, including returned cancellation paths.
Record transfers and PNG reread sample comparisons observe cancellation in at most 64 KiB
blocks. A delayed stream failure takes precedence over cancellation; earlier genuine errors remain
primary. Before the final cancellation cutoff, the
host rereads and validates the staged bundle using the same reader and artifact checks as `verify`.
It compares the exact manifest digest and run identity with the values this invocation prepared.
I/O owns the native transaction and invokes one host validator before and after commit; it contains no processing
record parser or method admission rules.

After the exclusive rename, I/O checks that the destination is the retained owned directory object.
The host validates this run identity, exact manifest digest and complete artifacts through one native
bundle root without a separate record reopening. Observation after the cutoff does not accept cancellation
as a replacement for an irreversible outcome.

| Commit and observation | Outcome |
|---|---|
| Successful rename and matching complete bundle | Success, `completed` |
| Ambiguous rename error, matching owned native directory and matching complete bundle | Success, `completed` |
| Conclusive refusal | Output failure; clean only proven-owned staging |
| Ambiguous error with absent, foreign or unreadable destination | `E_PUBLICATION_UNKNOWN`, exit 7, `unknown`; preserve potentially relevant staging |
| Owned directory is at the destination but record/artifacts are unavailable or disagree | `E_OUTPUT_VERIFY`, exit 5, `completed`; retain the published directory |
| Successful rename followed by unavailable or disagreeing inspection | `E_OUTPUT_VERIFY`, exit 5, `completed`; retain the published directory |

Known publication is never downgraded to uncertainty by a later inspection failure. A foreign
destination is never modified. Staging retention also disables destructor cleanup. Native object
identities captured when files are created guard cleanup against replacement of a known name;
replacement or unverifiable ownership preserves the entry and produces uncertainty. Cleanup remains
nonrecursive and is not a guarantee against arbitrary simultaneous tampering by an equally
privileged process. There is no rollback, automatic processing retry, copy fallback or stale-stage
sweep, and atomic visibility still does not promise crash durability.
Native leases keep object identifiers from being recycled while checks are live. The transaction
retains at most 65 metadata handles: one root and at most 64 bounded owned entries. Windows compares
the full 128-bit file identifier and volume identity, with object leases opened by ID so descendant
path handles do not prevent directory commit. Windows publication requires native open-by-ID support;
SMB output destinations are refused. Use a supported local volume. Unsupported identity observation
or object opening fails closed.
Root, parent and created-entry identities are checked around byte placement, validation callbacks and the
commit cutoff. Known replacement prevents further effects, and copied record bytes cannot prove
publication origin. Rename/unlink cannot atomically compare a caller-supplied object identity; these
checks remain outside a guarantee against arbitrary equally privileged namespace changes between
checks and native operations.


## What agreement is not

A digest detects disagreement with expected bytes. It is not a signature: it identifies no author,
and anyone able to rewrite both an artifact and its record can produce another internally
consistent bundle. Confirming a bundle establishes a structurally valid supported record and agreement with the
observable properties of its included artifacts. Source identity, build identity, parameter use,
processing observations and the producer's original sample comparison remain recorded producer
claims; verification cannot establish those events without the original execution. It never reruns
illumination or binarization. Agreement is not a statement about the meaning or authenticity of the
original document.


## Numerical work and scheduling

Percentile selection validates first and sorts caller-owned scratch in place with O(n log n)
worst-case comparisons; it never
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
The process-only `--threads auto|N` option admits integer bounds 1 through 256. Automatic
selection clamps detected hardware concurrency to that domain, with a fallback of one when the
machine reports zero. `--memory-limit MIB` admits 64 through 262144 MiB and defaults to 1024.
An application-owned checked `ExecutionControl` carries these settings and cancellation separately
from `ProcessRequest` and its method plan. The host constructs one processing ledger from that limit;
response diagnostics report the resolved scheduler bound, selected ledger limit and charged high water.
Controls are absent from persistent records. Read-only bundle verification keeps its separate fixed
1 GiB snapshot budget. Charged bytes are not a process-RSS limit.

Interpretation uses one run-owned Little CMS context and immutable profiles, with a distinct native
transform and row buffers for every prepared reader. Original fixed bands lease readers exclusively;
reader acquisition does not change band indices, reduction order or earliest-error priority. Reader
construction happens before workers; every reader lease ends before its interpreter is destroyed.
The native context's allocation ledger and sticky failure flags synchronize concurrent callbacks.
Each transform retains NOCACHE and the existing numerical flags. Native reader admission can
reduce the requested upper bound after a resource refusal, fully destroying the failed context
before reconstructing a smaller pool. Input, numerical and cancellation errors remain terminal,
as does a resource refusal before any reader fits or at one worker. These preparations finish
before pixel work; processing and publication are not automatically retried.

## Enforced boundaries, not just a diagram

CMake rejects undeclared direct layer/package links. Every production target registers its files,
headers and links. Native builds must contain every declared layer. An isolated fuzz build must
contain the complete transitive closure of its named root, not a manually duplicated source list.
Both modes compile the same first-party targets. The CLI fuzz closure excludes host and codecs. Separate PNG, TIFF and ICC harnesses exercise the raw representation and native color boundary. PNG/JPEG/TIFF harnesses use the production byte-span
decoder; only those targets link the codec layer.

Source checks validate manifest shape, duplicate targets, directory ownership, direct includes and
this table. Compiler-backed checks read the real include graph, require public headers to compile
alone/twice, and compare actual includes with registered links. `clang-query` checks forbidden function references, thread constructors/launch APIs,
throw/catch boundaries, explicit allocator calls as well as new/delete expressions, and namespaces.
One manifest-owned restriction baseline has explicit layer allowances; `may_thread` permits worker
ownership only at the scheduler. Private production headers cannot cross layer owners or escape
through public interfaces. The registered CLI fuzz client is checked against the CLI public closure
using real includes and generator-time target links. Final CMake target properties must agree with
production registrations, including additions made after registration. White-box tests intentionally
use private numerical/native failure seams; they are not production layers.
Include trace decoding uses the configured compiler identity from the same build owner. Clang
backslash/quote transport escapes are decoded once; ambiguous line-break escapes are refused. GNU
trace names remain literal. Decoding preserves path bytes and never chooses between existing aliases.
AST locations use exact compiler-observed file spellings and resolved layer ownership; a checkout
ancestor named `src` cannot make foreign code subject to project permissions. An unparsed translation
unit or unanswered matcher is a failure, never an empty success result.

Strict compiler warnings, clang-tidy, file-size limits, formatting, Ruff, mypy and reviewed local
suppression identities remain enforced. Suppressions name a specific unavoidable boundary and its
reason; no blanket waiver or grandfathering path is introduced.

## Single sources of truth and verification

SAX preflight bounds record bytes, depth, event counts and decoded-key uniqueness before DOM construction. JSON cleanup must not allocate inside noexcept destruction; the checked private header uses ordinary recursive container cleanup under admitted depth and fixed builder shapes. The cache and upstream notices remain intact. Bounded ordering gives percentile selection a worst-case comparison bound without altering nearest-rank semantics.


The top-level CMake project owns versioning. `deps/lock.json` owns source identities;
`deps/features.json` owns upstream feature policy; `deps/tools.json` owns developer-tool versions.
`spec/cli-contract.json` owns the format/mode matrix; its generated descriptor supplies runtime
capabilities. `spec/method-contract.json` owns implemented method identities and parameter domains;
generated descriptors feed actual method types and Fields, and actual compiled variants must equal
the reviewed catalog. Method-option attribution comes from reviewed method arguments. These authorities
generate descriptors, reference docs and fuzz dictionaries.

Actual C++ Fields own the record and response wire shapes, each currently version15. The native schema
emitter derives structural constraints from the same scalar codec and finite discovery constraints
from actual compiled catalogs, including the selected-method discovery branch. `update-schemas`
writes only native-emitted artifacts; `schemas-current` compares their exact bytes. Neither source
authoring nor authored CI constitutes execution evidence. The real Draft2020-12 validator checks
executable responses, emitted records and independent negative controls; native reading additionally
checks calendar/UTF8-byte and cross-field relationships that schema metadata does not establish.
The metadata-only native emitter is a registered client of the CLI public closure and has no host
composition or processing entry point.

Required PR jobs cover structural/reference checks, seven native execution lanes including both macOS 15 minimum lanes, both libFuzzer and AFL++,
and independent ASan/UBSan and TSan suites. Scheduled campaigns use CTest's authoritative target
registration, including box-mean, rather than a second shell list. Native sanitizer presets instrument first-party code. Isolated fuzz builds additionally
instrument the selected JPEG/TIFF/PNG/zlib/Little CMS closure and verify the actual archive symbols; see [fuzzing](fuzzing.md). Documentation records current guarantees
and limits, rather than claiming that every platform or future method already passed.

## Cooperative cancellation

## Execution contract

Cancellation is execution control, not a method parameter. The admitted `ProcessRequest` remains
immutable and contains admitted paths and the selected binary or continuous operation choices. `core::Cancellation` owns a
`std::stop_token` and, optionally, a static-lifetime `noexcept` observation function. A default
capability never requests cancellation. An embedding caller supplies a token from its own
`std::stop_source`; destroying that source does not invalidate a retained token's stop state.
No callback registration, monitor thread, opaque object pointer or process-global lookup is hidden
inside the numerical methods.

The command line passes the same capability to application admission and the processing port.
For processing, checked `app::ExecutionControl` carries it beside the per-call worker and memory
bounds, separately from `ProcessRequest` and its persistent method plan. Verification retains its
separate cancellation argument and fixed snapshot budget.
The host passes it into the codecs, scheduler and publisher. The numerical scheduler owns the
capability used by its operation; `BinarizationContext` does not carry a second, potentially
contradictory token. Borrowed image views still require live backing storage until execution and
all worker joins have finished. Token ownership does not extend image lifetime.

Admission validates the invocation first. A malformed invocation remains `E_ARGUMENT` even when
cancellation is pending. A valid, already-cancelled processing or verification invocation returns `E_CANCELLED`
without calling its execution port, opening inputs or creating output. Verification cancellation
always reports `not_started`; it cannot acquire a publication stage. Help, version and method
discovery do not process images and remain available. Expected cancellation maps to exit **130**
on all platforms, including handled SIGTERM; it is not a shell-derived signal exit number.

JSON errors contain `code: "E_CANCELLED"` and publication `not_started` or `not_published`.
Cancellation is never paired with `completed` or `unknown`. When publication cannot be established,
`E_PUBLICATION_UNKNOWN` and exit 7 retain their meaning. The existing application exception boundary
uses its preallocated unknown-publication response for an escaping, unreported execution exception;
it does not convert such an exception into a claim of orderly cancellation.

## Native interrupts

The standalone executable installs one process-boundary `InterruptScope` before dispatch and removes
it after response delivery. The bridge is not installed by library calls or fuzz harnesses.

On POSIX, `sigaction` observes SIGINT and SIGTERM with `SA_RESTART`. Dispositions deliberately inherited
as `SIG_IGN` are preserved. Changed dispositions are saved and restored when the scope ends. SIGPIPE is ignored within this
standalone boundary so a closed response pipe reaches stream error handling and exit 5. It never
sets the cancellation latch. The standalone entry makes stdout/stderr unbuffered before I/O,
so teardown cannot retry response bytes after signal restoration. Library calls change neither
stream buffering nor signal dispositions. On Windows,
`SetConsoleCtrlHandler` handles CTRL_C_EVENT and CTRL_BREAK_EVENT. Windows console reattachment resets
handlers, so the executable must not detach or attach a different console during processing.

Both handlers do only one operation: store true in a constant-initialized, always-lock-free static
atomic latch. They do not call `request_stop`, allocate, log, access thread-local state, enter an
exception handler, invoke application callbacks, clean paths or join workers. The latch has process
lifetime and is never reset; an in-flight Windows callback cannot access a destroyed stack scope.
The normal execution checkpoints read it. Installation failure returns an entry-boundary invariant
failure before processing. Repeated handled interrupts keep the same cancellation request; there is
no second-interrupt force-exit policy.

The optional static observation function in `Cancellation` is a normal-execution seam, **not** code
run by an OS handler. It must be `noexcept`, thread-safe, monotonic once stopped and backed by static
storage that is not unloaded while execution is live. Production uses the latch probe. Deterministic
tests use synchronized static checkpoint state initialized before workers start and reset only after
all workers have joined. There is no testing flag or extra diagnostic output in the shipped program.

## Observation granularity and ownership

The closed `Checkpoint` vocabulary names admission, allocation, scheduling, initialization,
processing, decoding, encoding, staging and commit boundaries. It is not a progress or tracing API.
A checkpoint reports an observed request; a request arriving after the final checkpoint of an
already completed computation need not change that computation's result.

| Component | Safe checkpoints and ownership |
|---|---|
| Scheduler | Before worker launch and before executing a claimed task. Exhausted queues do not observe a new stop; fully completed work remains successful. Skipped claimed work returns cancellation. Every started worker joins, including after partial launch failure. |
| Global Otsu | Before charged histogram acquisition, each 1024-sample histogram/output block and each bounded score scan. The fitted threshold is retained through output verification. |
| Fixed threshold | Before each bounded block of at most 1024 samples, including wide-row interiors. |
| Sauvola | Before workspace acquisition, reflected initialization rows, output rows and 1024-sample output blocks. Column initialization/advance is bounded by the existing fixed strip and halo width, not image height. |
| Box mean | Before intermediate acquisition, at 1024-sample reflected-sum/output intervals and within both separable passes. The no-stop summation order is unchanged. |
| PNG decoding | Before header work, before plane allocation and zero-fill blocks, in bounded input callbacks and between decoded rows/passes. File and byte-span decoding use the same implementation. |
| PNG encoding | Before opening the staged output, between rows and in bounded output callbacks. File close and error checking complete before cleanup or commit. Delayed flush/close failures outweigh cancellation; an earlier genuine failure remains primary. |
| Output verification | During bounded native reads and 64 KiB sample comparisons. |
| Manifest writing | Before opening and each transfer of at most 64 KiB, with checked flush and close on returned paths. |
| Publication | Before stage reservation, between writers, during encoding and once immediately before the native exclusive rename. |

PNG input/output callbacks transfer at most 64 KiB between observations. The token and observed-stop
flag live in `PngContext` outside every libpng jump frame. Callback and jump-frame automatic state
is trivial; a libpng jump does not cross a C++ resource owner. An observed cancellation is recorded
at its checkpoint. Later error handling does not reread a pending request and relabel a genuine codec,
I/O or allocation failure. Charged codec/image/workspace allocations are refunded by normal ownership
unwinding. Small metadata and cancellation error strings are not promised to be allocation-free.

The custom PNG error adapter invokes the CRT jump routine directly, using caller-owned jump
storage and avoiding indirect dispatch of the CRT entry under Windows control-flow protection.
A trivial bootstrap frame is active before native context creation. Native callbacks never
reconfigure jump storage; all C++ resource owners remain in the caller. No guard is disabled.

A cancelled numerical operation may have modified part of its destination; that internal plane is
not a valid result and must never be published. The source is unchanged. Workspace acquisition
failure and cancellation before initialization preserve the existing pre-write guarantees.

After joining workers, a genuine task error takes priority over cancellation. Among observed genuine
errors, the lowest task index wins. A thread-launch failure keeps resource-failure priority. A
zero-work request can succeed; skipped unfinished work cannot be reported as successful completion.
This precedence does not pretend to discover errors in tasks that cancellation prevented from running.

## Publication cutoff

The final precommit checkpoint is the cutoff. If it observes cancellation, abandon this invocation's
stage and do not call the rename operation. If it observes no cancellation, it authorizes the native
atomic no-replace rename. A request arriving after that snapshot is late, including one arriving just
before the actual system call. It does not retroactively revoke the authorized operation.
Ownership checks surround that normal-execution observer and reject observed namespace changes.
They do not reread cancellation after the cutoff. Native leases distinguish this invocation's
directory from copied contents at a foreign destination; an observed owned destination after a lost
reply establishes completed publication, even if later integrity verification fails.

| Outcome | Report |
|---|---|
| Cancelled before any stage is owned | `E_CANCELLED`, 130, `not_started`. |
| Cancelled with owned staging and confirmed cleanup | `E_CANCELLED`, 130, `not_published`. |
| Native commit succeeds, even after a late request | Publication remains `completed`; a later integrity/inspection failure may return `E_OUTPUT_VERIFY`. Do not delete the published result. |
| Native commit is definitely refused | Preserve the output failure; a late request cannot erase it. |
| Commit or owned cleanup cannot be confirmed | `E_PUBLICATION_UNKNOWN`, 7, `unknown`, even when cancellation is pending. |

Only the known owned staged file and its directory may be removed. No recursive production cleanup,
foreign-stage sweep, existing-destination deletion or crash-leftover recovery is introduced.
The publisher's private rename-operation seam allows deterministic tests around the cutoff; the
public publisher always binds the real native no-replace operation. Return metadata is prepared
before commit. Bundle preparation uses the complete validator before this cutoff; postcommit
reconciliation ignores late cancellation and preserves both known publication and ambiguous state.
Bundle reads and content hashing poll during bounded transfers, including source and mask identities. Atomic visibility is still **not crash durability**, and output ancestors remain
trusted against hostile replacement.

Delivery stays separate from execution: writing, rendering or flushing can fail after a cancellation
response or a completed image outcome has been determined. Process exit 5 may supersede the rendered
command's exit number. There is no retry, second response or retrospective alteration of delivered
bytes. Neither missing output nor exit 5 establishes that image publication did not commit.

## Shutdown restrictions

The following approaches violate the cancellation contract:

- Calling `std::stop_source::request_stop()` from a signal handler, borrowing a stack source from a
  Windows callback, resetting shared handler state, or introducing a polling monitor thread.
- Checking only between scheduler tasks: a Sauvola task contains full-height strip work and box-mean
  initialization can span long reflection periods. Checkpoints must reach those interiors.
- A second independently supplied cancellation token in the method context, nontrivial owners inside
  codec jump frames, or exception reporting that claims safe cancellation after unknown effects.
- Treating a request as proof of a cancelled commit, erasing a genuine error with a late stop, or
  cleaning arbitrary matching stage paths. The final observation authorizes commit and actual
  filesystem outcomes remain authoritative.
- Sleep-based correctness tests or a test-only mode in the shipped executable. Checkpoint probes,
  worker barriers and a separate native test driver provide deterministic control instead.

Cancellation remains execution control, independent of method configuration.

## Cancellation primary references

- [C++ signal-safe evaluations](https://eel.is/c++draft/support.signal)
- [C++ stop tokens and synchronous stop callbacks](https://eel.is/c++draft/thread.stoptoken)
- [POSIX sigaction](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigaction.html)
- [Windows console-handler registration](https://learn.microsoft.com/en-us/windows/console/setconsolectrlhandler)
- [Windows console callback lifetime and events](https://learn.microsoft.com/en-us/windows/console/handlerroutine)

JPEG source admission also observes decode checkpoints during bounded framing/entropy scans,
ICC assembly, native input/progress/scan work, charged allocation/initialization and decoded rows.
Tests enumerate deterministic stop points through the actual decoder and require full refunds.
These checkpoints feed the same control; no OS-handler or final precommit policy changes. See
[JPEG source admission](formats/jpeg.md) for foreign-call and resource boundaries.


D01 checks cancellation before charged allocations and every 256 entries during reflection-index
and weight-table initialization, Q16 gathering, distance generation, column sums, weighted accumulation
and mapping. Its eager first-party Working operation retains the immutable global Q16 plane and
weight table while scheduled bands of at most 64 rows visit all admitted search offsets. No native
D01 tile or method replay is involved. Workers join before returning; genuine worker errors retain
the scheduler's precedence over cancellation. Only completed D01 Facts survive later stage,
verification or publication failures; final-sample decode-back comparison does not rerun D01.
The existing final commit cutoff remains authoritative.

Moved-from requests are unready and refused before cancellation/I/O. First-party producers carry
actual effects unchanged; an escaping, unreported processing exception uses the preallocated unknown
fallback. The retained wire receiver admits `E_CANCELLED` only with `not_started` or `not_published`.
Verification exceptions are contained at the read-only boundary with `not_started` and no processing
work. [Architecture](#authority-and-composition) owns these boundaries.

I02 measures the current Working luminance and protection planes, checking measurement cancellation
every 1024 visited samples during gathering and protected-field filling. Its extrema passes check
processing cancellation every 256 queue positions; Gaussian passes and application check every
128 output samples, with at most 769 coefficients per Gaussian dot product. Preparation retains an
immutable fitted scalar field through the joined application, then releases it before D01. Charged
owners refund on failure or cancellation; completed Facts are stored only after successful eager
application and validation. Encoding compares final samples without rerunning I02, and read-only
bundle verification checks actual artifact/record agreement. The publication cutoff is unchanged.
These work bounds are not a hard latency deadline: indivisible PocketFFT calls in restoration and
blocking foreign or OS operations still need platform-specific latency qualification.

## Eager interpretation and fixed method execution

[Pipeline](pipeline.md) owns the eager stage sequence, retained pixel owners and exact geometry.

## Native sanitizer coverage

Sanitizer coverage follows effective compile commands and actual executed controls, not a preset
name alone. The sanitizer presets instrument first-party owners. TSan additionally instruments every
locked Little CMS C translation unit with fatal thread checks; other dependency internals remain
outside that TSan scope. Dependency command and private archive evidence are required, with a fresh
private build and prefix after recipe changes. The fuzz recipes separately instrument JPEG, TIFF,
PNG, zlib and Little CMS with ASan/UBSan. Native hardening and sanitizer instrumentation are separate
properties. NOCACHE and private reader ownership do not replace instrumented race controls.
An instrumented build is not a passing TSan runtime, platform or minimum-OS result.

## Identified image sources

The common pipeline calls the format-neutral source adapter. One bounded immutable encoded snapshot
is hashed and dispatched by signature, never by extension or decoder retries. `image::RasterMetadata`
contains shared ICC/orientation/resolution observations with closed PNG/JPEG/TIFF container declarations;
no native codec types escape `codec`. Common EXIF extraction retains its bounded IFD0 contract. PNG's
color/physical precedence remains codec-owned; terminal thresholding follows common interpretation.

The JPEG adapter promotes the locked static dependency into `de_codec`, with the manifest owning that
external-package permission. A bounded framing/metadata scan and charged native memory manager
precede full-resolution native decoding. Native jumps stay inside owner-free wrappers, while the
request's buffers and contexts outlive the frame. JPEG has no separate host, publisher or numerical
method. New records and responses carry typed source observations with explicit format/wire versions;
only the current closed bundle format is supported. See [JPEG](formats/jpeg.md).

TIFF uses the same host boundary, with a manifest-owned `DE::tiff` permission. Bounded directory
and strile admission precedes native decoding. Native payload reservations, per-handle errors,
shared charged JPEG control and strict Deflate completion belong to this adapter. Source sample
association and MINISWHITE declarations remain separate from color/profile interpretation;
orientation is applied once in common interpretation. See [TIFF](formats/tiff.md).

## Luminance denoising and native Fourier isolation

D01 owns binary64 NLM weights, exact cutoff/table cardinality, fixed accumulation order and one
final store. It has no native matrix/type dependency. D02 owns full-field binary64 TV-L1 workspaces
and retains an actually observed partial stop before unwinding; an iteration cap is not convergence.
R01 uses charged binary64 pocketfft blocks through one serial, cache-free adaptation. Native original
C/C++ allocation families, finite workspace reservations, move/borrow readiness, errors, refunds and
joined-worker behavior need independent native controls on each required platform. A charged
high-water measurement is not a process-RSS guarantee. All completed earlier facts survive downstream
failure, and publication remains one complete bundle transaction.

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
changes cannot silently enter release contents. [Package verification](build.md#complete-execution-and-package-evidence) owns the required artifact comparisons.

Raw option storage is indexed by generated identity and retains explicit presence. Application
admission parses supplied values by kind, checks applicability and required fields, then uses
generated concrete bindings to construct actual method records. Factories add cross-field rules.
Each selected eager stage owns its workspaces until all of its consumers and workers finish.
Generated files are rewritten only when their expected bytes change, so ordinary metadata
changes do not invalidate unrelated native translation units through timestamp churn.

Directory
access requires an active owner and confined entry names. CLI resource fallbacks allocate nothing;
rendering remains separately fallible.

[Quality](quality.md) requires independent source/compiler evidence, private-header isolation and exact restriction/exception permissions.

## Exact geometry before photometric execution

G01 metadata orientation and G02 explicit quarter-turns use checked integer coordinate mappings
in the image layer. Continuous color rows compose inverse G02/G01 gathers against the retained
decoded source, without interpolation or an additional full color plane. The host admits masks
in metadata-oriented frame B, transforms them identically into C for computation, and retains B
for canonical bundle assets. Binary output follows the same geometry before terminal Q16 classification. Request/record validation reconciles A/B/C extents and
output dimensions; physical resolution follows transpose parity. See [geometry](pipeline.md)
for processing frame F and the unsupported G03–G05 boundaries.
