// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"

#include <cstdint>
#include <span>
namespace docenhance::image {
inline constexpr std::uint32_t gaussian_radius_limit = 384;
struct GaussianPass {
    std::span<const double> weights;
    double normalization{};
    bool horizontal = true;
    std::uint32_t first_row{};
    std::uint32_t last_row{};
};
// Input samples are producer-admitted finite Unit data, including protected context.
// Positive finite coefficients and exact normalization, disjoint full storage and explicit
// nonempty output rows are admitted per pass. REFLECT_101 and constants retain exact identities.
[[nodiscard]] core::Result<void> gaussian_pass(PlaneView<const double> input,
                                               PlaneView<double> output, GaussianPass pass,
                                               const core::Cancellation& cancellation = {});
} // namespace docenhance::image
