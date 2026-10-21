# Fuzz campaigns and native decoding

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
After a failed harness, CTest stops launching new targets; already running targets finish and
the incomplete campaign remains failed with its findings and diagnostic evidence retained.

For local iteration after a complete fuzz configuration and dependency build, rebuild one changed
harness and its actual linked libraries with `cmake --build out/fuzz/app --target de_fuzz_cli --parallel 2`.
Run it against the committed seeds and regressions with `python tools/run_fuzzers.py --target cli --binary out/fuzz/app/fuzz/de_fuzz_cli --work out/fuzz/app/fuzz-work --seconds 5`.
Use the corresponding `fuzz-afl` build and `--engine afl` for AFL++. This focused run creates
its own evidence and is a fast diagnostic, not a complete campaign or a substitute for the
required full workflow when CLI parsing changes.

`DE_FUZZ_JOBS` defaults to two and admits one through four. Select four only with sufficient
CPU and memory: four libFuzzer workers can each reach its 2 GiB RSS ceiling, requiring
headroom beyond 8 GiB for runtimes and the host. AFL++ has no equivalent RSS cap. Campaign timeout is computed from the
actual target count, full execution waves, per-target watchdog time and reporting allowance. The
weekly workflow checks the plan against its available campaign budget before starting builds;
a growing target set cannot silently omit work or overrun a hand-maintained aggregate timeout.

The runner bounds its engine subprocess independently of CTest. Each engine owns a separate process
group on POSIX. Timeout and interruption paths terminate that owned group and retain failed state.
A campaign interruption also unwinds its child CTest group. Surviving cooperating nested runners receive
bounded grace to clean their detached engine groups, even if CTest exits first. Remaining group
members are then killed and the direct child reaped. Windows cleanup covers only the direct child.
Other independently detached descendants are outside this process-group cleanup guarantee.
A hard runner/host kill can leave an incomplete report; incomplete evidence cannot pass.

A passing campaign requires CTest success AND exactly one passing report per expected target.
Each report requires engine exit zero, a positive native execution count, at least the requested
engine wall time, and no saved findings. The coordinator independently rechecks duration, engine,
actual binary/manifest identities and complete executed JUnit results, rather than trusting the
runner-provided passed flag. AFL reports additionally retain its native `run_time`, a nonnegative integer that must reach the
requested duration independently of process wall time. This clock excludes initial calibration;
calibration time never supplies missing active-run exposure. libFuzzer reports require exactly one
valid nonnegative `INITED` callback count in the retained native log and a strictly larger final
execution count. This establishes post-initialization callbacks, not mutation-only seconds:
libFuzzer's `max_total_time` includes seed initialization. Missing or ambiguous retained markers
fail closed; neither coverage nor newly added corpus units need increase. Early successful exits are incomplete runs.
Missing statistics, zero executions, missing/duplicate reports, timeouts and saved crashes/hangs
are failures. JUnit and CTest logs are retained as evidence; target reports establish completion
without trusting a decorative PASS line or treating skipped tests as successes.

AFL++ runs with an owned set of runtime `AFL_*` variables, clearing inherited engine overrides.
Startup seed crashes or timeouts fail the run instead of silently removing retained regressions
from the active queue. Startup refusals preserve the engine exit and log location separately from
missing statistics; available finding files remain listed even when statistics cannot be read.

## Evidence and corpus ownership

Each invocation exclusively creates a new run directory below the requested work parent.
Nothing recursively deletes, reuses or replaces a prior run directory. Seeds and regressions are
copied by SHA-256 content identity, with every original path and size recorded in `corpus-index.json`.
Different bytes with the same filename remain distinct. Equal bytes may deduplicate, but both
origins stay recorded. Symlink and nonregular corpus entries are rejected rather than followed.
Staging admits at most 4,096 origins and 32 MiB of distinct input bytes. Before a standalone run
starts, its work parent must have room for another 256 MiB within a 1.5 GiB threshold. A campaign
reserves 256 MiB for each of up to four concurrent targets, leaving at least 512 MiB within that
threshold for retained earlier runs. During execution, allocated work is checked every five seconds;
exceeding 256 MiB for one run, 1.5 GiB for the work parent, or 49,152 entries stops the owned engine and records an
incomplete run without deleting findings. These are cooperative stop thresholds, not hard disk quotas:
growth between checks or a host kill can overshoot them. A filesystem quota or bounded ephemeral
volume is required for a hard limit. Build trees, dependency caches and separately packaged evidence
are outside the work-parent threshold and are never silently pruned.

The 256 MiB per-run threshold leaves room beyond the admitted 32 MiB staged inputs for
engine queues, findings, allocation blocks and diagnostics. Four workers reserve 1 GiB; the
1.5 GiB parent threshold leaves 512 MiB for earlier evidence. These are operational allowances,
not experimentally proven minimums. The entry ceiling admits four 4,096-origin staged corpora
plus 32,768 generated/report entries, and stays below the archive's 50,000-entry limit.
Smaller limits can terminate valid exploration
sooner, while larger limits consume more disk without increasing the requested time budget.

Packaging admits at most 1.5 GiB of logical source bytes and 50,000 logical entries including
its manifest. It admits the source inventory before reading campaign metadata, visits entries
incrementally and hashes the archive as a stream. It refuses archive destinations inside its
input trees. Plan for 1.5 GiB of source plus the actual existing archive size and at most
2 GiB for the replacement temporary archive: 5.5 GiB when the existing archive also fits the
current 2 GiB cap. Older or foreign archives may be larger and remain preserved until replacement.
Filesystem metadata, cooperative engine overshoot, build products and dependency/tool caches are extra. Sparse files can fail the logical archive
limit even when their allocated size passes the engine threshold. Keep one output archive per
build; moving copies elsewhere creates separately owned retention. Overshoot or unavailable
space fails packaging and cannot manufacture passing evidence.

Reports retain the binary and manifest hashes, command arguments, requested duration, execution
count, engine exit, elapsed time and finding paths. Engine logs retain up to 1 MiB of their first and
last output with an omitted-byte count; actual reproducers remain
beside the reports. After the owned engine processes join, a completed run with no findings
removes only its own staged/generated corpus and AFL queue scratch. The seed-origin index,
statistics, logs and result records remain; committed seeds and regressions are never removed.
Saved crashes or hangs stop the owned engine at the next periodic check instead of spending the remaining target time; the run remains failed. Failed or incomplete engine runs retain their scratch for investigation. Every cleanup candidate
and ancestor is checked before removal; links and special files are refused. Cleanup errors fail
the run and preserve records and any remaining scratch; an unlink failure can leave partial
cleanup. Earlier run directories are never automatically pruned.
The inherited environment and credentials are never exported. Review any new
finding, minimize it deliberately, then commit the regression; a successful campaign never edits
its own repository corpus. Both engines run required 15-second PR targets and 300-second weekly targets. Weekly
manual runs may select up to 1,800 seconds per target. Shorter runs explore fewer inputs; complete corpus replay,
harness coverage and strict evidence admission remain required. The manifest-derived `--plan`
output lists every target and computes the complete timeout from the selected concurrency and duration. CI packages the evidence as a tar archive because AFL++ filenames contain colons that
the artifact uploader cannot accept directly. PR artifacts are retained for seven days, and weekly
campaign artifacts for 30 days, including failed runs. Each archive includes a manifest binding
the engine and exact source commit; absent or incomplete campaigns are explicitly labeled rather
than receiving synthetic empty directories. Missing required upload bytes fail CI. Saved crash
inputs are replayed once under the already instrumented binary with a bounded watchdog and
captured diagnostics, without making a failed campaign green if a crash is nonreproducible.
The original inputs and engine evidence remain preserved separately from the diagnostic replay.
An archive manifest is a provenance and completeness assertion, not independent proof
of program correctness; the strict coordinator and per-target execution reports remain
authoritative. Verification failures stay fatal even if replay cannot reproduce them.
Evidence is commit/binary-specific, not a promise that a future version has no defects.

## Shared PNG decode boundary

The filesystem layer acquires and hashes a bounded immutable snapshot of the regular input
file, then supplies those same bytes to `codec::decode_png_raster`. File and borrowed-span inputs
therefore share allocator callbacks, strict CRC handling, format admission, channel/depth sample
expansion and Adam7 assembly. A changed file size during acquisition is refused. The snapshot
keeps hashing and decoding consistent; source paths are not a filesystem sandbox.

`PngLimits` can tighten, never disable or relax, the production limits of 128 MiB encoded input and
40 million pixels. Harness limits are much smaller, with a finite shared image/codec budget.
Reader state, file handles, planes and allocation owners live outside libpng's jump frames.
The callback returns from its read operation before raising a C codec error; no jump crosses a
live nontrivial C++ owner. Every success/refusal path is checked for allocation refunds.

The `png_decode` harness exercises arbitrary encoded input twice, checking deterministic acceptance,
typed channel/depth shape, every logical sample byte and errors, then tries a smaller allocation budget. The independent `png_samples` harness
constructs valid grayscale PNGs with CRCs, Adler checksums and stored DEFLATE blocks, without using
libpng's encoder. It exercises all five scanline filters and checks every sample for each supported
bit depth and Adam7, including
narrow images with omitted passes. Production validation is never disabled to obtain coverage.
These are complementary oracles: structured inputs reach successful decode paths; raw inputs
exercise malformed headers, compressed streams, truncation and strict rejection behavior.

The isolated fuzz build instruments the actual pinned libpng, libjpeg-turbo, libtiff, zlib and Little CMS C archives with ASan,
UBSan and the selected coverage engine. Before a campaign, application compilation commands are checked against requested fatal sanitizer modes, and the imported archive paths are checked
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

## Continuous-tone and profile harnesses

`png_continuous` exercises bounded raw PNG metadata/pixel decoding and the production row converter,
with 4,096 pixels and 65,536 encoded bytes as tighter input limits. It repeats integer rows and
checks complete budget refunds. It tests both embedded interpretation and explicit sRGB override,
without filesystem writes. `color_profile` feeds raw ICC bytes directly to the native adapter on
tiny gray/RGB rasters, so profile mutations need not survive an unrelated PNG CRC first. Both use
first-party, generated corpus profiles/images. Unexpected conversion/row failures are findings;
only named input/resource refusals belong to malformed input. Continuous fuzzing also constructs
bounded valid RGBA8/16 rasters and compares every oriented gray/RGB8/16 sample with an independent
scalar transfer/compositing/quantization reference. This path needs no valid PNG CRC, exercises
black/white matte and all orientations, and must succeed within its finite budget. Raw PNG and
ICC parsing remain separate mutation paths. `fuzz-codecs.json` identifies the selected actual
instrumented native archive closure; the campaign rejects a missing ASan, UBSan or coverage signature.

The `jpeg_decode` target exercises raw framing, metadata and the production decoder, with real
charged allocations and refund/source-descriptor oracles. Complete independently constructed
coefficient fixtures and malformed/truncated regressions seed it. The isolated build additionally
instruments the actual imported JPEG archive and checks its ASan/UBSan/coverage symbols; upstream
assembly is not claimed to be instrumented. The target manifest owns input bounds and campaign
workload admission, so the added target must fit the configured campaign budget.


The denoising harness invokes the first-party binary64 NLM kernels and checks independent
window references, correction identity, protection and resource refunds. Isolated fuzz builds
instrument the selected codec/color archives; imported archive symbols establish actual
ASan/UBSan and coverage presence. The required PR workload budget is 2400 seconds for the
complete target set. Native assembly remains outside the instrumentation claim.

Archive sanitizer/coverage symbols establish their presence in the actual imported archives.
Build flags and locked sources support the instrumentation configuration; neither symbol presence
nor a passing campaign proves that every object, assembly instruction or possible input was covered.

## Bounded TIFF decoding

The `tiff_decode` harness feeds malformed classic/BigTIFF snapshots through production directory,
strile and native codec admission with tighter byte/pixel limits and a finite charged budget.
It checks decoded/source agreement and allocation refunds on acceptance and refusal. Independent
8/16-bit fixtures test exact samples separately; see [TIFF processing](formats/tiff.md).

The TV-L1 harness consumes dimensions and 16-bit sample selectors, compares simultaneous
reference iterations with the production full-field dual/primal passes, and checks finite bounds.

The contrast harness compares interruptible sample ordering and nearest ranks to independent
sorting and scalar levels/gamma formulas.

C03 additionally fuzzes charged contextual preparation, floating reconstruction, immutable replay
and complete observation validation. CLI corpus seeds exercise grid admission and inactive private
options. Deterministic unit probes enumerate preparation and application cancellation checkpoints.

The S01 sharpening harness checks bounded Gaussian preparation, scalar soft-threshold comparison,
immutable reconstruction replay and complete report validation. CLI seeds cover sharpening admission
and inactive private options.

The `otsu` harness compares fitted B01 observations against independently enumerated byte
populations for every candidate bin, then checks exact output polarity and scratch refunds.
Corpus fixtures exercise single-bin fallback, empty-bin tie plateaus and skewed populations.

R01 restoration fuzzing executes the native full-complex FFT on a tiny globally reflected canvas.
A periodic analytic cosine is an eigenfunction of the admitted reflected padding: an asymmetric
normalized PSF has a closed complex Fourier coefficient, so phase, conjugation, DC restoration and
blend are checked independently without importing the production transform or a second FFT engine.
Immutable replay must reproduce exact transported rows without another native call, and destroying
the prepared context must refund charged payloads. The kernel is supplied as test coefficients;
the harness performs no source-file I/O.


## Sharpening sRGB-boundary regression

The independently computed sharpening reference is perceptual, but reconstructed source
samples are linear RGB. The committed AFL++ regressions
exercise the discontinuity at the published piecewise sRGB transfer threshold.
The oracle decodes the expected perceptual candidate to linear space (retaining
exact input for a no-op) and compares all three RGB channels. No production transfer
constants or numeric tolerances may be relaxed. On mutation, replay, field/refund checks
and direct two-dimensional Gaussian verification remain required.
