# Architecture boundaries and change coupling

## Claim review

The reviewed report ran source checks only. That is useful but does not establish compiler include,
AST, link or header self-containment results. This checkout has configured compilation databases,
including isolated native workflows; absence of a build is not a current prerequisite failure.

| Report claim | Assessment and disposition |
| --- | --- |
| Acyclic layer graph and one manifest authority | Confirmed. Retain the graph; shared restrictions and explicit client roots also belong to that manifest. |
| Links match includes in both directions | The declared-link check does both comparisons. Its registration could miss a later actual CMake link addition. Final target-property validation now rejects that counterexample. |
| AST checking fails closed | Parsing and matcher-answer checks are sound, but directory-fragment location patterns can classify foreign headers as project code under a checkout ancestor named `src`. Scope rules to exact compiler-observed spellings and resolved ownership; keep parsing/answer failure visible. Real controls exclude a foreign throw while detecting an owned one. |
| Public package leakage is restricted | Retain direct package/interface permissions and compiler package reach. The record field serializer's JSON permission is deliberate; it supplies the response and persisted record with one spelling. |
| Private headers cannot leak because only public include paths are exported | Incorrect as a guarantee. Relative or absolute private-header spellings can bypass exported paths, and an allowed layer edge did not establish privacy. Resolve written includes and inspect compiler traces; public interfaces and registered clients cannot consume private production headers. |
| I/O has 47 files, about 5.3k lines and three intertwined responsibilities | The count is accurate at the reviewed baseline: 47 files, 5,274 lines of 14,638 source lines. It is not itself a defect. No literal header include cycle was found. Real coupling exists where filesystem ownership uses PNG context declarations and result decoding includes the broad bundle API for limits. Separate those interfaces and the in-memory/staged-file identity implementations. |
| Every method is smeared across seven layers | Admission, pure mathematics, native execution, record reading/writing and transport have distinct authorities. I01's sparse solver files are numerical responsibilities, not seven competing implementations. D01's native reservation boundary is required. Fact serializers and typed numerical validation are already shared. Keep these owners; a method plugin/recipe framework would move coordination and weaken exhaustive admission without a demonstrated simplification. |
| The previous simplicity audit did not solve per-method spread | Its remit was CLI transfer and publication selection. Review method ownership here; preserve the distinct safety boundaries rather than treating that earlier audit as evidence of their necessity or defect. |
| Forbidden lists have six variants and denoising has weaker restrictions | Confirmed. Replace repeated lists with one baseline and reviewed per-layer allowances. Denoising inherits allocator/process restrictions and the same header restrictions as other non-I/O adapters. Only I/O admits stream opening; only the scheduler admits thread ownership. Cancellation's core stop-token permission remains distinct. |
| `may_thread` is dead and transitive threading passes | Confirmed by a real compiler counterexample using aliased `std::jthread` and `std::async`. Drive constructor and thread-launch reference checks from the manifest permission. References also catch direct address-taking before an indirect call. These finite API rules do not prove the behavior of arbitrary foreign libraries; feature configuration remains a separate boundary. |
| Only `exec` and `entry` use threads | Only `exec` currently uses the standard thread API. `entry` owns native interrupt setup and an atomic latch, not a worker or monitoring thread. |
| Production checks cover only source/public header roots | Correct about production-layer scope. White-box tests intentionally consume private solver/native failure seams. They do not become production layers. The CLI fuzz client is now explicitly registered and checked against the CLI's public layer/package closure using actual compiler includes and generator-time links; isolated-build root selection alone is insufficient. |
| Bundle names hide different meanings | The bundle layer owns persisted record/inventory contracts and remains a meaningful domain. Host assembly is now named `run_publication`; I/O exposes separate publication, snapshot and PNG artifact interfaces. The generated reviewed-method metadata is named `reviewed_methods.hpp`, distinct from executable catalog discovery. No wrapper headers or aliases retain obsolete names. |
| Leptonica, TIFF and Catch2 are unused layer permissions | They are package identities, not permissions granted to any layer. Probe/test consumers need those identities; current capability contracts correctly exclude Leptonica/TIFF processing. Removing them would lose attribution/classification without removing a dependency. Retain them. |

## Design pass

Separate interfaces by the authority they provide. Publication owns an opaque reserved slot, file
writers and the one commit/validation callback boundary. Bundle snapshotting owns immutable encoded
files and a closed native inventory. PNG artifact observation consumes bytes and returns codec facts.
Artifact limits have one header shared by result admission and bundle admission. In-memory hashing
has no filesystem/PNG dependency; slot identity belongs beside publication. Native stream and UTF-8
path conversion declarations no longer depend on libpng, and their implementations preserve the
opened-object, exclusive-creation and identity-spelling contracts.

Keep row encoding inside the transaction and native/codec exception and jump boundaries separate.
A full codec/publication layer split would still need the slot and ownership bridge and would add
new graph edges without removing those obligations. Smaller owned interfaces remove the concrete
unnecessary dependencies. Keep processing mathematics, accounting, cancellation cutoffs, serialized
records, option semantics and foreign-library feature settings unchanged.

Consolidate policy in `spec/architecture.json`, reject obsolete per-layer deny-list forms and unknown
permission fields, and require layer exceptions to remove actual baseline restrictions. The AST
engine owns API evaluation; build orchestration owns include/link/header evidence. Production private
headers are owner-only. Registered external clients use a named public root closure, not production
namespace/API permissions. CMake records actual client links at generation and checks final production
link properties after all directories have configured. The fuzz campaign validates the client before
launching engine children; merely generating its registration is insufficient. AST ownership uses
actual compiler-spelled and resolved file identities, not an incidental directory-name fragment. The compiler-policy target is build machinery,
not an additional domain dependency.

## Separate design challenge

Before implementation, the old checker accepted a real transitive `std::jthread`/`std::async` probe
and a relative private-header spelling. A temporary matcher experiment detected the real standard
constructors and async call on libc++ despite the type alias and inline namespaces. Require a macro
expansion and function-address counterexample too; permit the same worker ownership at `exec`.
Do not ban stop tokens merely because they appear in the thread-support library: core owns explicit
cancellation control. Do not claim a finite API inventory catches every possible native concurrency
mechanism or establishes foreign code's behavior.

Challenge private includes through absolute paths, parent traversal and transitive bridge headers.
Permit same-owner implementation includes; refuse public-interface leakage even from the same layer.
Challenge both the CLI client's real include trace and actual links, plus absent registration; its
normal engine and replay compositions must inspect every compiler entry, including repeated sources. Test a late CMake property
addition without changing registration, rather than asserting that a registration JSON exists.

Retain white-box solver/publication tests: replacing private seams with public production switches
would widen the runtime contract. Retain the bundle serializer authority and probe-only package
identities. Do not infer a defect from file counts or implement an extension framework for roadmap
methods. Update current includes, target/header registration, generated outputs, suppression locations
with their original code-bound reasons, tests and documentation together. Run real native, sanitizer,
fuzz and relocated-package workflows; source-only Windows checking remains explicitly distinct from
compiler-backed architecture checking.

A later locator challenge reproduced a foreign throw wrongly treated as project code when the
checkout ancestor was named `src`. Inspect exact compiler-observed files at their resolved owners;
retain original spellings for the AST engine, including traversal and filesystem aliases. Exact
filename controls must catch the owned throw and ignore the foreign one, with spaces and regex
metacharacters in the checkout name. The clang-query matcher language preserves regex escapes;
JSON-encoding a matcher string silently changes them. A positive control exposed that error before
the corrected locator was used on a real native build. The locator was implemented in an isolated
copy while ongoing workflows retained stable inputs.
