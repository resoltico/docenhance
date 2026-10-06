# JPEG source admission and interpretation

JPEG is an input container, not a new enhancement method. Continuous `preserve`/`gray` processing
admits the subset below and uses the existing color, oriented protection, opt-in I01, PNG encoding,
decode-back comparison and bundle publication path. There is no JPEG output. `bw` remains B02/B03's
stored-grayscale PNG operation; JPEG on that branch returns `E_NOT_IMPLEMENTED`, exit 4,
`not_started`. Color/profile override never changes this operation/format matrix.

## One identified source

The file adapter opens the source read-only and acquires one bounded encoded snapshot. Size/read/EOF
checks, cancellation and hashing concern those same bytes. Signature dispatch selects PNG, JPEG or TIFF;
extensions do not govern decoding and there is no decoder retry. The PNG-only and protection APIs
retain their domains. The source is preserved. Acquisition is not a transactional snapshot of an
arbitrarily concurrently edited file or a hostile-filesystem sandbox.
Regular-file status belongs to the opened native handle, including when a source link resolves to
a regular file. Name replacement after opening does not redirect acquisition; POSIX type admission
uses nonblocking open before fstat. Protection masks use the same acquisition boundary.

`Raster` holds decoded integer samples and shared ICC/orientation/resolution observations.
Container declarations are a closed PNG/JPEG/TIFF alternative: PNG color chunks retain their original
rules, JPEG retains its component interpretation, and [TIFF](tiff-processing.md) records sample
association and MINISWHITE separately. The color adapter alone applies ICC and
linear-light interpretation. JPEG's native YCbCr-to-RGB expansion is not ICC conversion or
linearization. Shared EXIF handling reads bounded IFD0 orientation/resolution only; it does not
traverse GPS, maker notes, thumbnails or later IFDs. PNG pHYs precedence is unchanged.

## Admitted JPEG codestreams

A source has one complete 8-bit Huffman DCT image: SOF0 baseline or SOF2 progressive. Gray has one
component with 1x1 sampling. Three-component RGB has 1x1 sampling for each component. YCbCr has
1x1 Cb/Cr and integral Y factors 1..4 in each direction, with no more than 10 blocks per MCU.
Fixtures cover every admitted integral sampling combination and partial MCU edges.

Physical dimensions are positive, no larger than the pinned decoder's 65,500 per-axis maximum,
and at most 40,000,000 pixels. Encoded input is at most 128 MiB. Coding processes other than SOF0/
SOF2, arithmetic/lossless coding, higher precision, CMYK/YCCK and unknown component interpretation
are explicit `E_INPUT` refusals (exit 3, `not_started`). There is no reduced-resolution decode, precision fallback or first-image
selection. An explicit 16-bit PNG output is subsequent representation precision, not recovered
JPEG information.

A bounded framing/entropy scan precedes native pixel allocation. It checks marker extents,
component identities/sampling, a single frame, scan count, final EOI and absence of trailing bytes.
Table/scan/entropy semantics are additionally checked by the native decoder, whose warnings are
errors. Missing EOI, damaged entropy and native recovery are not successful processing. There are
at most 65,536 framing markers after SOI, 128 scans and 8 MiB cumulative APP/COM payload. Restart/
stuffed entropy bytes remain bounded by the encoded ceiling. API limits can only tighten production
limits. Long scans and marker fill observe cancellation in bounded 64 KiB intervals.

The decoder policy `jpeg-islow-fancy-no-smoothing-1` fixes full-resolution scale 1/1, accurate
integer `JDCT_ISLOW`, fancy chroma upsampling where the pinned library supports it (its integral
expansion elsewhere), no progressive block smoothing, and explicit three-channel RGB order.
Gray remains one channel. Progressive output consumes every scan and requires DC information for
all components, followed by successful native completion. A complete codestream may deliberately
omit AC coefficients or refinement; no intermediate preview is published. SIMD is the explicitly
configured upstream implementation of this policy, not a speed-driven change of IDCT/scale.
Independent coefficient references permit a one-unit integer-IDCT difference from floating cosine
rounding; baseline/progressive equivalence and scalar/SIMD sample comparisons are separate checks.

## Metadata acceptance

JFIF establishes gray/YCbCr; Adobe transform 0 establishes RGB (or gray for one component), and
transform 1 establishes YCbCr. Contradictory JFIF/Adobe declarations fail. Without these markers,
three-component IDs `R,G,B` mean RGB and `1,2,3` mean YCbCr; other interpretations fail. Component
IDs are unique and recorded, alongside process, sampling, precision and decoder policy.

Known interpretation markers must precede the first image scan. Duplicate JFIF, Adobe or EXIF
and malformed recognized signatures/extents are refused. Unknown ancillary APP/COM data is bounded
and omitted. MPF multi-image declarations, APP11/JUMBF, ISO 21496 gain-map signatures, recognized
Adobe HDR/container XMP and extended XMP are unsupported; they are never silently interpreted as
an ordinary first image. This is a specified recognition policy, not arbitrary metadata traversal.

JPEG ICC APP2 segments require consistent nonzero numbering/count, unique indexes, nonempty data,
complete coverage and at most 4 MiB assembled bytes. Numbering order need not be physical order.
The compatible RGB/gray profile enters the existing bounded Little CMS path. `srgb` override may
ignore profile semantics but never repairs inconsistent ICC framing, component models, orientation
or container structure. No second native ICC conversion occurs.

Valid physical EXIF resolution takes precedence over valid physical JFIF density. The record retains
both physical declarations and whether they disagree. Unitless density is not DPI. Missing/nonphysical
EXIF permits JFIF fallback; malformed selected metadata does not. JPEG EXIF resolution fields must
form a positive pair; shared TIFF unit defaults retain their defined interpretation. Physical values
are rounded once to positive uint32 pixels per meter. Orientation 1..8 is applied exactly once in
the existing gather operation, which swaps physical axes when transposed. Protection is supplied in
these already-oriented coordinates. Original metadata is stripped from output except the supported
canonical PNG ICC profile and selected physical resolution.

## Native ownership, resources and cancellation

Libjpeg's `max_memory_to_use` is advisory for virtual arrays, not a total-allocation guarantee.
The production adapter replaces its public memory-manager methods immediately after native creation.
Every subsequent native small/large block, aligned sample row, pointer table and coefficient array
is a real `core::Buffer` charged to the request's existing 1 GiB continuous budget. Virtual arrays
are fully allocated/charged before entropy work, never backed by temporary files. Fixed tables admit
1024 live blocks and eight controls of each virtual-array kind. Exhaustion is a resource failure,
not a request to simplify the image.

Before native creation the adapter charges a conservative 64 KiB bootstrap reservation. The reviewed
pinned 3.2.0 creation path allocates only the memory manager and permanent marker/input/master
controllers before reading input; this reservation bounds that small upstream-owned bootstrap.
Those controllers retain the original allocator until destruction. It is deliberately distinguished
from measured post-bootstrap block allocations. Fixed C++ control tables, standard stream/string
bookkeeping and OS resources are not a claim of charged process RSS.

A native-call wrapper contains only borrowed/trivial views and scalar state. Raster/profile/buffer
owners live outside its nonlocal-jump frame. Allocation helpers complete exception handling and
destroy temporary owners before a jump; no C++ exception crosses a native callback. Jump-capable
callbacks deliberately have no `noexcept` specification: Windows CRT long jumps use stack-unwind
semantics, and an optimizer-dependent `noexcept` boundary can terminate instead of returning the
typed cancellation result. Nonjumping allocation/free helpers retain their ordinary guarantees. Destruction
releases both charged pools and the upstream bootstrap on success or failure. Native diagnostics
never print or terminate the program. Memory/scan refusal, cancellation and input corruption retain
distinct typed outcomes; real observed failures take precedence over a pending stop.

Cancellation reaches framing, metadata assembly, native input consumption, progress/scan work,
allocation/initialization and output rows. A bounded native/I/O operation can delay observation.
The existing final precommit cutoff, worker joins, immutable I01 model and truthful publication
states remain unchanged. Decoder state is destroyed before conversion/I01; peak charged storage
still includes the encoded snapshot, profiles, full oriented-capable integer raster, subsequent
processing rows/model and the separately bounded bundle-verification workspace. Measurements of
charged allocations, peak RSS and runtime are different evidence, never interchangeable limits.

## Persistent and response compatibility

New production records use format version 6 and require a closed `source.decoding` PNG/JPEG/TIFF
alternative. Response schema version 6 exposes the same mapping as `source_decoding` and reports an
explicit input/output-mode matrix. A custom processing port can report unknown source observations
as null; the native host always supplies them. Numerical method versions are unchanged.

Only the current record format is supported. Obsolete versions are refused without migration.

## Evidence and developer commands

Independent first-party JPEG fixtures specify Huffman tables and DCT coefficients without a native
encoder. Tests cover baseline/progressive gray/RGB/YCbCr, all admitted sampling layouts, partial MCUs,
cosine/color references, profiles/metadata conflicts, all eight orientations, masks, equivalent
PNG/JPEG downstream I01, version compatibility, truncation, resource refusal and deterministic
cancellation/refunds. Native package smoke processing exercises a progressive JPEG after relocation.
The manifest-declared JPEG fuzzer uses the production adapter and actual JPEG archive; isolated fuzz
builds verify C-code ASan/UBSan/coverage symbols in JPEG, PNG, zlib and Little CMS. Upstream assembly
is not thereby claimed to be sanitizer-instrumented.

`python tools/measure_jpeg_resources.py --probe out/release/app/tests/de_jpeg_resources --executable
out/release/app/bin/docenhance --out .cache/jpeg-resources/results.json` constructs explicit
3/12/24 MP coefficient cases and observes fresh processes on macOS/Linux. It separately records real
post-bootstrap native allocations, conservative decoder charges, total working charges, peak RSS
and wall time, plus a complete I01 processing run. Synthetic coefficient pages are resource evidence,
not empirical readability/fidelity benchmarks. Actual passing platforms and measurements belong to
the tested commit/logs; an authored workflow alone is not proof.
