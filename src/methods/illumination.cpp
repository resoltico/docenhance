// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/methods/illumination.hpp"

#include "docenhance/core/result.hpp"

#include <cmath>

namespace docenhance::methods {
namespace {
bool in_range(double value, double low, double high) noexcept {
    return std::isfinite(value) && value >= low && value <= high;
}
} // namespace
core::Result<Surface> Surface::create(SurfaceParameters parameters) {
    constexpr double target_floor = 0.1;
    constexpr double gain_limit = 4;
    constexpr double quantile_floor = 0.75;
    constexpr double quantile_limit = 0.99;
    constexpr double smooth_floor = 0.1;
    constexpr double smooth_limit = 20;
    const bool valid_mode = parameters.mode == SurfaceMode::explicit_surface ||
                            parameters.mode == SurfaceMode::automatic;
    if (!valid_mode || !in_range(parameters.strength, 0, 1) ||
        !in_range(parameters.max_gain, 1, gain_limit) ||
        !in_range(parameters.quantile, quantile_floor, quantile_limit) ||
        !in_range(parameters.smooth, smooth_floor, smooth_limit) ||
        (parameters.target && !in_range(*parameters.target, target_floor, 1)) ||
        (parameters.cell && (*parameters.cell < min_cell || *parameters.cell > max_cell))) {
        return core::failure(core::ErrorCode::argument, "Invalid I01 surface parameters");
    }
    return Surface{parameters};
}
} // namespace docenhance::methods
