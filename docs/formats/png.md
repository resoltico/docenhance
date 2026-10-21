# PNG representation and color processing

This contract defines PNG input, shared color/gray interpretation and canonical continuous/binary
PNG output.
[JPEG admission](jpeg.md) and [TIFF admission](tiff.md) feed the same
interpretation and output path through separate container decoders. The default applies no
enhancement filter. Explicit stages follow geometry, illumination, denoising, restoration,
contrast, sharpening and final quantization; see [current CLI behavior](../cli.md).
This representation contract does not define denoising, sharpening, restoration or classification
mathematics. The terminal Q16 B01/B02/B03 contract belongs to [binarization](../methods/binarization.md). Preserve original documents: color conversion,
alpha flattening, grayscale conversion, and requested quantization can discard information.

## Output intent and admission

`process INPUT --out-dir DIRECTORY` defaults to `--output-mode preserve`: retain the decoded color
versus grayscale category, not the original ICC space, file bytes, transparency or arbitrary metadata.
`--output-mode gray` produces encoded relative luminance. By default both modes perform only the interpretation,
orientation and representation operations specified here. I01 and [D01 denoising](../methods/denoising.md#d01-bounded-nlm) are separately opt-in; there is no hidden enhancement preset.

`--output-mode bw` selects terminal binary classification, defaulting to Sauvola when `--binarize`
is absent. It accepts every source admitted by the PNG/JPEG/TIFF decoders, with the same color/gray
interpretation, alpha policy, G01/G02 geometry, protection and selected eager photometric stages as
the other output modes. It quantizes final luminance once to `Q=floor(65535*E(Y)+0.5)`, then classifies
logical black 0/white 1 directly into physical grayscale PNG1. There is no 255 bridge. Protection
preserves entering photometric values; every destination still undergoes classification.

The representation policies are:

| Option | Default | Values | Applies to |
|---|---|---|---|
| `--bit-depth` | `auto` | `auto`, `8`, `16` | preserve/gray |
| `--alpha` | `white` | `white`, `black`, `reject` | every output mode |
| `--profile-policy` | `embedded` | `embedded`, `srgb` | every output mode |

Auto continuous depth is 16 for a decoded 16-bit source, otherwise 8. Low-depth gray and indexed PNG
expand to 8-bit gray/color; palette indices are not retained. Explicit continuous 16-to-8 conversion
reports `W_DEPTH_REDUCED`; terminal binary classification has its own irreversible representation
and does not claim this continuous precision-reduction event. An explicitly empty or wrong-method
option is an invocation error. `--bit-depth` is rejected with `bw`; binarizer parameters are rejected
with preserve/gray. No compatibility alias infers binary output.

Application admission produces one fixed typed `methods::Plan` with common interpretation,
photometric/protection selections and a continuous/binary output alternative. Resource paths remain
request-owned. Actual completed Facts are immutable and distinct from the selected request. Every
processing response embeds the full current run record, its separate digest, execution diagnostics
and derived warnings; see [bundles](../record.md) for the Fields-owned wire shapes and publication/error
semantics. Raw option strings never govern numerical execution.

## Static PNG sample contract

PNG source admission for every output mode accepts gray at 1/2/4/8/16 bits; indexed at 1/2/4/8 bits;
RGB, gray-alpha and RGBA
at 8/16 bits; and valid palette/gray/RGB `tRNS`. Adam7 input is supported. APNG control/frame chunks,
unknown critical chunks, invalid CRCs, malformed critical payloads, nonconsecutive IDAT, duplicate
known color/EXIF/resolution metadata and trailing bytes after IEND are rejected. The PNG decoder's
native warnings are errors, not silent acceptance of repaired input.

The file adapter reads one bounded immutable encoded snapshot. It checks observed file size and
actual byte reads, including an extra EOF check. This is not a transactional point-in-time snapshot
of a concurrently edited source. The source is opened read-only and is never overwritten.
Regular-file admission checks the opened native handle. Source leaf links may resolve once to a
regular file; replacing that name afterward does not redirect the retained handle. POSIX opening
is nonblocking before type admission so a substituted FIFO cannot block that check.

A separate bounded chunk scan validates framing, order and CRCs before pixel allocation. It extracts
only supported metadata. The borrowed pixel stream presented to libpng contains IHDR, PLTE, tRNS,
IDAT and IEND, without independently interpreting color chunks a second time. This is why explicit
sRGB override can ignore a semantically malformed ICC declaration without disabling CRC, container,
size, animation or orientation validation. Unknown ancillary metadata is checked structurally then
omitted, not passed to an uncontrolled profile/text decoder.

Decoded samples remain unsigned integer bytes, with explicit PNG network order for 16-bit values.
The raster descriptor owns width, height, channel model and depth separately from byte-row stride.
No native libpng or Little CMS type appears in a public raster/policy header.

## Color policy

The supported precedence in `embedded` mode is:

1. `cICP` with full-range sRGB tuple `(1,13,0,1)`.
2. A valid, channel-compatible embedded RGB/gray ICC profile.
3. A valid `sRGB` declaration.
4. Valid `gAMA` and/or `cHRM` declarations.
5. Explicitly reported assumption of sRGB/standard encoded gray.

Other cICP declarations fail rather than being treated as sRGB. Color declarations are still
validated when a higher-priority supported declaration is present; iCCP with sRGB is rejected.
Redundant gAMA/cHRM alongside sRGB must match its canonical declarations. Chromaticities require
valid normalized coordinates, a nondegenerate primary triangle and a positive white-point mixture.
Only input, display, output and color-space ICC profile classes with matching RGB/gray channels are
admitted. A device-link, CMYK or incompatible profile is not a color-space reinterpretation shortcut.

`gAMA` stores image gamma g; the sample-to-linear exponent is **1/g**, not g. With only gamma, assume
sRGB primaries; with only chromaticities, assume the sRGB transfer. The result reports these separate
assumptions. For `srgb` override, ignore the semantic color declarations deliberately and report
`W_PROFILE_OVERRIDDEN`; this does not bypass the PNG sample/structural or EXIF validation rules.

Little CMS base-library transforms use context-local state, relative-colorimetric intent, no
black-point compensation, and no optional acceleration/thread plugins. A base memory callback
charges plugin-managed working, pool, profile, IO and payload blocks to the same budget through a
fixed 1024-slot ownership table. Unavailable contexts and exhaustion are resource errors. Before
native profile entry, a failed context is refused so a null handle cannot select the global allocator.
The pinned LCMS fixed context bootstrap header contains control pointers and uses the existing
small-metadata exclusion in core/memory.hpp; it has no Reservation and its actual allocation/free
events remain visible to native observation. That exclusion covers neither variable pool/profile/IO
or full-ICC-copy allocations nor image/encoded payloads. The ownership table and small control
bookkeeping follow the same existing model. Charged high-water bytes are not total process RSS.
No exception crosses a native callback. All profile/transform owners are destroyed before the context.
One run context owns immutable profiles and a bounded set of exclusive readers, each with its own
transform and input/linear/alpha/scratch rows. Transform construction occurs before worker launch;
allocation callbacks synchronize the shared slot ledger and sticky failure state. Fixed-band work
leases readers and releases each lease before return, retaining the same row/reduction order.
Every transform keeps NOCACHE and the existing numerical flags. The worker request is an upper
bound. A resource refusal after partial native reader preparation destroys the complete failed
context, then attempts at most half the prior bound and no more than its completed reader prefix.
No-reader and one-worker resource refusals, and all other errors, are terminal. Pure PNG declaration
validation occurs once; fresh contexts repeat the required native construction checks. The fixed
allocation-slot guard and charged high-water accounting remain in force across this bounded
admission. No pixel work or publication is retried.

Color3 interpretation targets linear-light RGB with sRGB primaries and D65; gray1 interpretation
has one linear luminance sample per pixel. Known sRGB cases use the image numerical layer's direct
`noexcept` binary64 transfer primitives on admitted normalized samples. Gamma-only cases use their
declared binary64 power transfer. Native ICC gray uses a genuine GrayData/D50/gamma1 destination,
not an RGB expansion followed by luminance weights. Native ICC/chromaticity transforms use double
API rows while the pinned library's internal evaluator remains float32. This is an explicit native
precision boundary, not a float64 ICC-engine claim. No profile semantics, accepted class or override
rule is narrowed by the output mode.

Interpretation works in bounded 4096-pixel blocks. Analytic normalization, transfer, alpha,
compositing and terminal quantization remain binary64. Nonfinite transform results fail. Count
interpreted components outside [0,1] before clamping: gray1 has a P-component domain and color3 has
3P, based on interpreted source category rather than selected output channels. This is not a
measurement of every internal profile-engine clipping or gamut-mapping decision.

Each block read returns its own observations. Extraction folds them once into the completed
interpretation facts; later preserve-color reconstruction cannot recount or change those facts.
Read/extract and read/reconstruct use the same cancellation token and fixed proportional bands,
with at most 64 bands or the image height. Workers lease private native transforms and scratch;
active callbacks are capped by the prepared reader pool. Independent pure stages keep the caller's
scheduler.

For encoded c and linear v, the existing standard piecewise sRGB D/E equations apply. Gray output
uses `Y = 0.2126 R + 0.7152 G + 0.0722 B` in linear RGB, then E(Y). No averaging of encoded channels.
Precision preservation means no hidden 8-bit bottleneck, not lossless transformation between
arbitrary profiles or guaranteed unchanged source samples after a requested representation change.

## Alpha, orientation and retained metadata

PNG alpha is straight/unassociated. Hidden source colors at alpha zero are set to zero before
profile conversion. Composite in linear light with `a*C + (1-a)*matte`, where the matte is white
by default or black explicitly. `reject` permits an all-opaque alpha channel but rejects any
nonopaque pixel. Output is opaque. Count flattened pixels once during interpretation/extraction;
reconstruction and independent output comparison do not mutate those completed observations.

EXIF interpretation is limited to bounded IFD0 orientation and physical resolution fields, with
byte-order, offset, type, count, denominator and duplicate checks. It does not traverse GPS, maker
notes, thumbnails or secondary IFDs. Orientation 1..8 is an exact gather permutation; no interpolation
or second orientation pass occurs. Missing orientation means normal. Malformed selected metadata
fails. Selected EXIF resolution fields must form a positive pair and fit the supported physical range,
even when pHYs takes precedence. Unitless values remain nonphysical. pHYs physical resolution
takes precedence over valid EXIF resolution, and transposed orientations
swap X/Y densities. G02 90/270-degree rotation swaps the oriented densities once more;
see [geometry](../pipeline.md) for exact mappings and mask coordinates. No resolution is invented. Unitless pHYs aspect information is not promoted to DPI.

Continuous output retains only a deterministic canonical output ICC profile and valid physical
resolution. RGB uses the generated standard sRGB profile. Gray uses D50 and the standard parametric
sRGB decoding curve, not gamma 2.2. Its ICC parameters have signed 16.16 precision; output pixels use
the independent binary64 transfer definition. The profile is serialized/reopened through the pinned
library and compared byte-for-byte during independent output validation. Profile parameters and
integer pixel precision remain separate contracts. Source/linear interpretation profiles are
separate owners from this output profile.

The output profile is created only at the late encoding boundary after final samples exist. Its
temporary serialized bytes, final serialized bytes and native profile owners overlap under the
same charge ledger, and final profile bytes remain live through independent comparison. Binary
output embeds no ICC profile and may retain only valid selected physical resolution. Canonical
protection masks carry neither ICC nor resolution. Original text, EXIF, timestamps, GPS and unknown
metadata are not copied into output.

## Resources and irreversible publication

Every output mode uses one **selected charged-buffer processing ceiling** (default 1024 MiB,
`--memory-limit` domain 64..262144 MiB), with the same 128 MiB
encoded-source and 40-million-pixel domains and native row/dimension bounds. Source byte-decoder
callers can only tighten those source bounds. ICC data is capped at 4 MiB compressed/uncompressed,
EXIF at 64 KiB and source chunk count at 65,536. These are explicit refusals, never instructions to
downsample, discard color or reduce precision. Encoded result/mask and retained supplied-PSF assets
have separate role bounds, as specified in [bundles](../record.md); the source ceiling does not become
the canonical-result reader's byte domain.

Peak live storage includes the encoded snapshot while decoding; the full integer raster needed for
interlacing/arbitrary orientation; bounded profiles/native blocks and interpretation rows; one full
binary64 luminance plane; selected eager method workspaces; and final intended/encoded output.
Preserve-color reconstruction reads the retained immutable raster in bounded blocks and transports
its color to final luminance. Source/mask owners are released when their actual consumers finish.
Terminal binary work includes Q16 and logical-code planes; continuous output owns its intended
quantized integer raster. Encoded growth charges simultaneous old/new capacity. Row padding is
charged. Tall/narrow or wide shapes can still hit row/native/budget limits; pixel count alone does
not guarantee that every shape fits.

Complete staged and later bundle validation use a separate 1 GiB snapshot/decode budget that may
coexist with live processing charges. The exact artifact/record byte domains, sequential artifact
decoding and metadata exclusions belong to [bundles](../record.md). Charged high-water bytes are not
a process-RSS limit.

Continuous encoding uses noninterlaced 8/16-bit gray/RGB; binary encoding uses noninterlaced gray1.
Both use compression level 6, default zlib strategy and SUB filter. Continuous quantization is
`floor(max_sample*E(value)+0.5)` with explicit big-endian 16-bit writes. Terminal binary classification
uses its one Q16 quantization before mapping logical codes directly into packed bits. The writer
zeros every unused low row bit. Independent decoding compares encoded bytes against the still-live
intended integer raster/logical codes, dimensions, channels, depth, exact canonical ICC bytes and
physical resolution. It does not rerun a method or derive expected pixels from decoded output.
Independent format fixtures cover shared adapter mistakes.

### Canonical packed-row validation

Canonical result PNG1 validation admits the header before installing libpng's public user-transform
callback, then updates/starts row decoding. `png_set_user_transform_info` uses the exact state pointer
and zero depth/channel overrides; packing, expansion, gamma, byte-swap, filler and other sample
transforms stay disabled. The callback inspects the original unfiltered row before libpng combines
it into the caller buffer. Layout and unused-tail findings are retained only in trivial flags
outside the jump frame and are examined after native success; a real native error takes precedence.
Caller-buffer tail bits are never evidence of the original encoded row, because combine can restore
them. A nonzero original unused bit is refused even if the caller buffer's tail is zero.

General source PNG1 admission remains separate and does not impose this canonical-output tail rule.
Required controls include healthy physical-bit/tail cases, direct callback layout observations and
CRC/digest/size-repaired negative artifacts that reach this row check. Source authoring does not
claim those controls or native callback execution have passed on any platform.

The pipeline completes encoding and independent comparison before filesystem staging. The
publication transaction writes the prepared owned bytes and invokes the complete bundle validator
before/after its exclusive rename; it contains no row-encoding callback or method execution.
A mismatched encoded result returns `E_OUTPUT_VERIFY` before commit. Cancellation abandons only
owned unpublished paths; after the final precommit cutoff, actual publication outcomes remain
authoritative. There is no destructive post-publication rollback. Atomic visibility is not crash
durability; ancestors remain trusted and blocking native/I/O calls can delay cancellation.

## Separate design QA and executable evidence

Sample, color, alpha, orientation and verification admission follow the contracts above. PNG Third Edition cICP precedence determines the color interpretation ladder. The implementation follows the C++23, expected-return, layer and binary numerical contracts.

| Challenge | Resolution and verification obligation |
|---|---|
| Interpretation changes terminal classification semantics. | One explicit common interpretation pipeline and terminal Q16 boundary; independent source/geometry/photometric/classification references and exact equality controls. |
| An ICC metadata parser can ignore a declared interpretation. | Single scanner/policy path; malformed, conflicting, overridden and higher-precedence cases are executable tests. |
| Full-page RGB float buffers exceed practical budgets. | Retain integer samples and one luminance plane; bounded color reconstruction, charged method/final rasters and native blocks, real overlap/refund controls. |
| Alpha compositing on encoded samples changes the result. | Linear-light scalar reference, zero-hidden-color and 8/16-bit transparency fixtures. |
| A 16-bit wrapper hides 8-bit processing. | All 65,536 gray levels plus RGB/alpha ramps and explicit reduction tests. |
| Encoder and decoder share a sample-order mistake. | Independent Python PNG/CRC/filter reader and hand-constructed network-order inputs. |
| Verification checks pixels but ignores metadata. | Required exact descriptor/profile/resolution comparisons and deliberately inconsistent expected metadata tests. |
| Failed verification or cancellation leaks a result. | Shared publisher; deterministic checkpoints and wrong-row tests require no final/staging output and full refunds. |
| Untested metadata/native color code escapes fuzzing. | Bounded full PNG and raw ICC harnesses; actual PNG/zlib/LCMS archive instrumentation is required by campaigns. |

Passing these conformance fixtures is not an empirical readability/fidelity benchmark of arbitrary
real documents. No new method, neural component, runtime network, renderer GUI, batch processor,
recipe/preset system or public release artifact is implied by this PNG contract.
The implemented bounded TIFF/BigTIFF adapter has its separate [source contract](tiff.md).

## Primary references

- W3C, PNG Third Edition: <https://www.w3.org/TR/png-3/>
- Pinned libpng API/manual and source, as identified in `deps/lock.json`.
- Pinned Little CMS base API, memory context and profile implementation, as identified in `deps/lock.json`.
- The existing scalar sRGB numerical contract in `src/image/numeric.cpp` and its independent tests.

## Interpretation audit

[Interpretation verification](../quality.md) requires exact
scalar sample interpretation, shared static binary framing and metadata validation before
precedence. Decoder APIs reject unknown policies and zero/relaxed limits explicitly. Scalar
references include quantization-boundary and alpha adversaries; native engine precision remains
separately tested rather than hidden in a scalar oracle.
