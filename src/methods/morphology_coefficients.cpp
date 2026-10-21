// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "morphology_detail.hpp"

#include <cmath>
#include <cstdint>
#include <span>
namespace docenhance::methods {
double morphology_gaussian_weights(std::span<double> weights, double sigma,
                                   std::uint32_t gaussian_radius) noexcept {
    double normalization = 0;
    for (std::uint32_t k = 0; k < weights.size(); ++k) {
        const auto offset = static_cast<double>(k) - gaussian_radius;
        const auto value = std::exp(-(offset * offset) / (2 * sigma * sigma));
        weights.subspan(k, 1).front() = value;
        normalization += value;
    }
    return normalization;
}
} // namespace docenhance::methods
