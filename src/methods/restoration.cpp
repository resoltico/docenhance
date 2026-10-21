// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/restoration.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/methods/parameters.hpp"

#include <cmath>
#include <cstdint>
#include <variant>
namespace docenhance::methods {
core::Result<Wiener> Wiener::create(const WienerParameters& p) {
    if (!valid_parameters(p)) {
        return core::failure(core::ErrorCode::argument,
                             "Invalid Wiener regularization, blend or PSF");
    }
    return Wiener{p};
}
core::Result<std::uint32_t> psf_side(const Psf& p) {
    if (p.valueless_by_exception()) {
        return core::failure(core::ErrorCode::argument, "Invalid PSF state");
    }
    if (const auto* const gaussian = std::get_if<GaussianPsf>(&p)) {
        if (!valid_parameters(*gaussian)) {
            return core::failure(core::ErrorCode::argument, "Invalid Gaussian PSF sigma");
        }
        return (2 * static_cast<std::uint32_t>(
                        std::ceil(gaussian_psf_radius_sigmas * gaussian->sigma))) +
               1;
    }
    if (const auto* const motion = std::get_if<MotionPsf>(&p)) {
        if (!valid_parameters(*motion)) {
            return core::failure(core::ErrorCode::argument, "Invalid motion PSF length or angle");
        }
        return (2 * (static_cast<std::uint32_t>(std::ceil(motion->length / 2)) + 1)) + 1;
    }
    return 0;
}
double restoration_target(double input, double candidate, const Wiener& method) noexcept {
    const auto blend = method.parameters().blend;
    return ((1 - blend) * input) + (blend * candidate);
}
} // namespace docenhance::methods
