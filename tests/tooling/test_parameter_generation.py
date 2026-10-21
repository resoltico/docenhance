# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Compile adversarial generated values and refuse declarations without a safe C++ meaning."""

from __future__ import annotations

import copy
import tempfile
import unittest
from pathlib import Path

from parameter_fixtures import compile_and_run, contracts, numeric

from option_cpp import render_execution_limits, render_options
from parameter_contract import merged_options
from parameter_cpp import render_domains
from parameter_structs import render_parameters

PROBE = r"""#include "docenhance/methods/parameters.hpp"
#include "docenhance/core/utf8.hpp"
#include <stdexcept>
#include <cstdio>
#include <string>
using namespace docenhance::methods;
static_assert(ratio_domain.minimum == 0x1.fffffffffffffp-1);
static_assert(large_domain.minimum == 0x1.3c747785b50b2p+1023);
struct ThrowingPsf {
    explicit operator Kernel() const { throw std::runtime_error("injected construction failure"); }
};
int main() {
    WienerParameters p;
    const auto before = p.psf;
    if (!valid_parameters(p)) return 9;
    bool threw = false;
    try { p.psf.emplace<Kernel>(ThrowingPsf{}); }
    catch (const std::runtime_error&) { threw = true; }
    if (!threw) return 1;
    static_assert(noexcept(valid_parameters(p)));
    if (p.psf.valueless_by_exception()) {
        if (valid_parameters(p)) return 2;
        if (std::puts("PSF transition: valueless-refused") < 0) return 7;
    } else {
        if (p.psf != before || !valid_parameters(p)) return 8;
        if (std::puts("PSF transition: original-preserved") < 0) return 7;
    }
    std::string path;
    if (docenhance::core::valid_path(path)) return 3;
    path = "kernel.png";
    if (!docenhance::core::valid_path(path)) return 4;
    path = std::string("bad\0path", 8);
    if (docenhance::core::valid_path(path)) return 5;
    path = "\xff.png";
    if (docenhance::core::valid_path(path)) return 6;
    return 0;
}
"""


class ParameterGenerationTests(unittest.TestCase):
    """Exercise emitted C++, including failures Python-only metadata tests cannot detect."""

    def test_compiled_rationals_failed_psf_transition_and_conditional_path(self) -> None:
        """Exact rounding, throwing PSF transitions and separate resource path admission survive."""
        cli, methods = contracts()
        parameters = methods["families"][0]["methods"][0]["parameters"]
        parameters[-1]["when"]["--deblur"] = "wiener"
        for name, value in (
            ("--ratio", "9007199254740993/9007199254740994"),
            ("--large", "1e309/9"),
        ):
            cli["options"].append(
                {
                    **numeric(min=value, max=value, default=value),
                    "name": name,
                    "scope": ["process"],
                    "group": "Fixture",
                    "summary": "Value",
                }
            )
            cli["options"][-1].pop("option")
        options = merged_options(cli, methods)
        with tempfile.TemporaryDirectory(prefix="parameter-generation-") as directory:
            root = Path(directory)
            headers = root / "include/docenhance/methods"
            headers.mkdir(parents=True)
            (headers / "parameter_domains.hpp").write_text(render_domains(options))
            (headers / "parameters.hpp").write_text(render_parameters(methods["families"], options))
            result = compile_and_run(root, PROBE)
            self.assertEqual(result.returncode, 0)
            self.assertIn(
                result.stdout,
                ("PSF transition: valueless-refused\n", "PSF transition: original-preserved\n"),
            )
            print(result.stdout, end="")

    def test_unreachable_selector_condition_refused(self) -> None:
        """An active R01 resource cannot depend on its family being disabled."""
        cli, methods = contracts()
        methods["families"][0]["methods"][0]["parameters"][-1]["when"] = {"--deblur": "off"}
        with self.assertRaisesRegex(ValueError, "unreachable"):
            merged_options(cli, methods)

    def test_unsupported_psf_condition_refused_instead_of_dropping_member(self) -> None:
        """An accepted general condition must not silently erase the concrete resource field."""
        cli, methods = contracts()
        methods["families"][0]["methods"][0]["parameters"][-1]["when"] = {"--deblur": "wiener"}
        options = merged_options(cli, methods)
        with self.assertRaisesRegex(ValueError, "unsupported concrete PSF condition"):
            render_parameters(methods["families"], options)

    def test_choice_spellings_and_generated_collisions_refused(self) -> None:
        """Distinct CLI values cannot become invalid or aliased C++ enumerators."""
        for values in (["a-b", "a_b"], ["a.b"]):
            cli, methods = contracts()
            methods["families"][0]["methods"][0]["parameters"][0]["values"] = values
            methods["families"][0]["methods"][0]["parameters"].pop()
            with self.subTest(values=values), self.assertRaises(ValueError):
                render_domains(merged_options(cli, methods))

    def test_generated_type_and_domain_name_collisions_refused(self) -> None:
        """Different canonical options cannot alias a type or expanded grid-axis domain."""
        cli, methods = contracts()
        for name in ("--a1", "--a-1"):
            cli["options"].append(
                {
                    "name": name,
                    "kind": "choice",
                    "metavar": "V",
                    "values": ["a"],
                    "scope": ["process"],
                    "group": "Fixture",
                    "summary": "Choice",
                }
            )
        with self.assertRaisesRegex(ValueError, "colliding generated declaration"):
            render_domains(merged_options(cli, methods))
        cli, methods = contracts()
        for name, kind, default in (("--grid", "grid", "8x8"), ("--grid-columns", "integer", "8")):
            option = numeric(kind=kind, min="2", max="32", default=default)
            option.update(name=name, scope=["process"], group="Fixture", summary="Value")
            option.pop("option")
            cli["options"].append(option)
        with self.assertRaisesRegex(ValueError, "colliding generated declaration"):
            render_domains(merged_options(cli, methods))

    def test_reserved_member_and_missing_scalar_default_refused(self) -> None:
        """Unsupported authoring must fail before an invalid member or empty optional is emitted."""
        original_cli, original_methods = contracts()
        for parameter in (
            {
                "option": "--scalar",
                "field": "class",
                "kind": "decimal",
                "metavar": "N",
                "min": "0",
                "max": "1",
                "default": "0.5",
            },
            {
                "option": "--scalar",
                "field": "scalar",
                "kind": "decimal",
                "metavar": "N",
                "min": "0",
                "max": "1",
                "required": True,
            },
        ):
            cli, methods = copy.deepcopy(original_cli), copy.deepcopy(original_methods)
            methods["families"][0]["methods"][0]["parameters"].append(parameter)
            with self.subTest(parameter=parameter), self.assertRaises(ValueError):
                merged_options(cli, methods)

    def test_option_identity_and_command_cardinality_compile(self) -> None:
        """One generated cardinality authority and a disjoint sentinel remain valid C++."""
        cli, methods = contracts()
        cli["options"].append(
            {
                "name": "--count",
                "kind": "flag",
                "metavar": "",
                "scope": ["root"],
                "group": "Presentation",
                "summary": "Count",
            }
        )
        options = merged_options(cli, methods)
        with tempfile.TemporaryDirectory(prefix="option-generation-") as directory:
            root = Path(directory)
            headers = root / "include/docenhance"
            (headers / "methods").mkdir(parents=True)
            (headers / "app").mkdir()
            (headers / "methods/parameter_domains.hpp").write_text(render_domains(options))
            (headers / "app/options.hpp").write_text(render_options(cli, options))
            source = """#include "docenhance/app/options.hpp"
using namespace docenhance::app;
static_assert(command_operands(Command::process).minimum == 1);
static_assert(command_operands(Command::process).maximum == 1);
static_assert(command_operands(Command::methods).minimum == 0);
static_assert(command_operands(Command::methods).maximum == 1);
static_assert(command_operands(Command::root).maximum == 0);
static_assert(option_catalog.size() == option_count);
static_assert(OptionId::count != OptionId::count_sentinel);
int main() { return 0; }
"""
            self.assertEqual(compile_and_run(root, source).returncode, 0)
        for name in ("--class", "--count-sentinel"):
            cli["options"][-1]["name"] = name
            with self.assertRaisesRegex(ValueError, "reserved generated option identity"):
                render_options(cli, merged_options(cli, methods))

    def test_execution_domains_and_defaults_share_the_actual_cli_authority(self) -> None:
        """Mutated reviewed controls drive both compiled runtime constants and option metadata."""
        for maximum, memory_default, thread_default in ((256, 1024, "auto"), (17, 128, "3")):
            cli, methods = contracts()
            cli["options"].extend(
                [
                    {
                        "name": "--threads",
                        "metavar": "N",
                        "scope": ["process"],
                        "group": "Execution",
                        "kind": "auto_or_integer",
                        "min": "1",
                        "max": str(maximum),
                        "default": thread_default,
                        "summary": "Workers",
                    },
                    {
                        "name": "--memory-limit",
                        "metavar": "MIB",
                        "scope": ["process"],
                        "group": "Execution",
                        "kind": "integer",
                        "min": "64",
                        "max": "262144",
                        "default": str(memory_default),
                        "summary": "Memory",
                    },
                ]
            )
            options = merged_options(cli, methods)
            with tempfile.TemporaryDirectory(prefix="execution-generation-") as directory:
                root = Path(directory)
                headers = root / "include/docenhance"
                for name in ("methods", "app", "core"):
                    (headers / name).mkdir(parents=True, exist_ok=True)
                (headers / "methods/parameter_domains.hpp").write_text(render_domains(options))
                (headers / "app/options.hpp").write_text(render_options(cli, options))
                (headers / "core/execution_limits.hpp").write_text(render_execution_limits(options))
                default = "std::nullopt" if thread_default == "auto" else thread_default + "U"
                source = f"""#include "docenhance/app/options.hpp"
#include "docenhance/core/execution_limits.hpp"
#include "docenhance/app/execution_control.hpp"
using namespace docenhance;
constexpr auto threads = app::option_catalog[static_cast<std::size_t>(app::OptionId::threads)];
constexpr auto memory = app::option_catalog[static_cast<std::size_t>(app::OptionId::memory_limit)];
static_assert(threads.minimum == core::min_execution_threads);
static_assert(threads.maximum == core::max_execution_threads);
static_assert(core::max_execution_threads == {maximum});
static_assert(core::default_execution_threads == {default});
static_assert(threads.default_value.value() == "{thread_default}");
static_assert(memory.minimum == core::min_memory_limit_mib);
static_assert(memory.maximum == core::max_memory_limit_mib);
static_assert(core::default_memory_limit == std::size_t{{{memory_default}}} * 1024 * 1024);
static_assert(memory.default_value.value() == "{memory_default}");
int main() {{
    app::ExecutionControl control;
    return control.workers() != core::default_execution_threads ||
           control.memory_limit() != core::default_memory_limit;
}}
"""
                self.assertEqual(compile_and_run(root, source).returncode, 0)

    def test_execution_codegen_refuses_missing_or_incompatible_control_declarations(self) -> None:
        """No partial execution feature, method-owned control or unsupported scope is emitted."""
        with self.assertRaisesRegex(ValueError, "both reviewed"):
            render_execution_limits([])
        options = [
            {
                "name": "--threads",
                "kind": "auto_or_integer",
                "scope": ["process"],
                "family": "",
                "default": "auto",
                "min": "1",
                "max": "256",
            },
            {
                "name": "--memory-limit",
                "kind": "integer",
                "scope": ["process"],
                "family": "",
                "default": "1024",
                "min": "64",
                "max": "262144",
            },
        ]
        for key, value in (("scope", ["verify"]), ("kind", "integer"), ("family", "denoising")):
            changed = copy.deepcopy(options)
            changed[0][key] = value
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "process-only"):
                render_execution_limits(changed)
        del options[1]["default"]
        with self.assertRaisesRegex(ValueError, "process-only"):
            render_execution_limits(options)

    def test_invalid_command_cardinality_refused(self) -> None:
        """Malformed declaration bounds cannot be reinterpreted as an admitted command shape."""
        for bounds in (-1, True, [1, 0], [0], [0, 1, 2], [0, "1"]):
            cli, methods = contracts()
            cli["commands"]["process"]["operands"] = bounds
            with self.subTest(bounds=bounds), self.assertRaises(ValueError):
                merged_options(cli, methods)


if __name__ == "__main__":
    unittest.main()
