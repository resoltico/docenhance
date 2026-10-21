// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/denoising.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/parameters.hpp"
#include "nlm_math.hpp"

#include <cmath>
#include <cstdint>
namespace docenhance::methods {
core::Result<Tvl1> Tvl1::create(Tvl1Parameters p) {
    if (!valid_parameters(p)) {
        return core::failure(core::ErrorCode::argument,
                             "Invalid TV-L1 fidelity, iteration cap, tolerance or blend");
    }
    return Tvl1{p};
}
core::Result<Nlm> Nlm::create(NlmParameters p) {
    if (!valid_parameters(p) || p.search < p.patch) {
        return core::failure(core::ErrorCode::argument, "Invalid NLM strength, blend or windows");
    }
    return Nlm{p};
}
core::Result<std::uint16_t> nlm_quantize(const image::Rgb& rgb) {
    if (!image::valid_unit_samples(rgb)) {
        return core::failure(core::ErrorCode::argument, "Invalid RGB sample");
    }
    const auto y = image::luminance(rgb);
    const auto f = image::srgb_encode(y);
    return nlm_code(f);
}
core::Result<image::Rgb> nlm_correct(const image::Rgb& rgb, std::uint16_t input,
                                     std::uint16_t output, double blend) {
    if (!image::valid_unit_samples(rgb)) {
        return core::failure(core::ErrorCode::argument, "Invalid RGB sample");
    }
    const auto y = image::luminance(rgb);
    if (!std::isfinite(blend) || blend < 0 || blend > 1) {
        return core::failure(core::ErrorCode::argument, "Invalid denoising blend");
    }
    if (input == output || blend == 0) {
        return rgb;
    }
    const auto f = image::srgb_encode(y);
    const auto candidate = nlm_candidate(f, input, static_cast<double>(output));
    return image::blend_perceptual(rgb, f, candidate, blend);
}
} // namespace docenhance::methods
