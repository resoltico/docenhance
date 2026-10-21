// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/tvl1.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace docenhance::tests {
// Independent supported-state properties, not a production admission/rejection wrapper.
inline bool tvl1_state_bounded(methods::Tvl1Field field, std::span<const double> original) {
    if (original.size() != std::size_t{field.input.width()} * field.input.height()) {
        return false;
    }
    constexpr double norm_allowance = 8 * std::numeric_limits<double>::epsilon();
    for (std::uint32_t y = 0; y < field.input.height(); ++y) {
        for (std::uint32_t x = 0; x < field.input.width(); ++x) {
            const auto f = field.input.row(y).subspan(x, 1).front();
            const auto u = field.primal.row(y).subspan(x, 1).front();
            const auto bar = field.extrapolated.row(y).subspan(x, 1).front();
            const auto px = field.dual_x.row(y).subspan(x, 1).front();
            const auto py = field.dual_y.row(y).subspan(x, 1).front();
            if (!std::isfinite(f) || f < 0 || f > 1 || !std::isfinite(u) || u < 0 || u > 1 ||
                !std::isfinite(bar) || bar < -1 || bar > 2 || !std::isfinite(px) ||
                !std::isfinite(py) ||
                f != original.subspan((std::size_t{y} * field.input.width()) + x, 1).front() ||
                std::hypot(px, py) > 1 + norm_allowance ||
                (x + 1 == field.input.width() && px != 0) ||
                (y + 1 == field.input.height() && py != 0)) {
                return false;
            }
        }
    }
    return true;
}
} // namespace docenhance::tests
