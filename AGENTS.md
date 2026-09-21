# Instructions for implementation agents

Read `docs/status.md`, `docs/architecture.md` and `docs/decisions.md`, then the reviewed contracts under `spec/`. Follow the order in `docs/roadmap.md`; nothing there is complete.

`spec/architecture.json` is the layer graph, and the only place it is stated: add a layer or an allowed edge there, not in the build files, the checker or the documentation. A target links exactly the layers and packages its own files include. Layers below the adapter do not read files, environment variables, command lines or process streams, do not `throw`, and do not `catch`. The CLI is not the authority for processing rules.

Do not advertise a method/format until its entire contract and tests are implemented. Never use no-op/copy stubs to create apparently successful processing. `E_NOT_IMPLEMENTED` uses exit 4 and publication `not_started`; never repurpose exit 7.

Use the locked sources and explicit feature configuration. Do not fetch from moving branches or silently use system packages. Do not add neural/OCR/GPU/GUI/network/runtime Python components. Do not stamp MIT on upstream code or copy restricted research implementations.

Edit `spec/cli-contract.json` and `spec/method-contract.json` as reviewed authoring sources; regenerate with `python tools/generate_spec.py`. The JSON catalog is not a substitute for typed validated `EffectiveRecipe` and method variants. No semantic option may be silently ignored once its processing operation exists.

Run `python tools/check_project.py`, `python tools/check_gates.py`, `python tools/check_format.py`, `python -m ruff check`, `python -m mypy`, `python -m unittest discover -s tests/tooling -v`, relevant reference tests, and the real CMake workflow. Fix findings instead of suppressing them: every suppression must name its rule and be registered with a reason in `tests/exceptions/registry.json`, and size limits have no waivers. When you change a parser or the command line, fuzz it (`cmake --workflow --preset fuzz`) and add any reproducer to `fuzz/regressions/`. Report which checks actually ran. No fake dependency headers/libraries, mock build logs or claims that CI passed when a workflow was only authored. When dependency acquisition is unavailable, test what is available and clearly retain the missing native validation as an open requirement.

For numerical work implement the specified mathematics, border conventions, precision, resource estimates, failures and reference fixtures before performance optimization. Preserve unknown/ambiguous state rather than converting it to success. An enhanced image is never a certificate of the original document's meaning or authenticity.
