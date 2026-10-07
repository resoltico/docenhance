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
};
// Positive finite coefficients, unit finite samples, REFLECT_101 and exact constant identities.
[[nodiscard]] core::Result<void> gaussian_pass(PlaneView<const double> input,
                                               PlaneView<double> output, GaussianPass pass,
                                               const core::Cancellation& cancellation = {});
} // namespace docenhance::image
