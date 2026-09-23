# Design decisions

These decisions describe the current system and their rationale, not a session history. Change
them with the implementation; record user-visible changes in the [changelog](../CHANGELOG.md).
The [architecture](architecture.md) owns the detailed guarantees and limitations.

## Language and build model

Use C++23, explicit CMake targets and header file sets, Ninja presets and an isolated dependency
superbuild. A second build framework, a pre-standard language baseline or C++ modules would add
portability obligations without resolving a demonstrated runtime requirement. Ordinary translation
units therefore do not enable automatic module scanning. Dependencies are acquired explicitly;
a normal build verifies the cache rather than downloading or substituting host packages.

`deps/lock.json` owns source identities, `deps/features.json` upstream feature policy and
`deps/tools.json` tool versions and deployment floors. Exact pins and receipts detect changed
inputs; they do not prove that upstream code is trustworthy. Source/licence review and package
inspection remain necessary. Keep external types out of public headers and libraries behind the
adapter that owns their effects. The native dependency probe is a verification program, not
permission to use every pinned imaging library in production.

## Typed admission and explicit execution

The CLI translates syntax into `contract::Invocation`. Application validation constructs a private
`ProcessRequest`, and the processing port accepts only that admitted value. `de_host` implements
the port; `entry` composes it. Tests and CLI fuzzers inject a deterministic non-I/O implementation,
not a runtime bypass flag. This preserves realistic command coverage without filesystem authority.

Do not prebuild an arbitrary recipe engine or a dependency-injection framework. Extend admission,
execution and capability reporting together when another complete operation exists. The generated
catalog describes options and reviewed method definitions; it does not validate a processing plan.
The B03/PNG operation is real. Unimplemented catalog entries remain unadvertised and return explicit
failure rather than a successful copy or no-op placeholder.

## Exception containment follows authority

Expected failures use `std::expected<T, Error>`, aliased as `core::Result<T>`. Numeric kernels do not
throw or catch. External-library, scheduling and transport boundaries contain exceptional failures
under permissions declared in the layer manifest. The application must also contain exceptions
escaping the processing port: only it knows that execution has begun and effects may have occurred.

Before calling the processor, prepare a complete unknown-publication response. Returning that
response on an unexpected exception requires no fresh diagnostic allocation; a compile-time check
requires the outcome move to be non-throwing. Do not declare the processor `noexcept`: terminating
would lose the very outcome distinction that callers need. Returned expected failures keep their
reported state. Pre-execution validation remains before this boundary, so ordinary argument
rejection still means processing did not start.

An outcome's exit status is derived from its typed payload, not independently writable state.
`de_report` alone renders it. The CLI emits once with unformatted writes, so an embedding caller's
width/fill settings cannot rewrite the serialized bytes. It explicitly flushes only the stream
carrying content, without reconfiguring standard stream ties or exception masks. Buffer insertion alone does not establish delivery: `flush()` can set `badbit` or throw.
A transport failure never causes processing or rendering to run again. This does not acknowledge
consumer receipt, define a new process-signal policy or promise filesystem durability. JSON
`exit_code` describes the rendered outcome, not a later transport failure. A complete success
payload may therefore precede process exit 5; consumers must examine both without blind replay.

## UTF-8 is an admission contract, not a repair policy

Validate UTF-8 scalar encoding without allocation, locale dependence or normalization. Reject
malformed CLI tokens before CLI11 can quote them, and independently validate path strings in direct
application admission. NUL is a valid Unicode code point but not a valid embedded path character.
Unassigned scalar values and noncharacters are not malformed UTF-8; surrogate encodings, overlong
forms, truncated sequences and values above U+10FFFF are malformed.

Do not silently rewrite an identity to satisfy JSON. Strict serialization fails on a malformed
output path, whereas a malformed diagnostic gets a visibly explanatory ASCII fallback. This
separates identity from display text instead of allowing a global serializer replacement option
to change filenames. POSIX joins use `/`; a backslash there belongs to a filename. Windows path
conversion remains at the native adapter. Preserve admitted spelling, including decomposed Unicode.

The validator is checked over every scalar value and against an independent strict JSON decoder
in its own engine-agnostic fuzz target. CLI fuzzing and executable path tests exercise admission,
serialization and native publication together; a validator-only test is not sufficient.

## Memory and schedule are explicit resources

`core::Budget` tracks charged image/codec blocks. Move-only buffers share ownership of the atomic
accounting ledger, so a buffer can outlive its originating budget handle. Checked planes and views
establish extent and stride before kernel access; kernels additionally reject unsupported overlap.
Small standard-library metadata, C stream buffers and OS thread stacks are not charged blocks.
Do not claim that a ban on explicit allocation expressions eliminates every implicit allocation.

The scheduler owns work partitioning and thread creation, not an imaging dependency. Indexed
regions keep the numerical kernel independent from worker count; scoped workers join on failure
and report a deterministic task error. A persistent pool or different scheduler is justified only
by a measured requirement, not by a desire for more abstraction. The present CLI exposes neither
`--threads` nor `--memory-mib`; the B03 host uses its documented internal resource ceiling.

## Publication is an irreversible boundary

Write a complete PNG into exclusively owned sibling staging, close and check the encoded file,
and prepare allocating response metadata before the commit operation. Commit uses a native atomic
no-replace operation, never an existence check as a substitute for that operation. Cleanup touches
only the paths the current attempt owns; foreign entries and ambiguous filesystem errors must not
be hidden by a recursive cleanup or a false safe-retry response.

Atomic visibility is not crash durability. The output parent and its ancestors are trusted; this
is not a hostile-filesystem sandbox. `unknown` publication and a missing or incomplete response
require inspection rather than automatic replay of a potentially completed operation.

## Enforcement must observe the real program

`spec/architecture.json` declares the layer graph once. CMake verifies registrations and direct
links. Compiler dependency output checks include closure, clang-query checks AST restrictions and
standalone compilation checks public headers. Tests must demonstrate forbidden behavior is
rejected, not merely assert that a policy file exists. Compilation or matcher failure is not an
empty success result. Source-only checks on platforms without compatible compiler tooling are
not equivalent to compiler-backed enforcement.

Warnings, clang-tidy, formatting, Ruff, mypy and size limits remain required. Suppressions must be
narrow, code-bound and explained; a new implementation does not inherit an exception because its
filename or line number resembles an old one. Fuzz targets compile the same production targets,
and CTest registration drives campaigns and normal corpus replay. Full ASan/UBSan and TSan jobs
are independent, so a failing sanitizer job does not prevent the other from running.

Use the real JSON Schema validator on executable responses. A permissive homemade subset cannot
establish conformance to a closed response schema. Generated contract files are byte-checked from
their reviewed sources. Required GitHub checks establish the submitted commit's CI state; a local
subset, a historical run or a newly authored workflow cannot establish it.
