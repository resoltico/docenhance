// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"

#include <cstdint>
#include <limits>
namespace docenhance::methods {
inline constexpr double tvl1_step = 0.25;
inline constexpr std::uint32_t tvl1_first_checkpoint = 20;
inline constexpr std::uint32_t tvl1_checkpoint_period = 10;
inline constexpr std::uint32_t tvl1_required_passes = 2;
inline constexpr double tvl1_dual_roundoff = 16 * std::numeric_limits<double>::epsilon();
// On structurally admitted state: f/u are finite Unit, bar finite [-1,2], and
// nonterminal duals finite with the supported hypot norm <= 1 + 8*epsilon.
// Invalid terminal components remain checked Numerical failures at consumption.
struct Tvl1Field {
    image::PlaneView<const double> input;
    image::PlaneView<double> primal;
    image::PlaneView<double> extrapolated;
    image::PlaneView<double> dual_x;
    image::PlaneView<double> dual_y;
};
struct Tvl1Updates {
    double primal{};
    double dual{};
};
[[nodiscard]] core::Result<Tvl1Updates> tvl1_iteration(Tvl1Field field, double lambda,
                                                       const exec::Scheduler& scheduler);
// Producer-admitted finite Unit samples; the two readonly views may alias.
// One real scheduled task retains global row-major summation order.
[[nodiscard]] core::Result<double> tvl1_objective(image::PlaneView<const double> input,
                                                  image::PlaneView<const double> primal,
                                                  double lambda, const exec::Scheduler& scheduler);
} // namespace docenhance::methods
