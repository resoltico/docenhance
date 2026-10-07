// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
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
inline constexpr ImplementedMethod morph_descriptor{
    .id = "I02",
    .method_version = 1U,
    .selector = "morph",
};
inline constexpr ImplementedMethod nlm_descriptor{
    .id = "D01",
    .method_version = 1U,
    .selector = "nlm",
};
inline constexpr ImplementedMethod tvl1_descriptor{
    .id = "D02",
    .method_version = 1U,
    .selector = "tvl1",
};
inline constexpr ImplementedMethod levels_descriptor{
    .id = "C01",
    .method_version = 1U,
    .selector = "levels",
};
inline constexpr ImplementedMethod gamma_descriptor{
    .id = "C02",
    .method_version = 1U,
    .selector = "gamma",
};
inline constexpr ImplementedMethod clahe_descriptor{
    .id = "C03",
    .method_version = 1U,
    .selector = "clahe",
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
inline constexpr auto reviewed_methods = std::to_array<ImplementedMethod>({
    surface_descriptor,
    morph_descriptor,
    nlm_descriptor,
    tvl1_descriptor,
    levels_descriptor,
    gamma_descriptor,
    clahe_descriptor,
    sauvola_descriptor,
    fixed_descriptor,
});
} // namespace docenhance::methods
