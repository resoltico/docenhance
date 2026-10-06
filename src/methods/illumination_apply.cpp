// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/illumination.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <span>
namespace docenhance::methods {
namespace {
void observe(double gain, double raw_target, bool capped, IlluminationReport& report) noexcept {
    if (report.evaluated_samples == 0) {
        report.min_gain = gain;
        report.max_gain = gain;
    } else {
        report.min_gain = std::min(report.min_gain, gain);
        report.max_gain = std::max(report.max_gain, gain);
    }
    ++report.evaluated_samples;
    report.gain_capped_samples += static_cast<std::uint64_t>(capped);
    report.saturated_samples += static_cast<std::uint64_t>(raw_target > 1);
}
} // namespace
core::Result<void> apply_illumination_pixel(std::span<double> rgb, double background,
                                            IlluminationGain gain, IlluminationReport& report) {
    if (rgb.size() != image::rgb_channels || !std::isfinite(background) || background < 0 ||
        background > 1 || !std::isfinite(gain.target) || gain.target < 0 || gain.target > 1 ||
        !std::isfinite(gain.strength) || gain.strength < 0 || gain.strength > 1 ||
        !std::isfinite(gain.max_gain) || gain.max_gain < 1 ||
        gain.max_gain > illumination_gain_limit) {
        return core::failure(core::ErrorCode::argument, "Invalid illumination application values");
    }
    const image::Rgb original{
        rgb.front(),
        rgb.subspan(1, 1).front(),
        rgb.subspan(2, 1).front(),
    };
    const auto y = image::luminance(original);
    if (!y) {
        return std::unexpected(y.error());
    }
    const double ratio = gain.target / std::max(background, illumination_floor);
    const double applied_gain = std::clamp(ratio, 1.0, gain.max_gain);
    const double raw_target = *y * std::pow(applied_gain, gain.strength);
    const double desired = std::clamp(raw_target, 0.0, 1.0);
    observe(applied_gain, raw_target, ratio >= gain.max_gain, report);
    if (desired == *y) {
        return {};
    }
    const auto changed = image::transport_luminance(original, desired);
    if (!changed) {
        return std::unexpected(changed.error());
    }
    if (*changed != original) {
        ++report.changed_samples;
        report.status = IlluminationStatus::applied;
        report.reason = IlluminationReason::none;
        std::ranges::copy(*changed, rgb.begin());
    }
    return {};
}
} // namespace docenhance::methods
