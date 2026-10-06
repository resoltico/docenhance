// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/tvl1.hpp"
#include "tvl1_operators.hpp"

#include <cmath>
#include <cstdint>
namespace docenhance::methods {
core::Result<double> tvl1_objective(image::PlaneView<const double> input,
                                    image::PlaneView<const double> primal, double lambda,
                                    const core::Cancellation& cancellation) {
    if (input.empty() || input.width() != primal.width() || input.height() != primal.height() ||
        !std::isfinite(lambda) || lambda < Tvl1::minimum_lambda || lambda > Tvl1::maximum_lambda) {
        return core::failure(core::ErrorCode::argument,
                             "Invalid TV-L1 objective storage or fidelity");
    }
    double objective = 0;
    constexpr std::uint32_t interval = 1024;
    for (std::uint32_t y = 0; y < input.height(); ++y) {
        for (std::uint32_t x = 0; x < input.width(); ++x) {
            if (x % interval == 0 && cancellation.requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            const auto u = primal.row(y).subspan(x, 1).front();
            const auto f = input.row(y).subspan(x, 1).front();
            const auto g = tvl1_gradient(primal, x, y);
            if (!std::isfinite(u) || !std::isfinite(f) || u < 0 || u > 1 || f < 0 || f > 1 ||
                !std::isfinite(g.x) || !std::isfinite(g.y)) {
                return core::failure(core::ErrorCode::numerical, "Invalid TV-L1 objective sample");
            }
            objective += std::hypot(g.x, g.y) + (lambda * std::abs(u - f));
        }
    }
    if (!std::isfinite(objective)) {
        return core::failure(core::ErrorCode::numerical, "Nonfinite TV-L1 objective");
    }
    return objective;
}
} // namespace docenhance::methods
