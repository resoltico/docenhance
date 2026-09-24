# PNG representation and color processing

This contract defines the no-filter continuous-tone PNG path. It does not implement an illumination,
denoising, sharpening, restoration, or classification method. The separate B02/B03 stored-sample
contract remains in [binarization](binarization.md). Preserve original documents: color conversion,
alpha flattening, grayscale conversion, and requested quantization can discard information.

## Output intent and admission

`process INPUT --out-dir DIRECTORY` defaults to `--output-mode preserve`: retain the decoded color
versus grayscale category, not the original ICC space, file bytes, transparency or arbitrary metadata.
`--output-mode gray` produces encoded relative luminance. Both modes perform only the interpretation,
orientation and representation operations specified here. There is no hidden enhancement preset.

`--output-mode bw` explicitly selects the existing binary branch, defaulting to Sauvola when
`--binarize` is absent. B02/B03 still accept only 1/2/4/8-bit grayscale PNG without transparency,
expand stored samples to eight bits without gamma interpretation, and write endpoint-only eight-bit
output. Their numerical definitions and method versions are unchanged. The broader continuous-tone
input capability is not permission to silently reinterpret their samples or accept color/16-bit input.

The continuous-tone options are:

| Option | Default | Values |
|---|---|---|
| `--bit-depth` | `auto` | `auto`, `8`, `16` |
| `--alpha` | `white` | `white`, `black`, `reject` |
| `--profile-policy` | `embedded` | `embedded`, `srgb` |

Auto depth is 16 for a decoded 16-bit source, otherwise 8. Low-depth gray and indexed PNG expand to
8-bit gray/color; they are not advertised as retaining palette indices. Explicit 16-to-8 conversion
reports `W_DEPTH_REDUCED`. One-bit output is not part of this package. An explicitly empty or
wrong-operation option is an invocation error: continuous options are rejected with `bw`, and
binarizer parameters are rejected with `preserve`/`gray`. No compatibility alias infers binary output.

Application admission produces a closed `variant<Binarization, Continuous>` of validated alternatives.
Raw strings never govern pixel processing. The host supplies a typed conversion report only for the
continuous alternative; binary results retain their method identity. Continuous success has
`operation: continuous`, a `conversion` record and no fictional method ID. The generated response
schema closes both alternatives and includes `E_OUTPUT_VERIFY` (exit 5).

## Static PNG sample contract

Continuous input accepts gray at 1/2/4/8/16 bits; indexed at 1/2/4/8 bits; RGB, gray-alpha and RGBA
at 8/16 bits; and valid palette/gray/RGB `tRNS`. Adam7 input is supported. APNG control/frame chunks,
unknown critical chunks, invalid CRCs, malformed critical payloads, nonconsecutive IDAT, duplicate
known color/EXIF/resolution metadata and trailing bytes after IEND are rejected. The PNG decoder's
native warnings are errors, not silent acceptance of repaired input.

The file adapter reads one bounded immutable encoded snapshot. It checks observed file size and
actual byte reads, including an extra EOF check. This is not a transactional point-in-time snapshot
of a concurrently edited source. The source is opened read-only and is never overwritten.

A separate bounded chunk scan validates framing, order and CRCs before pixel allocation. It extracts
only supported metadata. The borrowed pixel stream presented to libpng contains IHDR, PLTE, tRNS,
IDAT and IEND, without independently interpreting color chunks a second time. This is why explicit
sRGB override can ignore a semantically malformed ICC declaration without disabling CRC, container,
size, animation or orientation validation. Unknown ancillary metadata is checked structurally then
omitted, not passed to an uncontrolled profile/text decoder.

Decoded samples remain unsigned integer bytes, with explicit PNG network order for 16-bit values.
The raster descriptor owns width, height, channel model and depth separately from byte-row stride.
No `cv::Mat`, libpng or Little CMS type appears in a public header.

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
charges actual native blocks to the same budget through a fixed 1024-slot ownership table. Exhaustion
is a resource error. The table and small C++ bookkeeping are not a claim of total process-RSS control.
No exception crosses a native callback. All profile/transform owners are destroyed before the context.

The target is linear-light RGB with sRGB primaries and D65. Known sRGB and gamma-only cases use the
same checked scalar transfer primitives as the image numerical layer; they are not a second
inconsistent implementation. Conversion works in bounded 4096-pixel chunks, using float32 native
color values and double-precision transfer/compositing/quantization calculations. Nonfinite color
results fail. Count native float components outside [0,1] before clamping; this is not a measurement
of every internal clipping/gamut-mapping decision made by the profile engine.

For encoded c and linear v, the existing standard piecewise sRGB D/E equations apply. Gray output
uses `Y = 0.2126 R + 0.7152 G + 0.0722 B` in linear RGB, then E(Y). No averaging of encoded channels.
Precision preservation means no hidden 8-bit bottleneck, not lossless transformation between
arbitrary profiles or guaranteed unchanged source samples after a requested representation change.

## Alpha, orientation and retained metadata

PNG alpha is straight/unassociated. Hidden source colors at alpha zero are set to zero before
profile conversion. Composite in linear light with `a*C + (1-a)*matte`, where the matte is white
by default or black explicitly. `reject` permits an all-opaque alpha channel but rejects any
nonopaque pixel. Output is opaque. Count flattened pixels once during the encoding pass; the
independent verification pass must not double those observations.

EXIF interpretation is limited to bounded IFD0 orientation and physical resolution fields, with
byte-order, offset, type, count, denominator and duplicate checks. It does not traverse GPS, maker
notes, thumbnails or secondary IFDs. Orientation 1..8 is an exact gather permutation; no interpolation
or second orientation pass occurs. Missing orientation means normal. Malformed selected metadata
fails. pHYs physical resolution takes precedence over EXIF resolution, and transposed orientations
swap X/Y densities. No resolution is invented. Unitless pHYs aspect information is not promoted to DPI.

Output retains only a deterministic output ICC profile and valid physical resolution. Original text,
EXIF, timestamps, GPS and unknown metadata are not copied. RGB uses a generated standard sRGB
profile. Gray uses D50 and a 65,530-entry sRGB decoding curve, not gamma 2.2. The ICC curveType stores
explicit uint16 knots rounded from the double D(c) reference; its unavoidable curve-value error is
at most half a 16-bit unit at each knot. This is an explicit refinement of the blueprint's floating
table: the pinned Little CMS float constructor otherwise serializes a 4096-knot approximation,
and its integer curve API explicitly rejects more than 65,530 entries. This supported dense table
avoids both silent coarse serialization and an unreadable profile exceeding the library limit.
Integer image samples are not quantized by this profile serialization step. Canonical serialization
fixes the ICC creation date to 2000-01-01 and recomputes the profile ID; the date is a profile
reproducibility convention, not a claimed document date. No proprietary profile is bundled.

## Resources and irreversible publication

Continuous processing has a **1 GiB charged-buffer ceiling**, distinct from the retained binary
128 MiB ceiling. Both retain the 128 MiB encoded-input and 40-million-pixel limits and native
row/dimension limits. API callers of byte decoding can only tighten these limits. ICC data is capped
at 4 MiB compressed/uncompressed; EXIF at 64 KiB; source chunk count at 65,536. These are explicit
refusals, never instructions to downsample, discard color or reduce precision.

Peak live storage includes the encoded snapshot while decoding; the full integer raster needed
for interlacing/arbitrary orientation; bounded profiles/native blocks; three small float row buffers;
and the encoder row or two verification rows. There are no full-page float working frames or a second
full decoded verification image. Row padding is charged. Tall/narrow or extremely wide layouts can
still hit row/native/budget limits; the pixel count alone is not a guarantee that every shape fits.

Encoding uses noninterlaced 8/16-bit gray/RGB, compression level 6, default zlib strategy and SUB
filter. Quantize once with `floor(max_sample*E(value)+0.5)` and explicit big-endian 16-bit writes.
Reopen the closed staged output in a separate decoder invocation. Check allowed chunk inventory,
all integer samples, dimensions, depth, channels, exact ICC bytes and physical resolution. Expected
rows are regenerated from the immutable source through the same typed conversion, not read back
from the output being verified. Independent format fixtures cover shared adapter mistakes.

The existing publication transaction alone owns staging and the native exclusive rename. Its row
writer completes encoding **and verification** before commit. Failure returns `E_OUTPUT_VERIFY`
when the encoded result differs. Cancellation during verification cleans only owned unpublished
paths; once the final precommit checkpoint authorizes the rename, its actual outcome remains
authoritative. There is no destructive post-publication rollback. Atomic visibility is not crash
durability, output ancestors remain trusted, and blocking native/I/O calls can delay cancellation.

## Separate design QA and executable evidence

The design and separate QA preceded implementation. The uploaded September 17 blueprint informed
sample, color, alpha, orientation and verification requirements. This smaller package deliberately
retains the current C++23/expected/layer/binarization contracts instead of restoring the blueprint's
conflicting C++20, unimplemented recipe framework or different floating-point Sauvola semantics.
PNG Third Edition cICP precedence is an explicitly researched addition to its older color ladder.

| Challenge | Resolution and verification obligation |
|---|---|
| Color support changes binary thresholds silently. | Separate admitted operations; existing binary numerical fixtures continue to run unchanged. |
| An ICC metadata parser can ignore a declared interpretation. | Single scanner/policy path; malformed, conflicting, overridden and higher-precedence cases are executable tests. |
| Full-page RGB float buffers exceed practical budgets. | Retain integer samples; bounded conversion/encoder/verifier rows; native memory hooks and refund tests. |
| Alpha compositing on encoded samples changes the result. | Linear-light scalar reference, zero-hidden-color and 8/16-bit transparency fixtures. |
| A 16-bit wrapper hides 8-bit processing. | All 65,536 gray levels plus RGB/alpha ramps and explicit reduction tests. |
| Encoder and decoder share a sample-order mistake. | Independent Python PNG/CRC/filter reader and hand-constructed network-order inputs. |
| Verification checks pixels but ignores metadata. | Required exact descriptor/profile/resolution comparisons and deliberately inconsistent expected metadata tests. |
| Failed verification or cancellation leaks a result. | Shared publisher; deterministic checkpoints and wrong-row tests require no final/staging output and full refunds. |
| Untested metadata/native color code escapes fuzzing. | Bounded full PNG and raw ICC harnesses; actual PNG/zlib/LCMS archive instrumentation is required by campaigns. |

Passing these conformance fixtures is not an empirical readability/fidelity benchmark of arbitrary
real documents. No new method, neural component, runtime network, renderer GUI, batch processor,
recipe/preset system, JPEG/TIFF adapter or public release artifact is introduced.

## Primary references

- W3C, PNG Third Edition: <https://www.w3.org/TR/png-3/>
- Pinned libpng API/manual and source, as identified in `deps/lock.json`.
- Pinned Little CMS base API, memory context and profile implementation, as identified in `deps/lock.json`.
- The existing scalar sRGB numerical contract in `src/image/numeric.cpp` and its independent tests.
