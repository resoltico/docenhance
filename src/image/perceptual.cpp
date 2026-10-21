// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/numeric.hpp"

namespace docenhance::image {
Rgb blend_perceptual(const Rgb& rgb, double input, double output, double blend) noexcept {
    if (input == output || blend == 0) {
        return rgb;
    }
    const double next = ((1 - blend) * input) + (blend * output);
    if (next == input) {
        return rgb;
    }
    return transport_luminance(rgb, srgb_decode(next));
}
} // namespace docenhance::image
