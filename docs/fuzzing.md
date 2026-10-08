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

`DE_FUZZ_JOBS` defaults to two and admits one through four. Select four only with sufficient
CPU and memory: four libFuzzer workers can each reach its 2 GiB RSS ceiling, requiring
headroom beyond 8 GiB for runtimes and the host. AFL++ has no equivalent RSS cap. Campaign timeout is computed from the
actual target count, full execution waves, per-target watchdog time and reporting allowance. The
nightly workflow checks the plan against its available campaign budget before starting builds;
a growing target set cannot silently omit work or overrun a hand-maintained aggregate timeout.

The runner bounds its engine subprocess independently of CTest. Each engine owns a separate process
group on POSIX. Timeout and interruption paths terminate that owned group and retain failed state.
A hard runner/host kill can leave an incomplete report; incomplete evidence cannot pass.

A passing campaign requires CTest success AND exactly one passing report per expected target.
Each report requires engine exit zero, a positive native execution count, at least the requested
engine wall time, and no saved findings. The coordinator independently rechecks duration, engine,
actual binary/manifest identities and complete executed JUnit results, rather than trusting the
runner-provided passed flag. Early successful exits are incomplete runs.
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

`io::load_grayscale_png` acquires and hashes a bounded immutable snapshot of the regular input
file, then supplies those same bytes to `io::decode_grayscale_png`. File and borrowed-span inputs
therefore share allocator callbacks, strict CRC handling, format admission, stored-sample
expansion and Adam7 assembly. A changed file size during acquisition is refused. The snapshot
keeps hashing and decoding consistent; source paths are not a filesystem sandbox.

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


The denoising harness invokes actual pinned CV_16U/L1 execution, compares global halos against a
whole native call, checks correction identity and resource refunds. Isolated fuzz builds instrument
OpenCV core/photo/imgproc alongside the codec archives; imported archive symbols establish actual
ASan/UBSan and coverage presence. The required PR workload budget is 2400 seconds for the expanded
complete target set. Native assembly remains outside the instrumentation claim.

Archive sanitizer/coverage symbols establish their presence in the actual imported archives.
Build flags and locked sources support the instrumentation configuration; neither symbol presence
nor a passing campaign proves that every object, assembly instruction or possible input was covered.

## Bounded TIFF decoding

The `tiff_decode` harness feeds malformed classic/BigTIFF snapshots through production directory,
strile and native codec admission with tighter byte/pixel limits and a finite charged budget.
It checks decoded/source agreement and allocation refunds on acceptance and refusal. Independent
8/16-bit fixtures test exact samples separately; see [TIFF processing](tiff-processing.md).

The TV-L1 harness consumes dimensions and 16-bit sample selectors, compares simultaneous
reference iterations with the production full-field dual/primal passes, and checks finite bounds.
Its additional execution wave retains the full per-target PR budget and watchdog allowances.

The contrast harness compares interruptible sample ordering and nearest ranks to independent
sorting and scalar levels/gamma formulas. It extends the manifest to 20 targets, ten waves
at the default two jobs or five at four jobs. Every target retains 60 seconds of engine work
and the same watchdog allowances.

C03 additionally fuzzes charged contextual preparation, floating reconstruction, immutable replay
and complete observation validation. CLI corpus seeds exercise grid admission and inactive private
options. Deterministic unit probes enumerate preparation and application cancellation checkpoints.

The S01 sharpening harness adds bounded Gaussian preparation, scalar soft-threshold comparison,
immutable reconstruction replay and complete report validation. Twenty-one targets retain every
per-target exposure interval; the campaign plan derives the required wave count and rejects an
insufficient whole-job budget. CLI seeds cover sharpening admission and inactive private options.

The `otsu` harness compares fitted B01 observations against independently enumerated byte
populations for every candidate bin, then checks exact output polarity and scratch refunds.
Corpus fixtures exercise single-bin fallback, empty-bin tie plateaus and skewed populations.
