# Fixed pipeline and exact geometry

## Eager interpretation and fixed method execution

`de_pipeline` consumes owned encoded snapshots and the admitted plan, without filesystem effects.
It decodes once, interprets source samples and extracts an owned binary64 luminance plane. A
context-local interpreter borrows its live raster; extraction folds returned block observations once,
and optional preserve-color reconstruction cannot modify completed interpretation facts. Extraction
and reconstruction keep the same cancellation token and fixed proportional bands, capped at 64
bands or the image height. Their scheduler uses at most the actual prepared reader count; each
active callback owns an exclusive reader. Independent pure methods retain the caller's scheduler.

Each selected photometric stage eagerly consumes the entering plane and returns immutable Facts
before the next family enters. Masks protect analysis and preserve entering photometric samples;
terminal binarization classifies every destination. R01 resolves the PSF in true processing frame F,
even for a numerical bypass. The exact encoded supplied-kernel buffer moves into the output asset
after its borrows end. Source and mask owners release as soon as their actual consumers finish.
Canonical continuous-output ICC is created only at late encoding; the final profile remains live
through independent comparison. Encoded growth accounts for simultaneous old/new capacity.

One `app::Published` owns the actual full `RunRecord`, record identity and execution diagnostics.
`app::ProcessFailure` owns a core code/message, explicit publication state, optional failed family,
sparse actual completed Facts and private runtime warning context. The host derives the record from
its actual admitted Plan, build and completed work before serialization/publication. No private
warning context or stored warning list enters the persistent record. Response rendering derives
warnings once from admitted facts and available runtime context. [Bundles](record.md) defines the complete wire/effect distinction.



G01 metadata orientation applies once to every admitted PNG/JPEG/TIFF source, independently of
preserve, gray or binary output. It is not a standalone selectable method. G02 is explicit
`--rotate 0|90|180|270`, default `0`, clockwise after G01. Perspective (G03), deskew (G04) and dewarping (G05) remain unsupported.

```sh
docenhance process scan.tif --out-dir turned --rotate 90 --protect-mask protect.png
docenhance process scan.png --out-dir binary --output-mode bw --rotate 270 --binarize otsu
```

The option preserves presence: an explicitly empty value or an angle outside the four admitted
values fails before input I/O. Rotation is execution geometry, independently of an enhancement
selector. It never enables a photometric method or changes the binarizer's terminal Q16 units.

## Coordinates

Coordinates name pixel centers. The top-left center is `(0,0)`, x increases rightward and y
downward. A W×H frame has centers through `(W-1,H-1)` and pixel support from `-0.5` to `W-0.5`
and `-0.5` to `H-0.5`. Directions and dimensions always refer to a named frame.

| Frame | Meaning | Current execution |
|---|---|---|
| A | Decoded source sample layout | Original decoded extent |
| B | After metadata orientation | G01 for every output mode |
| C | After explicit clockwise quarter-turn | G02 applied to B |
| D | After perspective rectification | D=C; G03 is unsupported |
| E | After deskew on an expanded canvas | E=D; G04 is unsupported |
| F | After vertical dewarp, on E's canvas | F=E; G05 is unsupported |

A supplied protection mask is in **B**, with B's dimensions, normal mask orientation and the
existing original-depth admission rules in [illumination](methods/illumination.md#i01-surface-illumination). The same G02
permutation maps the mask to C for processing. The canonical bundled mask stays in B, so its
width and height may differ from the output's after a 90/270-degree turn. Protected sample count
is invariant. Protection bypasses photometric changes, while requested geometry still moves those
samples. Terminal binary classification still classifies every destination after those protected
photometric values have been preserved.

PSF kernels and motion directions, illumination cells, denoising neighborhoods, CLAHE grids and
sharpening windows refer to **F**, which currently equals C. A supplied PSF coefficient image is
not rotated automatically: it describes blur in that processing frame.

## Exact permutations

For source W×H in A, G01 maps source centers to B as follows. Missing metadata orientation means
1; values outside 1..8 are malformed metadata. All output modes interpret orientation once,
including mirrored forms, without interpolation.

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

Interpretation composes inverse G02 and inverse G01 gathers against the retained decoded source;
it does not allocate another full color raster for a turn. Extraction stores the oriented/rotated
luminance plane, and the processing mask is gathered identically. All selected photometric methods
use that frame before continuous quantization or terminal Q16 binary classification. Rotated dimensions
control downstream applicability and output verification. The 0-degree case is an exact identity.

## Physical resolution

Existing source-format precedence and metadata validation remain in force. G01 orientations 5..8
swap X/Y physical densities once; G02 90/270 swap them once more. Two swaps cancel. 0/180 preserve
densities, missing physical resolution remains missing, and unitless aspect information is never
promoted to DPI. All output modes retain only valid selected physical resolution, and independent
output verification compares it exactly. Binary output has no ICC profile; its packed sample and
canonical metadata rules remain in [PNG processing](formats/png.md).

No operation invents a 300-dpi value, and physical metadata never chooses pixel window sizes.

## Execution, records and verification

The request records `rotation`; execution interpretation records its actual `rotation`, metadata
`orientation`, decoded extent and output extent. B is derived from the decoded extent and orientation
for every output mode. Request, interpretation, output extent and B-frame mask extent must agree;
pixel and protected counts are checked independently. See [bundles](record.md) for the single current
Fields-owned response/record forms. Obsolete shapes are refused. Geometry is reused unchanged through
encoding and independent output comparison, with no second orientation, turn or method execution.

Gather loops observe cancellation in bounded blocks. A partial internal permutation is not a
successful result and cannot be published. Charged storage remains within the
[selected processing ledger](architecture.md#values-ownership-and-budgets) shared by all output
modes, which is not a process-RSS limit. Geometry does not change the final precommit cutoff,
error precedence or truthful publication states; see [cancellation](architecture.md#cooperative-cancellation).

The independent oracle uses an asymmetric 3×2 raster with unique samples, all eight G01 forms and
all four G02 angles, including non-square masks and unequal X/Y densities. Exact sample comparison
is required at 8/16-bit precision, with protected destinations, binary thresholding, record refusal
and cancellation coverage. Symmetric fixtures alone cannot detect mirror or direction errors.
These obligations describe the required coverage, not a claim that every platform has passed.

## Design and separate challenge

Composing exact integer gathers avoids a resampling engine, extra color plane and round trip through
encoded samples. A generic warp abstraction would add unneeded interpolation and ownership rules
before any non-orthogonal method exists.

The separate challenge checks asymmetric and mirrored compositions, two density swaps, mask B/C
confusion, F-frame PSF directions and terminal Q16 classification after the common pipeline. Keeping the canonical mask
in B preserves the admitted coordinate declaration and its identity; transforming it only for
processing avoids silently reinterpreting a supplied mask as an output-frame asset.
