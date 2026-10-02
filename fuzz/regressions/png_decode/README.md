# Static PNG admission regressions

`static-animation-control` preserves an APNG control declaration previously accepted by the
stored-grayscale decoder. `bytes-after-iend` preserves a second object after a valid PNG.
The independent raw-decoder framing oracle rejects acceptance of either container.
