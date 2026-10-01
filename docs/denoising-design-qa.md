# D01 design review

The design in denoising.md was written before implementation. This separate review follows the
sample flow, failure paths and pinned native source rather than assuming the proposal is correct.

| Challenge | Finding and required resolution |
|---|---|
| Does a halo alone establish equivalence? | No. REFLECT_101 must fold the global source, not a local tile. Native input includes both radii, and only central output is retained. Exact full-native comparisons remain mandatory. |
| Does asking OpenCV for one thread bound scratch on macOS? | The public GCD count is misleading and setters introduce process-global policy. Smaller 256 tiles force the pinned granularity to one before backend dispatch. Test this bound and configure CV_TRACE off to exclude tracing's extra state/allocations. |
| Can native arithmetic overflow at admitted windows? | Largest patch sum is 225*65535 = 14745375; largest up-column allocation product is 310*41² = 521110 int elements. Search weighted accumulation uses int64 on CV_16U/L1. Extents are checked before signed casts. |
| Is h conversion actually exact real multiplication? | No: native receives a float. Record `257.0F * float(h)` exactly as passed; preserve the admitted double separately. |
| Can measurement replay alter observations? | Use output traversal only during one preparation pass; all reconstruction and verification reads use observation-free replay. Disabled/no-op D01 leaves I01's normal output completion intact. |
| Does retained Q preserve original doubles? | Q alone does not. Retain the interpreted source and frozen I01 model; reconstruct from those exact entering doubles. Return early on zero correction, zero blend and protected destination before transport. |
| Can I01 be falsely complete after only fitting? | Yes under a careless refactor. Only the full entering-frame traversal completes active I01. Errors during traversal preserve incomplete I01 and unreached D01. |
| Can D01 complete before output exists? | Yes: it describes prepared numerical work. Reconstruct once before publication for exact effect counts; publication failure retains this completed report. |
| Does a reservation hide duplicate allocations? | A Reservation charges only a ledger and allocates no payload. The audited native payload and fixed allowance coexist with charged tile/page storage and are refunded independently. Budget is not RSS. |
| What if native returns cancellation and allocation errors together? | Native failures are translated first, then cancellation checked. No native checkpoint can claim interruptibility inside a call. |
| Does a private new field contaminate old records? | Use version3 closed request/execution objects. Only version3 is supported; remove old branches and reject obsolete versions without migration. |
| Does catalog admission imply tests? | No. Mark D01 implemented only with actual typed admission/native execution/reference/fuzz/schema/record/fidelity tests. |
| Could OpenCV write diagnostics to command streams? | Disable tracing and ordinary native logs; configure one-time silent diagnostics because backend code overrides its macro. A stateless callback suppresses foreign streams while native exceptions still propagate. Exercise StsNoMem with captured streams. |
| Are upstream notices sufficient? | NLM source carries retained Intel/BSD-style notices in addition to the repository's Apache license. Include the original applicable notice in binary/source packaging without relicensing it. |

The design proceeds with the explicit constraints above. No QA obligation is deferred to a future
feature. Runtime and RSS are measured observations, not results established by this design review.

The executable composition check exposed a concrete output-profile defect: the old gray writer's
65530-entry curve exceeded Little CMS's serialized curve reader limit of 32767. The revised design
uses the existing standard parametric transfer, retains ICC 16.16 qualifications separately from
double pixel quantization, and requires a serialized-profile round trip. No resource fallback or
profile-policy override conceals the defect. This is included in the same processing/publication QA.


The native allocation audit found another concrete exception path: the default Mat allocator
allocated pixels before `new UMatData`, so a failed control allocation leaked the pixels. The
reviewed correction compiles a checked source override with an owning control object before pixel
allocation. Its digest/pattern/target checks fail closed on source changes; cache verification is
unchanged. Numerical output is unchanged, and every allocation failure is exercised independently
with a native allocation observer and the sanitizer workflow.
