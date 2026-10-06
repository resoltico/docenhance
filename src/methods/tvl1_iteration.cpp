// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/tvl1.hpp"
#include "tvl1_operators.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
namespace docenhance::methods {
namespace {
constexpr std::uint32_t interval = 1024;
bool unit(double v) noexcept {
    return std::isfinite(v) && v >= 0 && v <= 1;
}
bool storage(Tvl1Field f) noexcept {
    const auto same = [&](auto p) {
        return p.width() == f.input.width() && p.height() == f.input.height();
    };
    return !f.input.empty() && same(f.primal) && same(f.extrapolated) && same(f.dual_x) &&
           same(f.dual_y) && !image::overlaps(f.input, f.primal) &&
           !image::overlaps(f.input, f.extrapolated) && !image::overlaps(f.input, f.dual_x) &&
           !image::overlaps(f.input, f.dual_y) && !image::overlaps(f.primal, f.extrapolated) &&
           !image::overlaps(f.primal, f.dual_x) && !image::overlaps(f.primal, f.dual_y) &&
           !image::overlaps(f.extrapolated, f.dual_x) &&
           !image::overlaps(f.extrapolated, f.dual_y) && !image::overlaps(f.dual_x, f.dual_y);
}
core::Result<double> dual_pixel(Tvl1Field f, std::uint32_t x, std::uint32_t y) {
    const auto g = tvl1_gradient(f.extrapolated.as_const(), x, y);
    auto& px = f.dual_x.row(y).subspan(x, 1).front();
    auto& py = f.dual_y.row(y).subspan(x, 1).front();
    const auto center = f.extrapolated.row(y).subspan(x, 1).front();
    constexpr double projection_error = 8 * std::numeric_limits<double>::epsilon();
    if (!std::isfinite(center) || center < -1 || center > 2 || !std::isfinite(g.x) ||
        !std::isfinite(g.y) || !std::isfinite(px) || !std::isfinite(py) ||
        std::hypot(px, py) > 1 + projection_error || (x + 1 == f.input.width() && px != 0) ||
        (y + 1 == f.input.height() && py != 0)) {
        return core::failure(core::ErrorCode::numerical,
                             "Invalid TV-L1 extrapolated or dual sample");
    }
    const double cx = px + (tvl1_step * g.x);
    const double cy = py + (tvl1_step * g.y);
    const double divisor = std::max(1.0, std::hypot(cx, cy));
    const double nx = x + 1 == f.input.width() ? 0 : cx / divisor;
    const double ny = y + 1 == f.input.height() ? 0 : cy / divisor;
    const double update = std::hypot(nx - px, ny - py);
    px = nx;
    py = ny;
    return update;
}
core::Result<double> primal_pixel(Tvl1Field f, std::uint32_t x, std::uint32_t y, double lambda) {
    auto& u = f.primal.row(y).subspan(x, 1).front();
    const double input = f.input.row(y).subspan(x, 1).front();
    if (!unit(input) || !unit(u)) {
        return core::failure(core::ErrorCode::numerical, "Invalid TV-L1 input or primal sample");
    }
    const double v = u - (tvl1_step * tvl1_adjoint(f.dual_x.as_const(), f.dual_y.as_const(), x, y));
    const double d = v - input;
    const double next = std::clamp(
        input + std::copysign(std::max(std::abs(d) - (tvl1_step * lambda), 0.0), d), 0.0, 1.0);
    const double update = std::abs(next - u);
    f.extrapolated.row(y).subspan(x, 1).front() = next + (next - u);
    u = next;
    return update;
}
core::Result<double> dual_pass(Tvl1Field f, const core::Cancellation& cancellation) {
    double maximum = 0;
    for (std::uint32_t y = 0; y < f.input.height(); ++y) {
        for (std::uint32_t x = 0; x < f.input.width(); ++x) {
            if (x % interval == 0 && cancellation.requested(core::Checkpoint::processing)) {
                return core::cancelled();
            }
            auto update = dual_pixel(f, x, y);
            if (!update) {
                return std::unexpected(update.error());
            }
            maximum = std::max(maximum, *update);
        }
    }
    return maximum;
}
core::Result<double> primal_pass(Tvl1Field f, double lambda,
                                 const core::Cancellation& cancellation) {
    double maximum = 0;
    for (std::uint32_t y = 0; y < f.input.height(); ++y) {
        for (std::uint32_t x = 0; x < f.input.width(); ++x) {
            if (x % interval == 0 && cancellation.requested(core::Checkpoint::processing)) {
                return core::cancelled();
            }
            auto update = primal_pixel(f, x, y, lambda);
            if (!update) {
                return std::unexpected(update.error());
            }
            maximum = std::max(maximum, *update);
        }
    }
    return maximum;
}
} // namespace
core::Result<Tvl1Updates> tvl1_iteration(Tvl1Field field, double lambda,
                                         const core::Cancellation& cancellation) {
    if (!storage(field) || !std::isfinite(lambda) || lambda < Tvl1::minimum_lambda ||
        lambda > Tvl1::maximum_lambda) {
        return core::failure(core::ErrorCode::argument,
                             "Invalid TV-L1 iteration storage or fidelity");
    }
    if (cancellation.requested(core::Checkpoint::processing)) {
        return core::cancelled();
    }
    auto dual = dual_pass(field, cancellation);
    if (!dual) {
        return std::unexpected(dual.error());
    }
    auto primal = primal_pass(field, lambda, cancellation);
    if (!primal) {
        return std::unexpected(primal.error());
    }
    return Tvl1Updates{.primal = *primal, .dual = *dual};
}
} // namespace docenhance::methods
