// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
// Generated from spec/method-contract.json by tools/generate_spec.py.
#pragma once
#include "docenhance/methods/catalog.hpp"

#include <array>

namespace docenhance::methods {
inline constexpr ImplementedMethod surface_descriptor{
    .id = "I01",
    .method_version = 1U,
    .selector = "surface",
};
inline constexpr ImplementedMethod sauvola_descriptor{
    .id = "B02",
    .method_version = 1U,
    .selector = "sauvola",
};
inline constexpr ImplementedMethod fixed_descriptor{
    .id = "B03",
    .method_version = 1U,
    .selector = "fixed",
};
inline constexpr auto reviewed_methods =
    std::to_array<ImplementedMethod>({surface_descriptor, sauvola_descriptor, fixed_descriptor});
} // namespace docenhance::methods
