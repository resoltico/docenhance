# Execution states, lifetimes and ownership

## Design pass

Trace admitted requests through real processing/verification ports, numerical stages, worker joins,
codecs, staging, validation, commit, reconciliation and one-shot delivery. Keep publication distinct
from numerical completion and stream delivery. Read-only verification cannot acquire publication.

Remediate the owning boundaries:

- Application dispatch checks returned error/publication combinations and successful identities,
  admitted output target, conversion geometry/policy, stage requests/counts and verification
  confirmations. A malformed processing return uses the preallocated unknown outcome, without
  replay. Coherent failures retain their observed state, including completed integrity refusal.
- Share identity/calendar checks in core, conversion checks in image and illumination checks in
  methods, including completed D01 call/reservation agreement with extent, with the record reader. The typed reader retains record-specific relationships and
  canonical grammar; no runtime schema engine or additional layer permission is introduced.
- Request moves explicitly consume the source's required path. Native ports reject unready values
  before stop observation or I/O. Borrowed metadata requires an lvalue owner. Binary row adapters
  check index/shape before borrowing backing storage, preserving output on refusal.
- Native bundle directory access requires an active handle and a single relative entry name.
  Absolute, parent/nested and NUL names are refused; Windows also refuses alternate streams.
  POSIX literal backslashes retain their native filename meaning. Moved-from/default owners
  cannot reopen paths or enumerate the working directory.
- Verification prepares exception/malformed-return fallbacks before its read-only port starts.
  CLI allocation failure uses an allocation-free outcome; diagnostic construction failure cannot
  escape the boundary. Rendering/delivery may still fail with exit 5 and never retry execution.

## Separate design challenge

Before remediation, independent ASan probes demonstrated stack-buffer-overflow from an out-of-range
PlaneRows request and stack-use-after-scope from RasterMetadata{}.png(). A real native directory
probe opened an absolute foreign file through a default owner. A controlled processor returned
E_CANCELLED with completed publication and dispatch accepted it. A persistent C++ allocation-refusal
probe made diagnostic allocation escape cli::run.

Challenge the remedies against real boundaries. Preserve lvalue metadata borrows and valid row reads;
check empty/end/UINT32_MAX ranges without mutation. Move/reassign actual directory handles and
request owners, refusing their consumed sources before a pending stop. Preserve valid relative
access and POSIX literal backslashes. Mutate reported identities, geometry, counts, request settings,
readonly directory/calendar/inventory and failure states. A known committed integrity error remains
completed; contradictory claims must not become success or safe retry.

Do not replace shared ledger lifetime, scoped worker joins, native jump-frame ownership, the final
precommit cutoff, object-identity cleanup, exclusive rename, record-first reconciliation or checked
flush/close. Existing deterministic counterexamples protect these boundaries. A retaining-pointer
framework, retry/recovery path or filesystem sandbox would not repair the observed faults.

## Verification obligations and limits

Run real executable and complete native/reference/tooling/fuzz/Docker workflows, then exact-head CI
before merge. The allocation observer is shared with native resource experiments. Native/ASan builds
exercise sustained refusal through the CLI; TSan retains ownership of its allocator ABI and observes
the allocation-free handler directly. Report these modes separately rather than claiming fault
injection under TSan. Missing responses do not establish absence of processing effects.

Wire/record versions and numerical method definitions remain unchanged. Response schemas refuse
zero record identities, empty/unbounded verification inventories and publication outside processing.
The typed boundary additionally checks calendar and cross-field relationships. These observations
are not exhaustive lifetime safety, process-RSS limits, authenticity or crash durability. Explicitly
moving/destroying an lvalue owner during use still violates the caller's borrowing contract.
