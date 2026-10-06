# Bounded TIFF source admission

## Reviewed design

TIFF/BigTIFF is a source container for continuous preserve/gray processing, with the existing
I01/D01 pipeline and verified PNG bundle publication. Binary methods keep their grayscale-PNG
contract. One top-level IFD is admitted. A nonzero next-IFD pointer is a multipage refusal,
including malformed/cyclic chains; SubIFDs are not followed or promoted. Signature dispatch uses
one immutable, identified snapshot, irrespective of filename extension.

The domain is unsigned 1-bit gray, unsigned 8/16-bit gray/RGB, 8-bit palette with 16-bit entries,
and gray/RGB with one explicitly associated or unassociated alpha channel. Strips/tiles and
contiguous/separate planes are admitted. Compression is None, PackBits, LZW, Deflate (both tag
values), CCITT Group 3/4 bilevel, and modern 8-bit Huffman DCT JPEG (tag 7), sequential/progressive, for gray/RGB/YCbCr.
Arithmetic/lossless JPEG processes are refused. JPEG uses ISLOW, fancy upsampling, no block
smoothing and no scaling, with a 128-scan ceiling per strile. Old JPEG,
signed/float samples, CMYK/Lab/LogLuv, unknown extras, unsupported predictors and raw YCbCr are
refused. Predictor 2 belongs only to 8/16-bit LZW/Deflate; predictor 1 is the default.

Decode directly through encoded strip/tile APIs, never the 8-bit RGBA convenience API. Native
16-bit samples are serialized into the existing big-endian raster representation. Palette
expansion retains all 16 bits. Native YCbCr JPEG decoding explicitly requests RGB once.
Orientation stays metadata until the existing exact coordinate mapping. Associated alpha is
unassociated in the encoded sample representation before MINISWHITE inversion and color
interpretation. Zero-alpha hidden color is zero. Compositing remains linear-light and opaque.
ICC admission uses the existing 4 MiB bound and compatible-profile rules; absent profiles use
the reported sRGB assumption. Physical resolution is retained only when valid, with existing
orientation axis swapping. Arbitrary metadata is dropped.

A bounded directory preflight rejects duplicate/invalid fields, overflowing/out-of-snapshot
extents and multipage chains before native allocation. Encoded source and pixel bounds remain
128 MiB and 40 million pixels. Additional limits are 512 IFD entries, 65,536 striles, 16 MiB
encoded per strile and 8 MiB decoded per strile. Oversized units are resource refusals; there
is no downsampling or precision fallback. The host working budget remains 1 GiB.

Reserve 32 MiB for libtiff's cumulative native payload ceiling plus a conservative 1 MiB for
allocation headers, bootstrap structures and fixed control storage. Set both native single and
cumulative allocation limits before opening. Deflate uses libtiff's charged native allocation
path. A narrowly pinned source adapter installs the existing charged JPEG memory manager before
JPEG header parsing; its bootstrap is reserved separately. JPEG progress enforces the 128-scan ceiling per strile and the shared cancellation checkpoints.
Deflate requires exact decoded size, stream end and checksum completion; trailing/oversized
streams are refused. No environment-controlled JPEG memory
or backing-file policy is relied on. Raster, decoded-unit and ICC buffers use the shared ledger.
This is allocation accounting, not a process-RSS bound.
A borrowed jump-frame pointer routes standalone JPEG failures; a null frame delegates to
libtiff's non-returning native handler. Both return to codec wrappers with C++ owners outside
the jump frame.

Use per-handle diagnostics, immutable memory callbacks and no process-global TIFF handlers.
Callbacks check cancellation during transfers of at most 64 KiB; directory work, strile boundaries
and sample scatter blocks also observe it. A native decode unit is bounded but cooperative
cancellation is not a realtime deadline. Real codec/resource errors precede a subsequent pending
stop. Ownership refunds on all exits and the existing publication cutoff remain unchanged.

## Separate design QA

The challenge pass rejected the RGBA API (precision loss and implicit orientation), scanline
random access (stateful compressed strips), uncharged nested JPEG allocation, global callbacks,
first-page success on multipage input, and treating a single-allocation cap as a total budget.
It also checked MINISWHITE plus associated alpha: invert after unassociation, not by subtracting a
premultiplied value from the full sample maximum. Native sample byte order must be tested on both
file endian orders. Palette expectations must not be derived from the decoder itself.

Required evidence includes independent byte-built classic/BigTIFF fixtures, both endian orders,
8/16-bit exact samples, strip/tile/separate-plane matrices, alpha/orientation/metadata end-to-end
bundles, each compression, malformed-directory/codec refusal, allocation refusal/refund and
checkpoint enumeration. Raw malformed TIFF decoding is included in instrumented fuzz closures.
Support becomes a runtime capability only with these checks and dependent record contracts.
