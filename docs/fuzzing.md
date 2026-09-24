# Fuzz campaigns and PNG decoding

## One harness declaration

`fuzz/targets.json` owns each harness source, its direct link requirements and input-length bound.
`cmake/FuzzTargets.cmake` creates both engine executables and ordinary corpus replay tests from
those declarations. `tools/fuzz_manifest.py` rejects duplicate keys, unknown fields, unsafe source
paths, unowned harnesses, missing corpus directories, nonregular inputs and oversized seeds.
The manifest does not grant permission to advertise a product method or image format.

A new harness needs one declaration, its implementation, seeds and regressions. There is no
second source/link list or hand-maintained harness count. CMake reconfigures when the manifest or
campaign policy changes. The normal build still compiles, lints and replays every harness.

## Bounded, complete execution

`tools/run_fuzzers.py` runs one target. Durations must be positive and at most 1,800 seconds;
zero never means unlimited execution. Arbitrary engine options and implicit `--merge` promotion
are not accepted. The manifest can explicitly request full input-length exploration for a slow
harness. Both engines use the declared maximum input length and a per-input timeout.

`tools/run_fuzz_campaign.py` coordinates the configured child CTest tree. It checks that every
manifest target is registered exactly once, with the expected runner, identity and duration.
Disabled or skippable harnesses are rejected. It explicitly passes the worker count to that child
CTest invocation; outer CTest `--parallel` is not mistaken for child parallelism.

`DE_FUZZ_JOBS` defaults to two and is bounded to one or two. Campaign timeout is computed from the
actual target count, full execution waves, per-target watchdog time and reporting allowance. The
nightly workflow checks the plan against its available campaign budget before starting builds;
a growing target set cannot silently omit work or overrun a hand-maintained aggregate timeout.

The runner bounds its engine subprocess independently of CTest. Each engine owns a separate process
group on POSIX. Timeout and interruption paths terminate that owned group and retain failed state.
A hard runner/host kill can leave an incomplete report; incomplete evidence cannot pass.

A passing campaign requires CTest success AND exactly one passing report per expected target.
Each report requires engine exit zero, a positive native execution count, at least the requested
engine wall time, and no saved findings. Early successful exits are incomplete runs.
Missing statistics, zero executions, missing/duplicate reports, timeouts and saved crashes/hangs
are failures. JUnit and CTest logs are retained as evidence; target reports establish completion
without trusting a decorative PASS line or treating skipped tests as successes.

## Evidence and corpus ownership

Each invocation exclusively creates a new run directory below the requested work parent.
Nothing recursively deletes, reuses or replaces a prior run directory. Seeds and regressions are
copied by SHA-256 content identity, with every original path and size recorded in `corpus-index.json`.
Different bytes with the same filename remain distinct. Equal bytes may deduplicate, but both
origins stay recorded. Symlink and nonregular corpus entries are rejected rather than followed.

Reports retain the binary and manifest hashes, command arguments, requested duration, execution
count, engine exit, elapsed time and finding paths. Full engine logs and actual reproducers remain
beside the reports. The inherited environment and credentials are never exported. Review any new
finding, minimize it deliberately, then commit the regression; a successful campaign never edits
its own repository corpus. Both engines run short required PR campaigns and longer nightly
campaigns. CI packages the evidence as a tar archive because AFL++ filenames contain colons that
the artifact uploader cannot accept directly. Artifacts are retained for seven days, including
failed runs.
Evidence is commit/binary-specific, not a promise that a future version has no defects.

## Shared PNG decode boundary

`io::load_grayscale_png` checks and opens the regular input file, measures it, and supplies a
bounded reader. `io::decode_grayscale_png` supplies a borrowed byte span. Both call the same
internal decoder, allocator callbacks, strict CRC handling, format admission, stored-sample
expansion and Adam7 assembly. The file reader cannot read beyond its measured encoded-byte bound
if a file grows after admission. This is not a snapshot or a filesystem sandbox.

`PngLimits` can tighten, never disable or relax, the production limits of 128 MiB encoded input and
40 million pixels. Harness limits are much smaller, with a finite shared image/codec budget.
Reader state, file handles, planes and allocation owners live outside libpng's jump frames.
The callback returns from its read operation before raising a C codec error; no jump crosses a
live nontrivial C++ owner. Every success/refusal path is checked for allocation refunds.

The `png_decode` harness exercises arbitrary encoded input twice, checking deterministic acceptance,
samples and errors, then tries a smaller allocation budget. The independent `png_samples` harness
constructs valid grayscale PNGs with CRCs, Adler checksums and stored DEFLATE blocks, without using
libpng's encoder. It exercises all five scanline filters and checks every sample for each supported
bit depth and Adam7, including
narrow images with omitted passes. Production validation is never disabled to obtain coverage.
These are complementary oracles: structured inputs reach successful decode paths; raw inputs
exercise malformed headers, compressed streams, truncation and strict rejection behavior.

The isolated fuzz build instruments the actual pinned libpng and zlib C archives with ASan,
UBSan and the selected coverage engine. Before a campaign, the imported archive paths are checked
for sanitizer/coverage symbols and hashed. Native sanitizer builds still make only their existing
first-party instrumentation promise; this change does not instrument every planned dependency.
The CLI harness still does not link the production host or codec layer. Only codec harnesses gain
byte-span decoding, and none of them loads paths or publishes outputs.

## Verification and limits

Tooling tests include same-name seed/regression collisions, malformed declarations, zero-duration
rejection, zero-work and missing-report negative controls, timeouts, and a real child-CTest barrier
that distinguishes serial from parallel execution. Synthetic runner evidence is used only to test
the coordinator; actual engine campaigns and image tests are separate native checks.

Native tests anchor fixture CRC correctness, every supported depth/filter/interlace combination,
truncation, invalid/tighter limits and allocation refusal. Existing real-file/CLI tests continue
through the shared decoder. Fuzzing finds counterexamples, not proofs: a green bounded run does
not certify every PNG or make the memory budget a process-RSS limit. libFuzzer has an RSS ceiling;
AFL++ uses ASan-compatible address-space settings, finite harness allocations and job watchdogs,
not an equivalent process-RSS enforcement claim.

The `cancellation` harness chooses deterministic stop checkpoints for B02/B03, box means and the
production PNG byte decoder. It sends no OS signals and performs no filesystem publication. Completed
results retain their numerical/sample oracles; cancellation must return its typed result and refund
scratch/codec allocations. Native interrupt and commit-cutoff behavior have separate CTest coverage.
