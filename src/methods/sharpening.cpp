// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/sharpening.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/gaussian.hpp"
#include "docenhance/methods/parameters.hpp"
#include "sharpen_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
namespace docenhance::methods {
core::Result<Unsharp> Unsharp::create(UnsharpParameters p) {
    if (!valid_parameters(p)) {
        return core::failure(core::ErrorCode::argument, "Invalid unsharp parameters");
    }
    return Unsharp{p};
}
double unsharp_candidate(double f, double blurred, const Unsharp& method) noexcept {
    const auto p = method.parameters();
    constexpr double byte_maximum = 255;
    const auto d = f - blurred;
    const auto residual =
        std::copysign(std::max(std::abs(d) - (p.threshold / byte_maximum), 0.0), d);
    return f + (p.amount * residual);
}
image::GaussianPass unsharp_gaussian(std::span<double, unsharp_coefficients_max> storage,
                                     const Unsharp& method) noexcept {
    const auto sigma = method.parameters().sigma;
    const auto radius = static_cast<std::uint32_t>(std::ceil(3 * sigma));
    const auto weights = storage.first((2 * radius) + 1);
    double normalization = 0;
    for (std::uint32_t k = 0; k < weights.size(); ++k) {
        const auto offset = static_cast<double>(k) - radius;
        weights.subspan(k, 1).front() = std::exp(-(offset * offset) / (2 * sigma * sigma));
        normalization += weights.subspan(k, 1).front();
    }
    return {.weights = weights, .normalization = normalization};
}
} // namespace docenhance::methods
