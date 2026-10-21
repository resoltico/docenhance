// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/report_values.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace docenhance::methods {
inline constexpr std::size_t illumination_quantile_count = 3;
// Scalar application observations shared by the two illumination validators, never wire state.
struct IlluminationApplicationFacts {
    PixelCounts counts{};
    std::uint64_t gain_capped_samples{};
    std::uint64_t saturated_samples{};
    double min_gain = 1;
    double max_gain = 1;
};
[[nodiscard]] bool illumination_effect_counts(const IlluminationApplicationFacts& facts) noexcept;
[[nodiscard]] bool illumination_source_counts(const IlluminationApplicationFacts& facts,
                                              std::uint32_t samples, double source_q90) noexcept;
[[nodiscard]] bool illumination_quantile_gains(
    const IlluminationApplicationFacts& facts, std::uint32_t samples,
    const std::array<double, illumination_quantile_count>& background_quantiles, double target,
    double cap) noexcept;
} // namespace docenhance::methods
