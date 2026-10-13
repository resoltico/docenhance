// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/restoration.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>
namespace docenhance::methods {
bool valid_wiener_parameters(const WienerParameters& parameters) {
    return std::isfinite(parameters.k) && parameters.k >= Wiener::minimum_k &&
           parameters.k <= Wiener::maximum_k && std::isfinite(parameters.blend) &&
           parameters.blend >= 0 && parameters.blend <= 1 &&
           !parameters.psf.valueless_by_exception() && psf_side(parameters.psf).has_value();
}
core::Result<Wiener> Wiener::create(WienerParameters p) {
    if (!valid_wiener_parameters(p)) {
        return core::failure(core::ErrorCode::argument,
                             "Invalid Wiener regularization, blend or PSF");
    }
    return Wiener{std::move(p)};
}
core::Result<std::uint32_t> psf_side(const Psf& p) {
    if (const auto* const gaussian = std::get_if<GaussianPsf>(&p)) {
        if (!std::isfinite(gaussian->sigma) || gaussian->sigma < GaussianPsf::minimum_sigma ||
            gaussian->sigma > GaussianPsf::maximum_sigma) {
            return core::failure(core::ErrorCode::argument, "Invalid Gaussian PSF sigma");
        }
        return (2 * static_cast<std::uint32_t>(
                        std::ceil(GaussianPsf::radius_sigmas * gaussian->sigma))) +
               1;
    }
    if (const auto* const motion = std::get_if<MotionPsf>(&p)) {
        if (!std::isfinite(motion->length) || motion->length < MotionPsf::minimum_length ||
            motion->length > MotionPsf::maximum_length || !std::isfinite(motion->angle) ||
            motion->angle < MotionPsf::minimum_angle || motion->angle > MotionPsf::maximum_angle) {
            return core::failure(core::ErrorCode::argument, "Invalid motion PSF length or angle");
        }
        return (2 * (static_cast<std::uint32_t>(std::ceil(motion->length / 2)) + 1)) + 1;
    }
    if (!core::valid_path(std::get<FilePsf>(p).path)) {
        return core::failure(core::ErrorCode::argument, "Invalid PSF filename");
    }
    return 0;
}
bool valid_restoration_paths(const Restoration& method) noexcept {
    const auto* const selected = std::get_if<Wiener>(&method);
    if (selected == nullptr) {
        return true;
    }
    const auto* const file = std::get_if<FilePsf>(&selected->parameters().psf);
    return file == nullptr || core::valid_path(file->path);
}
core::Result<double> restoration_target(double input, double candidate, const Wiener& method) {
    if (!std::isfinite(input) || input < 0 || input > 1 || !std::isfinite(candidate)) {
        return core::failure(core::ErrorCode::numerical, "Invalid restoration luminance");
    }
    const auto blend = method.parameters().blend;
    const auto target = ((1 - blend) * input) + (blend * candidate);
    if (!std::isfinite(target)) {
        return core::failure(core::ErrorCode::numerical, "Restoration blend is not finite");
    }
    return target;
}
std::string_view status_name(RestorationStatus value) noexcept {
    switch (value) {
    case RestorationStatus::unresolved:
        return "unresolved";
    case RestorationStatus::disabled:
        return "disabled";
    case RestorationStatus::no_change:
        return "no_change";
    case RestorationStatus::applied:
        return "applied";
    case RestorationStatus::failed:
        return "failed";
    }
    return "failed";
}
std::string_view reason_name(RestorationReason value) noexcept {
    switch (value) {
    case RestorationReason::none:
        return "none";
    case RestorationReason::zero_blend:
        return "zero_blend";
    case RestorationReason::no_eligible_samples:
        return "no_eligible_samples";
    case RestorationReason::no_effect:
        return "no_effect";
    case RestorationReason::processing_failure:
        return "processing_failure";
    }
    return "processing_failure";
}
} // namespace docenhance::methods
