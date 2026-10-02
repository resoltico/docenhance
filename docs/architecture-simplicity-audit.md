# Architecture simplicity and change coupling

## Design pass

Trace ordinary changes through reviewed syntax, raw invocation storage, typed admission, native
execution, row generation, records, publication and delivery. The layer graph remains appropriate:
application factories own meaning; the CLI owns syntax; host composes execution; numeric methods
have no native effects; codec and filesystem adapters own their resources.

Three avoidable coordination points need remediation:

- CLI registration is generated, but value transfer repeats every option name in a handwritten
  assignment list, alongside separate string/flag maps and local root/command state. Generate typed
  invocation-member bindings with option metadata and register directly against stable per-command
  invocation storage. Keep absence distinct from an explicit empty value, command scopes, repeated
  option refusal and the exact JSON error pre-scan. Domains still belong to application factories.
- Publication selects artwork, operation and an optional converter independently, and supplies
  continuous-only mask/stage facts even on the binary path. Use binary/continuous artwork values
  carrying their matching operation and observation owners. Derive record fields from that one
  selection. Borrow stage observations until rows are complete; do not predict or duplicate them.

- Generation rewrites all seven outputs even when their bytes are unchanged. A controlled run
  confirmed all seven timestamps changed, causing unrelated consumers to rebuild. Compare exact
  output bytes and write only changed files; still detect stale or corrupt output in check mode.

Return the prepared illumination model as an owned optional result rather than a one-field
wrapper and output parameter. Its caller retains it through preparation, encoding and verification.
The continuous response takes conversion observations from the published record, with no fallback
that can conceal their absence; an integrity refusal preserves known completed publication.

## Separate design challenge

Before implementation, inspect the pinned CLI callback: it converts a present value and invokes
the callback, without assigning defaults to absent options. Register callbacks only after the
command array is in its final position; keep that storage live until parsing ends. Moving an
invocation after parse must preserve the outer JSON pre-scan even on root/subcommand errors.
A real compiler control accepts the actual header, rejects a flag bound to string storage, and
rejects a nonexistent invocation member. Selective-generation controls retain fixed timestamps
on unchanged outputs and update only the two consumers of a changed help description.
Generated member pointers must exist and match flag/value semantics at compile time; missing,
duplicate or injected binding spellings fail generation.

The binary and continuous PNG writers intentionally differ: binary/mask output has no color
profile, while continuous output requires one. Keep their admission and native jump wrappers
distinct. Keep allocation/native exception containment, scheduler joins, observation-free replay,
complete staged verification, one exclusive commit, record-first reconciliation and one-shot
response delivery. A generic pipeline/recipe engine or merging codec layers would add concepts
or lose these distinctions without addressing the observed coordination points. No graph edge
or exception permission changes are needed.

## Verification obligations

Check nondefault CLI values at their actual typed owners, plus real executable defaults, empty
values, repeated options, scoped help, UTF-8 identities and root flag errors. Existing real bundle
fixtures verify binary/continuous records, masks, I01/D01 composition, native cancellation,
publication failures and delivery after commit. Run complete native/reference/tooling/fuzz and
Docker gates, then exact-head CI before merge. Their concrete outcomes belong to the PR and logs,
not a claim that the design is exhaustive proof of safety.
