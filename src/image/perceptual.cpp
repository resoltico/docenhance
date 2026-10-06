// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"

#include <cmath>
#include <expected>
namespace docenhance::image {
core::Result<Rgb> blend_perceptual(const Rgb& rgb, double input, double output, double blend) {
    const auto y = luminance(rgb);
    if (!y) {
        return std::unexpected(y.error());
    }
    if (!std::isfinite(input) || !std::isfinite(output) || !std::isfinite(blend) || input < 0 ||
        input > 1 || output < 0 || output > 1 || blend < 0 || blend > 1) {
        return core::failure(core::ErrorCode::argument, "Invalid perceptual blend values");
    }
    if (input == output || blend == 0) {
        return rgb;
    }
    const double next = ((1 - blend) * input) + (blend * output);
    if (next == input) {
        return rgb;
    }
    const auto target = srgb_decode(next);
    if (!target) {
        return std::unexpected(target.error());
    }
    return transport_luminance(rgb, *target);
}
} // namespace docenhance::image
