# Verification independence and failure detection

## Design pass

Keep independent numerical references, real executable/publication tests, strict discovery and
fresh results. Important guarantees need counterexamples their actual checks reject, rather than
another copy of a capability flag, test inventory or completion claim.

Concrete gaps at the verification owners:

- Native compilation evidence checks source presence but not effective sanitizer options. A copy
  of the actual sanitized compilation database with numeric.cpp opting out through
  `-fno-sanitize=all` is accepted. Check every application compilation command against requested
  sanitizer modes, including option ordering and recovery, and apply that check to isolated fuzz
  builds too. Archive symbols still establish native-library instrumentation presence, not every
  upstream object or assembly instruction.
- Sanitizer builds need a real benign/error process control compiled with the same project options.
  Require the selected runtime to diagnose memory, undefined arithmetic and unsynchronized writes;
  a nonzero exit alone could be a different failure. Retain diagnostics without turning expected
  crashes into CTest WILL_FAIL escapes.
- Color fuzz harnesses can silently ignore unexpected converter failures and only compare repeated
  rows. Refuse unexpected failure classes and exercise a bounded valid structured raster against an
  independently written scalar sample/alpha/orientation/quantization reference. Keep raw PNG/ICC
  mutations as separate malformed-input coverage.
- Symlink refusal/cleanup cases can return early when setup fails while retaining enough unrelated
  assertions to pass. Make required fixture creation fail visibly. Windows native verification must
  provide symbolic-link creation capability; document this test prerequisite rather than reporting
  an unexercised property as passed.

No product processing behavior, method, wire version, dependency, compatibility reader or migration
is introduced. Keep the existing CTest/manifest discovery owners and the existing native boundary.

## Separate design challenge

A sanitizer probe alone does not prove every production source is instrumented; effective command
inspection and real runtime fault detection protect different properties. An uninstrumented or
recovery-enabled negative control must fail, and benign probes must succeed. TSan races use thread
handshakes, not sleeps. Optimizations and library hardening must not erase or mask the intended
fault; require the actual sanitizer diagnostic category.

A repeated production color calculation is a determinism check, not numerical correctness.
Structured scalar inputs avoid PNG CRC admission barriers and invalid native-profile assumptions;
raw ICC remains an input/resource refusal domain. Challenge a wrong sample producer and an unexpected
conversion error independently. Reference mathematics must not call production transfer or orientation
helpers. Preserve finite harness allocations and corpus replay on every native platform.

Discovery cannot establish that every assertion is meaningful. Retain the existing real skip,
disable, hidden/expected-failure, missing/duplicate result and package-byte negative controls; do
not build a general assertion-count framework to substitute for property-specific evidence. Platform
exclusions stay explicit: POSIX FIFO behavior and Windows handle semantics are different contracts,
Windows sanitizer execution is unavailable in the supported presets, and native dependencies are
instrumented by isolated fuzz builds rather than first-party native sanitizer builds.

## Challenge evidence

The original native compilation checker accepted a numeric.cpp command with
`-fno-sanitize=all`. The corrected check rejects this and recovery overrides; current native
ASan/UBSan, TSan and isolated-fuzz compilation databases satisfy it. A real native probe without
instrumentation fails all three sanitizer-detection claims rather than passing merely by exiting
or crashing. Required link cases and existing raw-profile/PNG corpus replay pass locally without
the former early returns. Structured seeds cover every orientation, both source depths, transfer,
matte and output choices without needing a valid compressed container.

Real ASan/UBSan and TSan probe binaries diagnose the intended heap read, signed overflow and
handshake-driven race while benign execution succeeds. Original color harnesses accepted a temporary
producer that returned an invariant refusal for normal orientation and otherwise flipped the first
row byte deterministically. Revised raw ICC failure checks and independent structured color samples
reject those defects respectively. Production source is restored; the control inputs remain in the
existing orientation PNG and gray ICC corpora.

Final challenge also rejects compiler sanitizer ignorelists, including the compiler's obsolete
option spelling: required flags alone do not establish coverage when an ignorelist excludes a
source. Native and both isolated engine compilation/archive checks are repeated under the final
guard; engine binaries and campaigns are unchanged by this tooling correction.
