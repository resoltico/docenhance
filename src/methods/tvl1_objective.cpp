// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/parameter_domains.hpp"
#include "docenhance/methods/tvl1.hpp"
#include "tvl1_operators.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>
namespace docenhance::methods {
namespace {
struct ObjectiveFold {
    double value{};
    double lambda{};
    core::Cancellation cancellation;
};
core::Result<void> objective_row(image::PlaneView<const double> input,
                                 image::PlaneView<const double> primal, std::uint32_t y,
                                 ObjectiveFold& fold) {
    constexpr std::uint32_t interval = 1024;
    for (std::uint32_t x = 0; x < input.width(); ++x) {
        if (x % interval == 0 && fold.cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        const auto u = primal.row(y).subspan(x, 1).front();
        const auto f = input.row(y).subspan(x, 1).front();
        const auto g = tvl1_gradient(primal, x, y);
        fold.value += std::hypot(g.x, g.y) + (fold.lambda * std::abs(u - f));
    }
    return {};
}
} // namespace

core::Result<double> tvl1_objective(image::PlaneView<const double> input,
                                    image::PlaneView<const double> primal, double lambda,
                                    const exec::Scheduler& scheduler) {
    if (input.empty() || input.width() != primal.width() || input.height() != primal.height() ||
        !std::isfinite(lambda) || lambda < tv_lambda_domain.minimum ||
        lambda > tv_lambda_domain.maximum) {
        return core::failure(core::ErrorCode::argument,
                             "Invalid TV-L1 objective storage or fidelity");
    }
    ObjectiveFold fold{.lambda = lambda, .cancellation = scheduler.cancellation()};
    const auto work = [&](std::size_t) -> core::Result<void> {
        for (std::uint32_t y = 0; y < input.height(); ++y) {
            if (auto row = objective_row(input, primal, y, fold); !row) {
                return row;
            }
        }
        if (!std::isfinite(fold.value)) {
            return core::failure(core::ErrorCode::numerical, "Nonfinite TV-L1 objective");
        }
        return {};
    };
    if (auto result = scheduler.for_each(1, exec::WorkRef{work}); !result) {
        return std::unexpected(std::move(result.error()));
    }
    return fold.value;
}
} // namespace docenhance::methods
