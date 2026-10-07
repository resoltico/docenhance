// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/sharpening.hpp"

#include "docenhance/core/result.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
namespace docenhance::methods {
core::Result<Unsharp> Unsharp::create(UnsharpParameters p) {
    if (!std::isfinite(p.sigma) || p.sigma < Unsharp::minimum_sigma ||
        p.sigma > Unsharp::maximum_sigma || !std::isfinite(p.amount) || p.amount < 0 ||
        p.amount > Unsharp::maximum_amount || !std::isfinite(p.threshold) || p.threshold < 0 ||
        p.threshold > Unsharp::maximum_threshold) {
        return core::failure(core::ErrorCode::argument, "Invalid unsharp parameters");
    }
    return Unsharp{p};
}
core::Result<double> unsharp_candidate(double f, double blurred, const Unsharp& method) {
    if (!std::isfinite(f) || !std::isfinite(blurred) || f < 0 || f > 1 || blurred < 0 ||
        blurred > 1) {
        return core::failure(core::ErrorCode::numerical,
                             "Unsharp input is not unit finite perceptual intensity");
    }
    const auto p = method.parameters();
    constexpr double byte_maximum = 255;
    const auto d = f - blurred;
    const auto residual =
        std::copysign(std::max(std::abs(d) - (p.threshold / byte_maximum), 0.0), d);
    return f + (p.amount * residual);
}
std::string_view status_name(SharpenStatus v) noexcept {
    switch (v) {
    case SharpenStatus::disabled:
        return "disabled";
    case SharpenStatus::no_change:
        return "no_change";
    case SharpenStatus::applied:
        return "applied";
    case SharpenStatus::failed:
        return "failed";
    }
    return "failed";
}
std::string_view reason_name(SharpenReason v) noexcept {
    switch (v) {
    case SharpenReason::none:
        return "none";
    case SharpenReason::zero_amount:
        return "zero_amount";
    case SharpenReason::no_eligible_samples:
        return "no_eligible_samples";
    case SharpenReason::no_effect:
        return "no_effect";
    case SharpenReason::processing_failure:
        return "processing_failure";
    }
    return "processing_failure";
}
} // namespace docenhance::methods
