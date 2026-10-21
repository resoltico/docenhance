# Roadmap

This is a planning index. [Current status](status.md), the generated [method reference](methods.md)
and reviewed contracts describe implemented behavior. A roadmap item does not admit a method or
command argument.

## Future geometry constraints

Future geometry includes manual perspective, deskew and vertical text-line dewarp. Each needs
complete typed admission, execution, independent numerical references, resource estimates,
cancellation and document-quality evaluation. The current [A–F frame definitions](pipeline.md)
provide the coordinate vocabulary; these planning constraints do not admit operations or flags.
Future perspective quad coordinates belong to C; naming that boundary does not admit `--quad`
or other future flags.

The future geometry contract removes global physical resolution after perspective or dewarping,
and retains it after small-angle rotation only when X/Y densities are equal. Those operations are
not implemented here.

Future G03–G05 must establish their complete analysis, map composition, validity/protection
footprint and one-full-resolution-resampling contracts before admission; reduced analysis rasters
cannot replace full-precision output. These future paths also require truthful abstention.

Multipage processing, batch, presets and arbitrarily large streaming require separate complete
contracts. No runtime OCR, neural/GPU, GUI or network component is planned.

Engineering evidence is commit-specific. See [foundation review](quality.md#evidence-and-qualification) and
[quality evaluation](document-quality.md). Minimum-OS execution, measured resource envelopes and
cleared held-out human reviews remain separate obligations. Planned work implies no empirical
readability benefit.
