// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/contrast.hpp"

#include "docenhance/core/result.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
namespace docenhance::methods {
core::Result<Levels> Levels::create(LevelsParameters p) {
    if (!std::isfinite(p.low) || p.low < 0 || p.low > Levels::maximum_low ||
        !std::isfinite(p.high) || p.high < Levels::minimum_high || p.high > Levels::maximum_high ||
        p.high <= p.low || !std::isfinite(p.blend) || p.blend < 0 || p.blend > 1) {
        return core::failure(core::ErrorCode::argument, "Invalid levels percentiles or blend");
    }
    return Levels{p};
}
core::Result<Clahe> Clahe::create(ClaheParameters p) {
    if (p.grid_columns < 2 || p.grid_columns > clahe_maximum_grid || p.grid_rows < 2 ||
        p.grid_rows > clahe_maximum_grid || !std::isfinite(p.clip) || p.clip < 1 ||
        p.clip > clahe_maximum_clip || !std::isfinite(p.blend) || p.blend < 0 || p.blend > 1) {
        return core::failure(core::ErrorCode::argument, "Invalid CLAHE grid, clip or blend");
    }
    return Clahe{p};
}
core::Result<Gamma> Gamma::create(GammaParameters p) {
    if (!std::isfinite(p.gamma) || p.gamma < Gamma::minimum_exponent ||
        p.gamma > Gamma::maximum_exponent || !std::isfinite(p.blend) || p.blend < 0 ||
        p.blend > 1) {
        return core::failure(core::ErrorCode::argument, "Invalid gamma exponent or blend");
    }
    return Gamma{p};
}
core::Result<double> levels_candidate(double f, LevelsRange range) {
    if (!std::isfinite(f) || f < 0 || f > 1 || !std::isfinite(range.low) ||
        !std::isfinite(range.high) || range.low < 0 || range.high > 1 ||
        range.high - range.low < levels_minimum_range) {
        return core::failure(core::ErrorCode::argument, "Invalid levels mapping values");
    }
    return std::clamp((f - range.low) / (range.high - range.low), 0.0, 1.0);
}
core::Result<double> gamma_candidate(double f, const Gamma& method) {
    if (!std::isfinite(f) || f < 0 || f > 1) {
        return core::failure(core::ErrorCode::argument, "Invalid gamma input");
    }
    if (f == 0 || f == 1 || method.parameters().gamma == 1) {
        return f;
    }
    const double result = std::pow(f, method.parameters().gamma);
    if (!std::isfinite(result) || result < 0 || result > 1) {
        return core::failure(core::ErrorCode::numerical, "Invalid gamma result");
    }
    return result;
}
std::string_view status_name(ContrastStatus v) noexcept {
    switch (v) {
    case ContrastStatus::disabled:
        return "disabled";
    case ContrastStatus::no_change:
        return "no_change";
    case ContrastStatus::applied:
        return "applied";
    case ContrastStatus::failed:
        return "failed";
    }
    return "invalid";
}
std::string_view reason_name(ContrastReason v) noexcept {
    switch (v) {
    case ContrastReason::none:
        return "none";
    case ContrastReason::zero_blend:
        return "zero_blend";
    case ContrastReason::no_eligible_samples:
        return "no_eligible_samples";
    case ContrastReason::identity_gamma:
        return "identity_gamma";
    case ContrastReason::insufficient_dynamic_range:
        return "insufficient_dynamic_range";
    case ContrastReason::no_effect:
        return "no_effect";
    case ContrastReason::processing_failure:
        return "processing_failure";
    }
    return "invalid";
}
} // namespace docenhance::methods
