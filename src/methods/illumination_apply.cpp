// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "illumination_gain.hpp"

#include <algorithm>
#include <cmath>

namespace docenhance::methods {
IlluminationAdjustment illumination_adjustment(double y, double background,
                                               IlluminationGain gain) noexcept {
    const double ratio = illumination_gain_ratio(background, gain.target);
    const double applied_gain = std::clamp(ratio, 1.0, gain.max_gain);
    const double raw_target = y * std::pow(applied_gain, gain.strength);
    return {
        .target = std::clamp(raw_target, 0.0, 1.0),
        .gain = applied_gain,
        .capped = ratio >= gain.max_gain,
        .saturated = raw_target > 1,
    };
}
} // namespace docenhance::methods
