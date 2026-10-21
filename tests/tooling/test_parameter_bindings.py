# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual generated bindings reject type confusion and preserve optional/path ownership."""

from __future__ import annotations

import json
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

from parameter_fixtures import compile_and_run

import generate_spec

SLOT_PROBE = r"""
#include HEADER
#include <string>
#include <string_view>
#include <variant>
int main() {
    using namespace docenhance;
    app::SuppliedOptions slots;
    slots.values[static_cast<std::size_t>(app::OptionId::nlm_h)] = std::string_view{"3"};
    if (app::bind_parameters<methods::NlmParameters>(slots)) return 1;
    slots.values[static_cast<std::size_t>(app::OptionId::nlm_h)] = 3.0;
    const auto nlm = app::bind_parameters<methods::NlmParameters>(slots);
    if (!nlm || nlm->h != 3 || nlm->patch != 7 || nlm->search != 21) return 2;
    slots.values[static_cast<std::size_t>(app::OptionId::clahe_grid)] = app::Grid{4, 7};
    const auto grid = app::bind_parameters<methods::ClaheParameters>(slots);
    if (!grid || grid->grid_columns != 4 || grid->grid_rows != 7) return 3;
    slots.values[static_cast<std::size_t>(app::OptionId::clahe_grid)] = std::uint32_t{4};
    if (app::bind_parameters<methods::ClaheParameters>(slots)) return 4;
    if (app::bind_parameters<methods::WienerParameters>(slots)) return 5;
    slots.values[static_cast<std::size_t>(app::OptionId::psf)] = std::string_view{"off"};
    if (app::bind_parameters<methods::WienerParameters>(slots)) return 6;
    slots.values[static_cast<std::size_t>(app::OptionId::psf)] = std::string_view{"kernel"};
    std::string borrowed = "kernel.png";
    slots.values[static_cast<std::size_t>(app::OptionId::psf_file)] = std::string_view{borrowed};
    const auto wiener = app::bind_parameters<methods::WienerParameters>(slots);
    if (!wiener || !std::holds_alternative<methods::Kernel>(wiener->psf)) return 7;
    std::string owned_path;
    if (!app::assign_supplied(slots, app::OptionId::psf_file, owned_path)) return 8;
    borrowed.assign("overwritten");
    if (owned_path != "kernel.png") return 9;
    return 0;
}
"""
OPTIONAL_PROBE = r"""
#include HEADER
#include <string_view>
int main() {
    using namespace docenhance;
    app::SuppliedOptions slots;
    slots.values[static_cast<std::size_t>(app::OptionId::illumination)] =
        std::string_view{"surface"};
    const auto absent = app::bind_parameters<methods::SurfaceParameters>(slots);
    if (!absent || absent->cell != 8 || absent->target != 0.8) return 1;
    slots.values[static_cast<std::size_t>(app::OptionId::background_cell)] =
        std::string_view{"auto"};
    slots.values[static_cast<std::size_t>(app::OptionId::background_target)] =
        std::string_view{"source"};
    const auto explicit_auto = app::bind_parameters<methods::SurfaceParameters>(slots);
    if (!explicit_auto || explicit_auto->cell || explicit_auto->target) return 2;
    slots.values[static_cast<std::size_t>(app::OptionId::background_cell)] =
        std::string_view{"source"};
    if (app::bind_parameters<methods::SurfaceParameters>(slots)) return 3;
    slots.values[static_cast<std::size_t>(app::OptionId::background_cell)] = std::uint32_t{16};
    slots.values[static_cast<std::size_t>(app::OptionId::illumination)] =
        std::string_view{"auto"};
    const auto automatic = app::bind_parameters<methods::SurfaceParameters>(slots);
    if (!automatic || automatic->cell != 16 ||
        automatic->mode != methods::SurfaceMode::automatic) return 4;
    slots.values[static_cast<std::size_t>(app::OptionId::illumination)] = std::string_view{"morph"};
    if (app::bind_parameters<methods::SurfaceParameters>(slots)) return 5;
    return 0;
}
"""


class ParameterBindingTests(unittest.TestCase):
    """Compiled controls reach actual private generated functions, not a Python imitation."""

    def test_wrong_slots_and_owned_resource_path(self) -> None:
        """Wrong slots refuse; the separate resource owner retains its copied path bytes."""
        with tempfile.TemporaryDirectory(prefix="parameter-binding-") as temporary:
            source = SLOT_PROBE.replace(
                "HEADER", json.dumps((ROOT / "src/app/parameter_bindings.hpp").as_posix())
            )
            self.assertEqual(compile_and_run(Path(temporary), source).returncode, 0)

    def test_explicit_automatic_values_override_numeric_defaults(self) -> None:
        """Absent and explicit symbolic options stay distinct even after valid default changes."""
        with tempfile.TemporaryDirectory(prefix="parameter-defaults-") as temporary:
            root = Path(temporary)
            shutil.copytree(ROOT / "spec", root / "spec")
            shutil.copy2(ROOT / ".clang-format", root / ".clang-format")
            path = root / "spec/method-contract.json"
            methods = json.loads(path.read_text())
            for family in methods["families"]:
                for parameter in family["shared"]:
                    if parameter["option"] == "--background-target":
                        parameter["default"] = "0.8"
                for method in family["methods"]:
                    for parameter in method["parameters"]:
                        if parameter["option"] == "--background-cell":
                            parameter["default"] = "8"
            path.write_text(json.dumps(methods))
            with patch.object(generate_spec, "ROOT", root):
                outputs = generate_spec.outputs()
            for destination, text in outputs.items():
                if destination.suffix == ".hpp":
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_text(text)
            shutil.copy2(ROOT / "src/app/admission.hpp", root / "src/app/admission.hpp")
            source = OPTIONAL_PROBE.replace(
                "HEADER", json.dumps((root / "src/app/parameter_bindings.hpp").as_posix())
            )
            self.assertEqual(compile_and_run(root, source).returncode, 0)
