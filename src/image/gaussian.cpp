// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/gaussian.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
namespace docenhance::image {
namespace {
core::Result<double> convolve(image::PlaneView<const double> input, GaussianPass pass,
                              std::uint32_t x, std::uint32_t y) {
    const auto center = input.row(y).subspan(x, 1).front();
    bool constant = true;
    double sum = 0;
    for (std::size_t k = 0; k < pass.weights.size(); ++k) {
        const auto offset =
            static_cast<std::int64_t>(k) - static_cast<std::int64_t>(pass.weights.size() / 2);
        const auto ix =
            pass.horizontal ? image::reflect101_folded(std::int64_t{x} + offset, input.width()) : x;
        const auto iy = pass.horizontal
                            ? y
                            : image::reflect101_folded(std::int64_t{y} + offset, input.height());
        const auto value = input.row(static_cast<std::uint32_t>(iy)).subspan(ix, 1).front();
        if (!std::isfinite(value) || value < 0 || value > 1) {
            return core::failure(core::ErrorCode::numerical,
                                 "Gaussian input is not unit finite luminance");
        }
        constant = constant && value == center;
        sum += value * pass.weights.subspan(k, 1).front();
    }
    return constant ? center : sum / pass.normalization;
}
} // namespace

core::Result<void> gaussian_pass(image::PlaneView<const double> input,
                                 image::PlaneView<double> output, GaussianPass pass,
                                 const core::Cancellation& cancellation) {
    constexpr std::size_t coefficient_limit = (2 * gaussian_radius_limit) + 1;
    if (input.empty() || input.width() != output.width() || input.height() != output.height() ||
        image::overlaps(input, output) || image::overlaps(pass.weights, output.storage()) ||
        pass.weights.empty() || pass.weights.size() % 2 == 0 ||
        pass.weights.size() > coefficient_limit || !std::isfinite(pass.normalization) ||
        pass.normalization <= 0) {
        return core::failure(core::ErrorCode::argument, "Invalid Gaussian pass storage");
    }
    double total = 0;
    for (const auto weight : pass.weights) {
        if (!std::isfinite(weight) || weight <= 0) {
            return core::failure(core::ErrorCode::argument, "Invalid Gaussian coefficient");
        }
        total += weight;
    }
    if (total != pass.normalization) {
        return core::failure(core::ErrorCode::argument, "Gaussian normalization differs");
    }
    constexpr std::uint32_t checkpoint_interval = 128;
    for (std::uint32_t y = 0; y < input.height(); ++y) {
        for (std::uint32_t x = 0; x < input.width(); ++x) {
            if (x % checkpoint_interval == 0 &&
                cancellation.requested(core::Checkpoint::processing)) {
                return core::cancelled();
            }
            const auto value = convolve(input, pass, x, y);
            if (!value) {
                return std::unexpected(value.error());
            }
            output.row(y).subspan(x, 1).front() = *value;
        }
    }
    return {};
}
} // namespace docenhance::image
