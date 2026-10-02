# Filesystem identity and hostile mutation

## Design pass

Trace source/mask snapshots and complete bundle reading, staged file creation, comparison/hash,
commit, reconciliation and cleanup. Preserve exact admitted UTF-8 spelling in responses and records;
bind effect paths once without lexical/canonical normalization. Source leaf links may resolve once
to an opened regular file; bundle/staging entries must remain non-link objects.

Independent native probes reproduced two failures before remediation: a writer changing the process
working directory redirected a relative publication to an unrelated stage, and replacing the reserved
stage let the native rename publish a foreign directory while returning success.

Choose remedies at the owning boundaries:

- Validate regular-file status on the opened source handle, with nonblocking POSIX open before
  fstat, instead of a pathname check followed by blocking fopen. Hash/decode continue using the same
  immutable acquired bytes; no transactional snapshot claim is added.
- Resolve absolute effect paths before callbacks. Preserve the original admitted spelling for the
  reported result. Retain native object leases for staging and created entries, preventing identity
  reuse while ownership is being checked. Create POSIX staging with owner-only permissions directly.
- Check root, parent and created-object ownership before creating another entry, rereading a written
  slot, preparing validation or entering the commit cutoff. Missing/unverifiable ownership refuses
  further effects; cleanup remains bounded and nonrecursive.
- Reconciliation requires the destination's native directory identity to match the reserved object,
  not merely copied run-record bytes. Known successful native commit remains completed even if
  subsequent inspection fails; ambiguous effects without matching ownership remain unknown.
- Use one complete host validator before and after commit. Native ownership is established by I/O,
  so retire the partial record read and record-first reopening protocol. Complete bundle acquisition
  retains one native root; Windows path-based child access checks its retained binding, including
  observed ancestor relocation, while POSIX child access stays relative to the retained descriptor.

Do not add a filesystem sandbox, recursive cleanup, retry/rollback, migration, path repair or a
second record grammar. Native handles and directory-relative access belong to I/O; the host keeps
bundle meaning and truthful publication classification.

## Separate design challenge

A precheck alone cannot make pathname rename/unlink conditional on an inode/file identifier.
Equally privileged actors can still mutate a namespace between checks and native operations;
ancestors remain trusted, and snapshots of several files are not globally atomic. Preserve those
limits and reject observed replacement rather than claiming universal hostile-filesystem safety.

Challenge copied-content foreign stages, working-directory changes, root/parent replacement,
symlinks/reparse points, special files, moved native readers and object-identity leases. A held lease
must prevent identifier reuse and must not prevent legitimate rename or decoded verification.
Windows metadata leases therefore need compatible sharing and must retain the full supported native
file identifier. Test native effects with deterministic callbacks/checkpoints and thread handshakes,
not timing sleeps. Directory/file tables and native handle retention remain finite.

Verify no writes through a known replaced parent, no success for a foreign stage, no foreign cleanup,
no source reopening after snapshot acquisition and no normalization of admitted/report identity.
Update current contracts and consumers together; obsolete private forms are retired directly.

The challenge separated native publication origin from bundle integrity. Holding the native object
lease permits conclusive completion after an ambiguous rename reply when that object is observed at
the destination. This supersedes the earlier record-first reconciliation assumption without losing
uncertainty when ownership cannot be established. A single host callback validates full integrity;
precommit accepts cancellation, postcommit does not. No additional reader/cache/callback framework
is needed, and the old partial-record API and observation enum are removed without aliases.

## Native references and limits

Windows metadata leases use compatible read/write/delete sharing and full file identity. The
[FILE_ID_INFO contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info)
provides the volume and 128-bit identifier; the
[legacy information contract](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/ns-fileapi-by_handle_file_information)
warns that the 64-bit identifier is not necessarily unique on ReFS. Native handle identity is
in-memory evidence, never persisted provenance or document authenticity.

POSIX descriptors and Windows metadata leases remain live until commit/cleanup is explained.
Unsupported or unavailable native identity fails closed. Changing file contents concurrently can
still yield nontransactional source bytes; hash/decode use exactly the acquired immutable snapshot.
No claim is made about crash durability, every filesystem, process RSS or an uninterruptible native
call's duration. Local and final-head platform checks provide commit-specific evidence.
