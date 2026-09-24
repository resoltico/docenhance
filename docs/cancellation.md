# Cooperative cancellation and publication-safe shutdown

## Execution contract

Cancellation is execution control, not a method parameter. The admitted `ProcessRequest` remains
immutable and contains paths and the selected B02/B03 method. `core::Cancellation` owns a
`std::stop_token` and, optionally, a static-lifetime `noexcept` observation function. A default
capability never requests cancellation. An embedding caller supplies a token from its own
`std::stop_source`; destroying that source does not invalidate a retained token's stop state.
No callback registration, monitor thread, opaque object pointer or process-global lookup is hidden
inside the numerical methods.

The command line passes the same capability to application admission and the processing port.
The host passes it into the codecs, scheduler and publisher. The numerical scheduler owns the
capability used by its operation; `BinarizationContext` does not carry a second, potentially
contradictory token. Borrowed image views still require live backing storage until execution and
all worker joins have finished. Token ownership does not extend image lifetime.

Admission validates the invocation first. A malformed invocation remains `E_ARGUMENT` even when
cancellation is pending. A valid, already-cancelled processing invocation returns `E_CANCELLED`
without calling the processing port, opening the input or creating output. Help, version and method
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
as `SIG_IGN` are preserved. Changed dispositions are saved and restored when the scope ends. On Windows,
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
| Scheduler | Before starting workers and claiming tasks. Stop assigning unnecessary work, permit cooperative exits, and join every started worker, including after partial thread-launch failure. |
| Fixed threshold | Before each bounded block of at most 1024 samples, including wide-row interiors. |
| Sauvola | Before workspace acquisition, reflected initialization rows, output rows and 1024-sample output blocks. Column initialization/advance is bounded by the existing fixed strip and halo width, not image height. |
| Box mean | Before intermediate acquisition, at 1024-sample reflected-sum/output intervals and within both separable passes. The no-stop summation order is unchanged. |
| PNG decoding | Before header work, before plane allocation and zero-fill blocks, in bounded input callbacks and between decoded rows/passes. File and byte-span decoding use the same implementation. |
| PNG encoding | Before opening the staged output, between rows and in bounded output callbacks. File close and error checking complete before cleanup or commit. |
| Publication | Before stage reservation, during encoding and once immediately before the native exclusive rename. |

PNG input/output callbacks transfer at most 64 KiB between observations. The token and observed-stop
flag live in `PngContext` outside every libpng jump frame. Callback and jump-frame automatic state
is trivial; a libpng jump does not cross a C++ resource owner. An observed cancellation is recorded
at its checkpoint. Later error handling does not reread a pending request and relabel a genuine codec,
I/O or allocation failure. Charged codec/image/workspace allocations are refunded by normal ownership
unwinding. Small metadata and cancellation error strings are not promised to be allocation-free.

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

| Outcome | Report |
|---|---|
| Cancelled before any stage is owned | `E_CANCELLED`, 130, `not_started`. |
| Cancelled with owned staging and confirmed cleanup | `E_CANCELLED`, 130, `not_published`. |
| Native commit succeeds, even after a late request | Success, with the actual output path and `completed`. Do not delete the published result. |
| Native commit is definitely refused | Preserve the output failure; a late request cannot erase it. |
| Commit or owned cleanup cannot be confirmed | `E_PUBLICATION_UNKNOWN`, 7, `unknown`, even when cancellation is pending. |

Only the known owned staged file and its directory may be removed. No recursive production cleanup,
foreign-stage sweep, existing-destination deletion or crash-leftover recovery is introduced.
The publisher's private rename-operation seam allows deterministic tests around the cutoff; the
public publisher always binds the real native no-replace operation. Return metadata is prepared
before commit. Atomic visibility is still **not crash durability**, and output ancestors remain
trusted against hostile replacement.

Delivery stays separate from execution: writing, rendering or flushing can fail after a cancellation
response or a completed image outcome has been determined. Process exit 5 may supersede the rendered
command's exit number. There is no retry, second response or retrospective alteration of delivered
bytes. Neither missing output nor exit 5 establishes that image publication did not commit.

## Design review and separate QA

The design pass preceded product-code changes. The separate QA pass challenged and rejected:

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

The accepted implementation keeps these constraints explicit. It adds no method, image format,
recipe framework, job service, persistent thread pool, migration or compatibility branch.

## Verification and limits

C++ tests cancel before admission and at interior numerical, codec and publication checkpoints.
They assert worker joins/error precedence, source preservation, workspace refunds, owned cleanup,
precommit cancellation and success/refusal/uncertainty after a late request. The decoder tests enumerate
every checkpoint for both ordinary and Adam7 input; encoding tests enumerate checkpoints through
successful publication. Negative schema tests reject contradictory cancellation responses.

A separate test executable links the same production interrupt bridge, CLI and host. Its readiness
handshake lets a parent deliver genuine SIGINT/SIGTERM on POSIX or targeted CTRL_BREAK on Windows
before dispatch. POSIX tests also verify inherited ignored dispositions and restoration. In-process
checkpoint tests cover exact processing phases; the process test is not a claim of hitting each phase
with a timed OS signal. The manifest-declared cancellation fuzzer generates deterministic stop points
without filesystem effects or uncontrolled process signals, checking refunds and successful outputs
against existing independent sample references. Normal method/reference suites continue checking
unchanged no-stop numerical behavior.

These are coverage definitions, not a statement that every platform has already passed. Actual results
belong to the tested commit and its CI logs. Cooperative cancellation is not a hard realtime guarantee:
foreign-library work, allocator calls, blocking reads/writes and filesystem operations can delay a
checkpoint. A blocked delivery stream can delay exit too. No cleanup guarantee is made for SIGKILL,
TerminateProcess, console close/logoff/shutdown, crash or power failure. Charged-buffer accounting is
not a process-RSS bound. Partial internal buffers are discarded, not certified as faithful documents.

## Primary references

- [C++ signal-safe evaluations](https://eel.is/c++draft/support.signal)
- [C++ stop tokens and synchronous stop callbacks](https://eel.is/c++draft/thread.stoptoken)
- [POSIX sigaction](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigaction.html)
- [Windows console-handler registration](https://learn.microsoft.com/en-us/windows/console/setconsolectrlhandler)
- [Windows console callback lifetime and events](https://learn.microsoft.com/en-us/windows/console/handlerroutine)
