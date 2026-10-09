# Exact geometry and coordinate frames

The exact G01 metadata-orientation behavior already exists in continuous-tone PNG/JPEG/TIFF
representation; it is not advertised as a standalone executable method capability.
G02 adds explicit `--rotate 0|90|180|270`, default `0`, clockwise after that orientation. Rotation
also applies to the stored-gray PNG binary branch, which retains its established policy of ignoring
metadata orientation. Perspective (G03), deskew (G04) and dewarping (G05) remain unsupported.

```sh
docenhance process scan.tif --out-dir turned --rotate 90 --protect-mask protect.png
docenhance process scan.png --out-dir binary --output-mode bw --rotate 270 --binarize otsu
```

The option preserves presence: an explicitly empty value or an angle outside the four admitted
values fails before input I/O. Rotation is execution geometry, independently of an enhancement
selector. It never enables a photometric method or changes stored-gray threshold units.

## Coordinates

Coordinates name pixel centers. The top-left center is `(0,0)`, x increases rightward and y
downward. A W×H frame has centers through `(W-1,H-1)` and pixel support from `-0.5` to `W-0.5`
and `-0.5` to `H-0.5`. Directions and dimensions always refer to a named frame.

| Frame | Meaning | Current execution |
|---|---|---|
| A | Decoded source sample layout | Original decoded extent |
| B | After metadata orientation | G01 for continuous output; B=A for stored-gray binary output |
| C | After explicit clockwise quarter-turn | G02 applied to B |
| D | After perspective rectification | D=C; G03 is unsupported |
| E | After deskew on an expanded canvas | E=D; G04 is unsupported |
| F | After vertical dewarp, on E's canvas | F=E; G05 is unsupported |

A supplied protection mask is in **B**, with B's dimensions, normal mask orientation and the
existing original-depth admission rules in [illumination](illumination.md). The same G02
permutation maps the mask to C for processing. The canonical bundled mask stays in B, so its
width and height may differ from the output's after a 90/270-degree turn. Protected sample count
is invariant. Protection bypasses photometric changes, while requested geometry still moves those
samples. Binary processing continues to reject protection masks.

PSF kernels and motion directions, illumination cells, denoising neighborhoods, CLAHE grids and
sharpening windows refer to **F**, which currently equals C. A supplied PSF coefficient image is
not rotated automatically: it describes blur in that processing frame. Future perspective quad
coordinates belong to C; naming that boundary does not admit `--quad` or other future flags.

## Exact permutations

For source W×H in A, G01 maps source centers to B as follows. Missing metadata orientation means
1; values outside 1..8 are malformed metadata. Continuous processing interprets orientation once,
including mirrored forms, without interpolation. Binary processing does not apply this table.

| Orientation | Destination `(x,y)` for source `(u,v)` | B extent |
|---:|---|---|
| 1 | `(u,v)` | W×H |
| 2 | `(W-1-u,v)` | W×H |
| 3 | `(W-1-u,H-1-v)` | W×H |
| 4 | `(u,H-1-v)` | W×H |
| 5 | `(v,u)` | H×W |
| 6 | `(H-1-v,u)` | H×W |
| 7 | `(H-1-v,W-1-u)` | H×W |
| 8 | `(v,W-1-u)` | H×W |

For W×H in B, G02 maps source centers to C:

| Clockwise degrees | Destination `(x,y)` for source `(u,v)` | C extent |
|---:|---|---|
| 0 | `(u,v)` | W×H |
| 90 | `(H-1-v,u)` | H×W |
| 180 | `(W-1-u,H-1-v)` | W×H |
| 270 | `(v,W-1-u)` | H×W |

Every destination has exactly one source. Pixel count and source sample precision are unchanged;
no margin, crop, interpolation or intermediate quantization occurs. Color/profile interpretation,
alpha compositing, selected filters and final output quantization retain their separate contracts.
Exact geometry is sample permutation, not original-file byte preservation.

Continuous row delivery composes inverse G02 and inverse G01 gathers against the retained decoded
source; it does not allocate another full color raster for a turn. The processing mask is gathered
identically. Binary rotation permutes stored samples before threshold execution. Rotated dimensions
control downstream applicability and output verification. The 0-degree case is an exact identity.

## Physical resolution

Existing source-format precedence and metadata validation remain in force. G01 orientations 5..8
swap X/Y physical densities once; G02 90/270 swap them once more. Two swaps cancel. 0/180 preserve
densities, missing physical resolution remains missing, and unitless aspect information is never
promoted to DPI. Output verification compares the resulting physical resolution exactly. Binary
output retains its existing minimal metadata policy and does not acquire physical resolution.

The future geometry contract removes global physical resolution after perspective or dewarping,
and retains it after small-angle rotation only when X/Y densities are equal. Those operations are
not implemented here. No operation invents a 300-dpi value, and physical metadata never chooses
pixel window sizes.

## Execution, records and verification

The request records `rotation_degrees`. Continuous conversion observations include
`conversion.rotation_degrees`, decoded source and rotated output extents, and metadata orientation;
B extent is derived from the decoded extent and orientation. Binary observations include
`rotation_degrees` alongside the stored-gray operation, and B=A. Request, observations, output extent and B-frame mask extent must agree; pixel count and
protected count are checked independently. The current closed response/record formats reject
obsolete forms, rather than assuming a missing rotation is zero. Geometry is reused unchanged
through encoding and independent output verification, with no second orientation or turn.

Gather loops observe cancellation in bounded blocks. A partial internal permutation is not a
successful result and cannot be published. Charged storage remains within the existing processing
budgets, which are not process-RSS limits. Geometry does not change the final precommit cutoff,
error precedence or truthful publication states; see [cancellation](cancellation.md).

The independent oracle uses an asymmetric 3×2 raster with unique samples, all eight G01 forms and
all four G02 angles, including non-square masks and unequal X/Y densities. Exact sample comparison
is required at 8/16-bit precision, with protected destinations, binary thresholding, record refusal
and cancellation coverage. Symmetric fixtures alone cannot detect mirror or direction errors.
These obligations describe the required coverage, not a claim that every platform has passed.

## Design and separate challenge

Composing exact integer gathers avoids a resampling engine, extra color plane and round trip through
encoded samples. A generic warp abstraction would add unneeded interpolation and ownership rules
before any non-orthogonal method exists. Future G03–G05 must establish their complete analysis,
map composition, validity/protection footprint and one-full-resolution-resampling contracts before
admission; reduced analysis rasters cannot replace full-precision output.

The separate challenge checks asymmetric and mirrored compositions, two density swaps, mask B/C
confusion, F-frame PSF directions and the stored-gray binary boundary. Keeping the canonical mask
in B preserves the admitted coordinate declaration and its identity; transforming it only for
processing avoids silently reinterpreting a supplied mask as an output-frame asset.
