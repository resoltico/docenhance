# Denoising resource observations

These are actual synthetic measurements on macOS ARM64 using the release preset, AppleClang 21,
application 0.5.0, and the locked OpenCV 5.0.0 with the checked Mat ownership correction. LLVM 23
runs the build's lint/architecture checks. The developer runner is
`tools/measure_denoising_resources.py`; raw local evidence is retained under
`.cache/denoising-resources/macos-arm64-release.json`.

The independently framed source is 8-bit grayscale with synthetic shading and bounded noise.
D01 uses h=3, patch=7, search=21, blend=0.5 and 256-pixel tiles, producing 8-bit gray PNG. I01 rows
select explicit surface/cell64 where indicated. Each operation is a fresh process; RSS comes from
that child's wait4 usage, not the parent or an accumulated children maximum. All operations
completed their normal decoded verification and complete bundle publication.

| Pixels | Operations | Runtime, seconds | Preparation charge peak, MiB | Process peak RSS, MiB |
|---|---|---:|---:|---:|
| 3 million | D01 | 3.65 | 15.87 | 24.41 |
| 3 million | I01 -> D01 | 5.22 | 15.87 | 30.20 |
| 12 million | D01 | 13.00 | 58.71 | 75.81 |
| 12 million | I01 -> D01 | 21.74 | 58.74 | 75.41 |
| 24 million | D01 | 36.42 | 116.15 | 144.17 |
| 24 million | I01 -> D01 | 50.15 | 116.19 | 144.00 |

Preparation charge includes coexisting source, scalar/tile buffers and the native reservation at
completed calls. It is not the whole process or the separately budgeted bundle-validation working
set. Two scalar planes cost four bytes/pixel before row alignment; default full-tile native scratch
reserves 1,028,968 bytes. Padding, source/profile/mask/model lifetimes and downstream validation
explain why a four-byte/pixel calculation alone cannot describe the execution.

The independent allocation observer exercises the maximum 310x310 native input with patch15/
search41. It observed 2,687,524 live payload bytes against the 2,784,696-byte reservation and zero
remaining native payload after the call. Ordinary C++ new/new[] (distance arrays/weight vector/
controls) and native Mat data are observed separately from Budget. All seven individually injected
allocation failures returned resource errors and refunded observed payloads. With four requested
native threads, observed allocation work remains on one thread; the pinned one-stripe extent
invariant, not a backend thread-count getter, establishes the production bound.

The source override fixes native control-allocation cleanup; one-time diagnostic configuration
suppresses foreign streams while retaining native exceptions. The observer also injects a native
StsNoMem error to exercise that path. Its fixed pointer registry and warm-up are observation machinery,
not a claim about allocator bookkeeping or process RSS. Build identity/settings accompany the raw
results, and the observer is never linked into the shipped executable.

These runs establish neither a speed guarantee nor general document fidelity. Simultaneous build/
fuzz activity can affect runtime, and RSS varies by allocator/platform. The fixed-seed P04 equivalent
fixture separately reduced flat-region variance from 14.768120 to 6.572771 at explicit h3/patch3/
search7/blend0.5, retaining all three designated disconnected punctuation components at threshold100.
That synthetic assessment does not certify natural handwriting, signatures or colored annotations.

The defined weak-stroke fixture retained perceptual contrast 0.059069 from 0.063480 and preserved
red-over-green ordering for all sixteen designated red annotation samples. Exact protected working
samples and numerical changes that round to identical output bytes have separate unit evidence.
These observations retain the same synthetic-fidelity limitation as the punctuation fixture.

The maximum-window release tile took 0.357 seconds in a later fresh-process observation; the debug
build took 10.672 seconds while other compiler/sanitizer jobs were active. Cancellation can wait
for that native call. These are measured examples, not an upper wall-clock bound.

TSan owns the replaceable C++ allocation ABI. Its observer therefore installs the sanitizer's
malloc/free observation hooks, retaining ordinary/native memory accounting and the one-worker check
without replacing TSan's allocator. Mat payload refusal is injected in this mode; the native and
ASan configurations separately exercise all seven C++/Mat allocation failures. Production and
concurrent-call tests remain instrumented under TSan.
