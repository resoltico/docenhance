# Resource and computational limits

## Design pass

Trace one admitted request through acquisition, interpretation, optional I01/D01, encoding,
decoded comparison, staging validation and postcommit reconciliation. Release the record-identification snapshot before full reconciliation;
its digest is the only retained fact needed for the later comparison. Preserve the distinct
processing and validation ledgers; combining them would change the admitted resource contract.

| Boundary | Simultaneously live resources and enforced work bounds |
|---|---|
| Acquisition | At most 128 MiB encoded source and 40 million pixels; encoded bytes coexist with decoded aligned rows and codec storage until decoder return. PNG bounds chunks; JPEG bounds markers, metadata, scans and sampling. |
| Binary processing | Source and destination aligned byte planes share 128 MiB with codec allocations. Sauvola adds charged strip scratch, at most four automatic workers; the reusable scheduler caps explicit concurrency at 64 and joins every started worker. |
| Continuous processing | Decoded source/profile, converter/native color context, mask, retained model and prepared D01 planes share 1 GiB. Temporary mask decoding includes encoded bytes and decoded rows before producing its canonical byte plane. |
| I01 preparation | At most 65,536 grid cells, 500 solver iterations, 17 hierarchy levels, a 512-square cell-selection buffer and 1,048,576 lattice samples. Measurement, solve and lattice transient buffers occupy separate phases; retained model/source/mask still coexist with them. |
| D01 preparation | Two aligned uint16 page planes coexist with source, profile, mask/model and two bounded tile planes. Each sequential native call reserves audited scratch before execution; the maximum 310-square tile forces one native stripe without changing process thread policy. |
| Publication | Processing owners remain live through encoding and decoded comparison. Complete bundle validation adds its own 1 GiB ledger, including record/result/mask encoded snapshots; decoded result and mask planes are sequential. Reconciliation can repeat validation but cannot repeat numerical processing. |
| Metadata/control | Record bytes <=1 MiB, depth <=16, parser events <=1024; native allocation registries and directory inventories are finite. Standard containers, diagnostic strings, ledger control, C streams, allocator bookkeeping, native control and OS stacks are not a process-RSS budget. |

The accounting uses checked aligned sizes, subtraction-before-addition admission and shared ledger
lifetime. Exact-budget, one-byte-short, concurrent refund, extreme extent and native allocation
refusal tests already protect these mechanisms. Keep them rather than introducing a universal
resource planner or user-configurable work limit.

Two concrete remedies belong at existing owners:

- Admit JSON through the library's SAX interface before DOM construction. A SAX refusal stops
  immediately at the event ceiling or a duplicate key; a DOM discard callback does not terminate
  parsing. Release admission bookkeeping before the bounded DOM pass. Both passes use the same
  library grammar, with no explicit throws or added exception permission.
- Replace percentile partitioning with in-place sorting of the caller's scratch. The value remains
  the specified nearest rank; worst-case comparison work becomes O(n log n). Do not allocate a
  second sample array, change quantiles, subsample or introduce another numerical implementation.

## Separate design challenge

The locked JSON parser calls object_start for discarded objects but skips their object_end callback.
An independent 30,001-byte shallow array of 10,000 empty objects reached 10,514 callbacks and retained
9,488 key scopes despite the 1,024-event bound. The fix must terminate before bookkeeping for the
first excessive event. Test exact and excess boundaries, duplicate keys, escaped strings and a
malformed unread suffix; independently observe parser allocation payload rather than timing it.
The existing architecture forbids explicit project throws; SAX refusal honors that rule. Keep
byte/depth admission before SAX and preserve decoded escaped-key duplicate detection. The shallow
fixture observed 643,168 bytes before remediation; the final SAX path measured 3,104 bytes and
returned every observed payload on refusal. Remove the fuzz harness's 4 KiB truncation so
the retained reproducer reaches this boundary in full; campaign mutation sizes remain explicit.

On local LLVM 23 libc++, a median-pivot adversarial permutation at the 90th percentile required
3,176,052 / 12,649,846 / 50,475,330 / 201,639,789 comparisons for 4,096 / 8,192 / 16,384 / 32,768
samples. In-place sorting required 148,643 / 323,673 / 698,886 / 1,498,617 comparisons. The growing
quadratic partition work is avoidable with the standard sort complexity guarantee. Challenge the
replacement with the maximum lattice scratch permutation, independent known ranks, repeated values,
invalid inputs unchanged and an allocation observer; no timing-sleep test or latency promise.

Inject each observed C++ allocation refusal through a nested JSON fixture reaching both SAX
admission and DOM construction. Native/ASan must return resource errors and refund all observed
payloads, including failures while producing the ordinary rejection diagnostic. TSan retains its
allocator ABI; its bookkeeping and zero-allocation selection observations are separate evidence.

Charged limits do not ensure every admitted pixel count/aspect ratio fits, bound aggregate concurrent
library requests, certify allocator overhead or impose wall-clock deadlines. Native calls and
filesystem I/O can block. Cancellation remains cooperative; preserve existing checkpoints, joins,
error precedence and the publication cutoff. Full platform CI and local required workflows establish
commit-specific evidence, not exhaustive resource safety or document authenticity.

## Destruction ownership challenge

The per-allocation experiment aborted with std::bad_alloc rather than reaching the record reader's
catch. The debugger places termination in nlohmann JSON's noexcept data destructor: its flattened
cleanup allocates a std::vector traversal stack. A caller-side cleanup wrapper cannot protect
partially constructed objects inside the parser, and a custom JSON allocator does not control that
stack's separate standard allocator. Replacing the JSON library/field mapping would expand the
change without addressing this owning fault more simply.

Compile a checked private copy of the locked single header with the allocating flattening block
removed. Ordinary container destruction then recursively releases children without allocating.
Every production untrusted DOM passes byte/depth/SAX admission first; the only other production
parse reads the fixed-shape record serialization. Response/record builders construct bounded typed
shapes, not arbitrary user-supplied DOMs. The existing 16-level admission bounds recursive cleanup;
this is not permission to parse arbitrary-depth JSON. Challenge the maximum admitted depth and
both ordinary and exceptional cleanup with each observed allocation refusal.

Keep the immutable source cache and original upstream attribution untouched. Pin the reviewed
header digest and correction feature in deps/features.json; reuse upstream CMake packaging through
a private source copy instead of maintaining a parallel package configuration. Bind this recipe to
fresh build trees/private prefixes and Docker cache identity. Verify that the actual installed header
matches the reviewed corrected bytes against an independent output-digest reference, including a
negative control altering both mutable copies; a cache flag or two copies agreeing proves neither
review nor cleanup behavior. Native allocation-refusal tests establish the behavior separately.
