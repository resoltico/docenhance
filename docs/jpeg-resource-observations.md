# JPEG resource observations

Executed on macOS ARM64 with LLVM Clang 23.1.2 **debug** binaries. These are synthetic 4:2:0 coefficient pages, not readability benchmarks or release-performance targets. The pipeline explicitly requested I01 surface processing with target 0.8; every recorded run applied it. Native block peaks are actual post-bootstrap charged allocations. Working peaks also include the conservative 64 KiB bootstrap charge, pixels and a charged encoded snapshot; RSS is independently observed in fresh processes.

| Pixels / process | Native block peak MiB | Decode working charge MiB | Decode RSS MiB | Decode seconds | I01 pipeline RSS MiB | I01 pipeline seconds |
|---|---:|---:|---:|---:|---:|---:|
| 2000x1500-baseline | 0.08 | 8.80 | 11.19 | 0.079 | 28.77 | 15.998 |
| 2000x1500-progressive | 8.68 | 17.41 | 19.83 | 0.096 | 37.20 | 19.820 |
| 4000x3000-baseline | 0.14 | 34.86 | 37.73 | 0.348 | 87.16 | 50.920 |
| 4000x3000-progressive | 34.57 | 69.29 | 72.22 | 0.370 | 86.84 | 46.957 |
| 6000x4000-baseline | 0.21 | 69.59 | 73.06 | 0.589 | 154.64 | 98.284 |
| 6000x4000-progressive | 68.88 | 138.26 | 141.81 | 0.750 | 154.64 | 93.272 |

These results distinguish the progressive coefficient store from baseline row work. Decoder owners are destroyed before the conversion/I01 phase; the full pipeline additionally verifies its output and bundle through the existing separately bounded workspace. A charged budget is not a process-RSS cap. These observations establish this tested local setup only.

Reproduce with `tools/measure_jpeg_resources.py`; raw local evidence is `.cache/jpeg-resources/macos-arm64-debug.json`. Release/Linux observations and native CI are separate evidence.
