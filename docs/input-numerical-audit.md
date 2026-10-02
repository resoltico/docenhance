# Input interpretation and numerical semantics audit

## Design pass

Trace signature admission and container framing through metadata interpretation, exact orientation,
linear conversion/compositing, I01 fitting/application, prepared D01 correction, final integer
quantization and independent output verification. Preserve the stored-sample B02/B03 domain,
normalized units, REFLECT_101 borders, exact integer moments, immutable fits, oriented protection,
real-error precedence and publication cutoff. No planned method or format becomes a capability.

Concrete remedies at their owners:

- Known sRGB/gamma samples and alpha currently round through float32 before the specified double
  scalar calculations. Use bounded double row storage for normalized input, scalar transfer and
  alpha/compositing/quantization. The native ICC/chromaticity adapter uses the pinned library's
  double API formatters, whose audited internal color pipeline remains float32. This removes the
  scalar bottleneck without a full-page float image or an extra processing graph. Allocate gray
  input by its actual one-channel shape; output linear RGB still has three channels.
- The stored-grayscale decoder bypasses the shared PNG framing scan and accepts animation and
  bytes after IEND. Apply that same bounded CRC/order/static-container scan before native decode.
  A stored-sample scan does not interpret color/orientation metadata; native grayscale acceptance
  and expansion retain their sample meaning. Unknown critical chunks, split IDAT, duplicate known
  declarations, missing/trailing end markers and animation fail before publication.
- PNG EXIF resolution fields can be partial/zero or outside supported physical range and disappear
  when pHYs wins. Validate selected EXIF fields independently of container and precedence, then
  choose the valid PNG/JPEG physical declaration. Keep TIFF's unit default, unitless non-DPI rule,
  nearest physical rounding, byte spelling and one exact orientation permutation.
- Continuous PNG silently clamps invalid programmatic limits, unlike binary PNG/JPEG. Reject
  unsupported/zero/relaxed limits at the source/result boundary instead of treating them as valid.
  Refuse unknown profile-policy enum values at PNG/JPEG decoding rather than choosing a policy.

The reviewed layer graph already puts these decisions in io/color/image/methods/host. It needs no
new edge, native public type, option, dependency, migration or alias. Scalar precision corrections
implement the existing mathematics; method identities and current record/response fields retain
their meaning. Decode-back comparison cannot by itself find an error shared by its row producer.

## Separate design QA

Before implementation, real executable probes against independent double sRGB/compositing
references found 4 gray, 13 RGBA and 4 gray-alpha one-level mismatches among 10,000 seeded 16-bit
samples per model. Retain exact adversarial triples and a broader independent corpus, including
alpha endpoints, quantization boundaries, gamma-only interpretation, 8/16-bit output and gray/RGB.
Do not widen tolerances to hide a shared scalar error. Native profile precision remains explicitly
separate, tested against independent matrix/TRC references and scalar/native format agreement.

Real executable counterexamples also published binary APNG and binary PNG with trailing bytes,
and accepted a zero EXIF X-resolution when valid pHYs was present. Regressions must fail on the
original code, preserve source bytes, report truthful absence and leave no final/staged output.
Test both endian forms, positive/partial/zero/overflow/unitless resolution, precedence and transposed
axes. Stronger metadata admission must not turn stored grayscale into color-managed thresholds.

Challenge buffer-size and component-stride changes with gray/RGB/alpha, palette expansion, Adam7,
native ICC, zero-hidden color, budget/refund and deterministic cancellation tests. The 4096-pixel
bound remains; double rows cost more charged bytes, not permission to reduce precision on refusal.
The pinned Little CMS UnrollDoublesToFloat/PackDoublesFromFloat path is the native precision
boundary; double storage is not a claim of a float64 ICC engine.

Retain independent direct-window B02 and box-mean references, byte-threshold equality tests, exact
orientation maps, true I01 solver residual/interpolation checks, protection identities and native
D01 tile/full-image seam/corner comparisons. Verify I01 -> D01 -> final quantization composition
and observation counts without refitting or native replay. Expected failures stay failures;
automatic predicates cannot erase resource/numerical refusal.

Primary PNG interpretation/ordering reference: the [reviewed PNG Third Edition recommendation](https://www.w3.org/TR/2025/REC-png-3-20250624/).
The repository's narrower admission/metadata rules are deliberate contracts, not a claim that
Exif overrides are universally mandated by PNG. Locked decoder/color sources own native behavior.

QA also found float32 ingress rounding in the I01 reference harness. Remove that correlated
implementation detail: declared linear source codes normalize directly in the independent
reference, whose dense solve, interpolation and color-transport checks continue to pass.
