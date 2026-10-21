// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/restoration.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace docenhance::methods {
inline constexpr std::size_t restoration_analytic_coefficients_max = std::size_t{35} * 35;
constexpr std::uint32_t restoration_guard(std::uint32_t width, std::uint32_t height) noexcept {
    return std::max(restoration_minimum_guard,
                    restoration_guard_radius_factor * std::max(width / 2, height / 2));
}
} // namespace docenhance::methods
