# Execution, failure, cancellation and publication audit

## Design pass

Trace admission, the processing port, scheduler joins, native codecs, numerical observations,
staging, validation, exclusive commit, reconciliation and one-shot response delivery as separate
owners. Preserve the existing final precommit cutoff and error precedence: pending cancellation
is not evidence of failure, absence or rollback. Returned errors describe observed effects;
escaping processing exceptions retain the prepared unknown-publication response.

Three gaps need remedies:

- The standalone POSIX executable inherits SIGPIPE's terminating disposition. A closed response
  pipe can kill it before stream delivery returns exit 5. Ignore SIGPIPE only for the executable's
  boundary scope, save/restore its disposition, and leave library callers' process state alone.
- Verification already returns cancellation, but the response schema only permits it for process.
  Admit cancellation after valid verification admission, permit both commands in the schema, and
  require verification's publication state to remain not_started.
- PNG output verification uses libpng's default file reader without bounded transfer checkpoints;
  manifest writing similarly uses one unbounded fwrite. Reuse the bounded PNG reader with an
  explicit verification checkpoint, and carry execution control into bounded manifest writes.
  Check between generic bundle writers as well. Check actual write/read failures before observing
  another stop; always flush and close an acquired stream exactly once on returned paths.
  A delayed flush/close failure outranks cancellation; an earlier genuine failure remains primary.

No new layer, worker pool, retry, signal polling thread, recovery or compatibility path is needed.
The scheduler's fixed outcomes and scoped jthreads remain appropriate: joins precede diagnostic
allocation; lowest observed genuine task error outranks cancellation; launch failure wins.
Native jump frames remain trivial, with owners in enclosing callers. Publication metadata is
prepared before rename; known rename success remains completed even when observation throws.

## Separate design QA

Challenge the proposed fixes before product changes:

- Reproducing response delivery against a pipe with no reader returns signal termination (-13)
  on the existing macOS executable. Library stream tests cannot establish process behavior.
  Test the actual executable with a closed stdout and closed stderr and also verify a successfully
  committed bundle survives failed response delivery.
- Do not route SIGPIPE through the interruption latch: delivery failure must remain exit 5,
  not cancellation. Restore a deliberately installed prior SIGPIPE handler and preserve ignored
  SIGINT. Windows has no POSIX disposition; exercise its native broken-pipe stream behavior too.
- Merely broadening the cancellation command enum admits impossible verification not_published.
  Add a schema constraint and positive/negative mutation checks. Application admission must reject
  malformed requests before cancellation and avoid calling a valid already-cancelled verify port.
- Put reader state outside every setjmp frame. The callback must not allocate or throw, and a short
  read must be reported before a later pending cancellation can replace it. Preserve decoder byte
  bounds and sample semantics when sharing its existing callback.
- Cancellation between manifest transfers must close and refund through ownership, prevent commit,
  and preserve real I/O failure. Inject short writes with a simultaneous stop to prove precedence.
- Throwing preparation must clean owned staging; throwing observation after native success must
  return completed integrity failure; throwing observation after an ambiguous native result must
  retain unknown publication and staging. Test these authority boundaries without timing sleeps.

These challenges retain the design above. Hard termination, blocking native calls and crash
persistence remain outside the cooperative/atomic-visibility contract.

## Implementation challenge: stream teardown

The real executable still returned -13 after scoped SIGPIPE ignoring: libc retained bytes from
the failed flush and retried them at teardown, after the disposition had been restored. The
separate challenge rejects extending cancellation semantics or bypassing all process destruction
with immediate termination. Configure the standalone stdout/stderr C streams as unbuffered before
any I/O, preserving the normal synchronized C++ stream relationship. Then no failed response
remains buffered for teardown; the explicit CLI write/flush still determines delivery and exit 5.
Both entry variants require successful setup before admission. Library calls and embedding streams
retain their caller's configuration. The real closed-pipe test must pass through ordinary process
termination, and the signal-disposition test must still prove restoration.

The native CRT contract requires buffering setup before I/O and defines `_IONBF` as unbuffered;
see [Microsoft's setvbuf reference](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/setvbuf).
The real-process tests establish this project's behavior on each executed platform, rather than
treating the API description as evidence that the executable passed.

The publication API header is registered in its owning I/O target's public file set. It was the
only public header omitted from target registration; independent header analysis still checked it,
but the actual CMake target inventory must describe the interface it uses. No layer edge changes.
