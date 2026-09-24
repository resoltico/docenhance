# Instructions for implementation agents

Read `docs/status.md`, `docs/architecture.md`, `docs/decisions.md` and the reviewed contracts under
`spec/`. The roadmap describes planned work; inspect current capability contracts before choosing
a package. Names describe responsibilities, not a project's age, maturity or delivery phase.

`spec/architecture.json` owns the layer graph and API permissions. Update it when a genuine design
change requires a new edge or exception boundary; do not duplicate rules in build scripts or docs.
A target links exactly the layers and packages its own files include. Numeric kernels remain free
of I/O, process state, allocation expressions, throwing and catching. The CLI owns syntax and
response delivery, never processing rules. `de_app` admits `ProcessRequest`; `de_host` executes it.
The application contains unreported processing exceptions as unknown publication, not safe retry.

Do not advertise a method or format until its complete contract and tests exist. B02 Sauvola and B03 fixed-threshold
grayscale-PNG processing are implemented; other method entries remain plans. Never use no-op/copy
stubs to manufacture successful processing. `E_NOT_IMPLEMENTED` uses exit 4 and `not_started`;
`E_PUBLICATION_UNKNOWN` uses exit 7 and `unknown`. An incomplete response is not proof of no effect.

Command text and admitted paths must be well-formed UTF-8. Do not normalize paths, repair invalid
identity bytes or let diagnostic fallback change a filename. Rendering and delivery happen once,
after execution. Explicitly flush the selected stream and handle its failure without retrying.

Use locked sources and explicit feature configuration. Do not follow moving references or silently
substitute system packages. Do not add neural/OCR/GPU/GUI/network/runtime Python components. Do not
stamp MIT on upstream code or copy restricted research implementations.

Edit `spec/cli-contract.json`, `spec/method-contract.json` and the response schema template
`spec/command-response.schema.json`; regenerate with
`python tools/generate_spec.py`. Extend typed admission together with a complete processing method;
a JSON catalog is not executable validation, and unsupported options must not be silently ignored.

Run `python tools/check_project.py`, `python tools/check_gates.py`, `python tools/check_format.py`,
`python -m ruff check`, `python -m mypy`, `python -m unittest discover -s tests/tooling -v`, relevant
reference tests and the real CMake workflow. Fix findings, rather than weakening the gates. Every
suppression names its rule and has a code-bound reason in `tests/exceptions/registry.json`; size
limits have no waivers. Fuzz parser/CLI changes with `cmake --workflow --preset fuzz` and retain
reproducers in `fuzz/regressions/`. Report precisely which checks and platforms ran; authored CI,
partial local builds and absent dependencies do not establish a passing full workflow.

For numerical work implement the specified mathematics, borders, precision, resource estimates,
failures and reference fixtures before optimizing. A charged-buffer budget is not a process-RSS
limit. Preserve unknown state rather than converting it to success. An enhanced image is not a
certificate of the original document's meaning or authenticity.

For binarization, read `docs/binarization.md`. Preserve validated method alternatives, strict
option presence and normalized units. The runtime catalog comes from actual variant types and
must match reviewed generated metadata; a catalog flag alone does not implement a capability.
